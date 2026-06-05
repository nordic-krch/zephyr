/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ICBMsg backend.
 *
 * This is an IPC service backend that dynamically allocates buffers for data storage
 * and uses ICMsg to send references to them.
 *
 * Shared memory organization
 * --------------------------
 *
 * Single channel (RX or TX) of the shared memory is divided into two areas: ICMsg area
 * followed by Blocks area. ICMsg is used to send and receive short 3-byte messages.
 * Blocks area is evenly divided into aligned blocks. Blocks are used to allocate
 * buffers containing actual data. Data buffers can span multiple blocks. The first block
 * starts with the size of the following data.
 *
 *  +------------+-------------+
 *  | ICMsg area | Blocks area |
 *  +------------+-------------+
 *       _______/               \_________________________________________
 *      /                                                                 \
 *      +-----------+-----------+-----------+-----------+-   -+-----------+
 *      |  Block 0  |  Block 1  |  Block 2  |  Block 3  | ... | Block N-1 |
 *      +-----------+-----------+-----------+-----------+-   -+-----------+
 *            _____/                                     \_____
 *           /                                                 \
 *           +------+--------------------------------+---------+
 *           | size | data_buffer[size] ...          | padding |
 *           +------+--------------------------------+---------+
 *
 * The sender holds information about reserved blocks using a 32-bit bitmask and it is
 * responsible for allocating and releasing the blocks. The receiver just tells the sender
 * that it does not need a specific buffer anymore.
 *
 * Control messages
 * ----------------
 *
 * ICMsg is used to send and receive small 3-byte control messages.
 *
 *  - Send data
 *    | MSG_DATA | endpoint address | block index |
 *    This message is used to send data buffer to specific endpoint.
 *
 *  - Release data
 *    | MSG_RELEASE_DATA | 0 | block index |
 *    This message is a response to the "Send data" message and it is used to inform that
 *    specific buffer is not used anymore and can be released. Endpoint addresses does
 *    not matter here, so it is zero.
 *
 *  - Bound endpoint
 *    | MSG_BOUND | endpoint address | block index |
 *    This message starts the bounding of the endpoint. The buffer contains a
 *    null-terminated endpoint name.
 *
 *  - Release bound endpoint
 *    | MSG_RELEASE_BOUND | endpoint address | block index |
 *    This message is a response to the "Bound endpoint" message and it is used to inform
 *    that a specific buffer (starting at "block index") is not used anymore and
 *    a the endpoint is bounded and can now receive a data.
 *
 * Bounding endpoints
 * ------------------
 *
 * When ICMsg is bounded and user registers an endpoint on initiator side, the backend
 * sends "Bound endpoint". Endpoint address is assigned by the initiator. When follower
 * gets the message and user on follower side also registered the same endpoint,
 * the backend calls "bound" callback and sends back "Release bound endpoint".
 * The follower saves the endpoint address. The follower's endpoint is ready to send
 * and receive data. When the initiator gets "Release bound endpoint" message or any
 * data messages, it calls bound endpoint and it is ready to send data.
 */

#include "zephyr/spinlock.h"
#include <stdint.h>
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L /* For strnlen() */

#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/stats/stats.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/hash_function.h>
#include <zephyr/sys/util.h>
#include <zephyr/ipc/ipc_service_backend.h>
#include <zephyr/cache.h>

#if defined(CONFIG_ARCH_POSIX)
#include <soc.h>
#define MAYBE_CONST
#else
#define MAYBE_CONST const
#endif

LOG_MODULE_REGISTER(ipc_icbmsg,
		    CONFIG_IPC_SERVICE_BACKEND_ICBMSG_LOG_LEVEL);


/** Size of the header (size field) of the block. */
#define ICBMSG_BLOCK_HEADER_SIZE (sizeof(struct icbmsg_block_header))

#define DT_DRV_COMPAT zephyr_ipc_icbmsg

/**
 * Required block alignment.
 */
#define BLOCK_ALIGNMENT sizeof(uint32_t)

/**
 * Returns required data cache alignment for instance "i".
 */
#define GET_CACHE_ALIGNMENT(i) \
	DT_INST_PROP_OR(i, dcache_alignment, 4)

#define MATCHING_CACHE_ALIGNMENT(i) \
	(GET_CACHE_ALIGNMENT(i) == GET_CACHE_ALIGNMENT(0)) &&
#if !(DT_INST_FOREACH_STATUS_OKAY(MATCHING_CACHE_ALIGNMENT) 1)
#error "Cache alignment needs to be the same for all instances"
#endif

/** Allowed number of endpoints. */
#define NUM_EPT CONFIG_IPC_SERVICE_BACKEND_ICBMSG_NUM_EP

/** Special endpoint address for control messages. */
#define EPT_CONTROL 0xFF

enum msg_type {
	MSG_EP_BOUND,		/* Endpoint bounding message. */
	MSG_EP_UNBOUND,		/* Endpoint unbounding message. */
};

enum state {
	STATE_IDLE = 0,
	STATE_CONNECTING,
	STATE_CONNECTED,
};

enum ept_state {
	EPT_UNBOUND = 0,
	EPT_READY,
};

#define MAX_ACTIVE_COUNT CONFIG_IPC_SERVICE_BACKEND_ICBMSG_MAX_ACTIVE_COUNT
#define HI_PRIO_MASK BIT(7)

#define SHM_PADDING_SIZE \
	GET_CACHE_ALIGNMENT(0) > sizeof(uint32_t) ? \
	((GET_CACHE_ALIGNMENT(0) - (MAX_ACTIVE_COUNT + sizeof(struct icbmsg_shm_q_hdr))) \
	 % GET_CACHE_ALIGNMENT(0)) \
	 : 0

struct icbmsg_shm_q_hdr {
	uint32_t block_idx;
	uint32_t hi_prio_cnt;
};

struct icbmsg_shm_q {
	struct icbmsg_shm_q_hdr hdr;
	uint8_t slots[MAX_ACTIVE_COUNT];
	uint8_t padding[SHM_PADDING_SIZE];
};

BUILD_ASSERT(sizeof(struct icbmsg_shm_q) % GET_CACHE_ALIGNMENT(0) == 0);

STATS_SECT_START(icbmsg_stats)
STATS_SECT_ENTRY32(tx_packet_block1_count)
STATS_SECT_ENTRY32(tx_packet_block2_count)
STATS_SECT_ENTRY32(tx_packet_block_more_count)
STATS_SECT_ENTRY32(tx_packet_count)
STATS_SECT_ENTRY32(tx_alloc_failed_count)
STATS_SECT_ENTRY32(tx_data_count)
STATS_SECT_ENTRY32(tx_max_active_count)
STATS_SECT_ENTRY32(tx_curr_block_usage_count)
STATS_SECT_ENTRY32(tx_max_block_usage_count)
STATS_SECT_ENTRY32(rx_packet_count)
STATS_SECT_ENTRY32(rx_data_count)
STATS_SECT_END;

STATS_NAME_START(icbmsg_stats)
STATS_NAME(icbmsg_stats, tx_packet_block1_count)
STATS_NAME(icbmsg_stats, tx_packet_block2_count)
STATS_NAME(icbmsg_stats, tx_packet_block_more_count)
STATS_NAME(icbmsg_stats, tx_packet_count)
STATS_NAME(icbmsg_stats, tx_alloc_failed_count)
STATS_NAME(icbmsg_stats, tx_data_count)
STATS_NAME(icbmsg_stats, tx_max_active_count)
STATS_NAME(icbmsg_stats, tx_curr_block_usage_count)
STATS_NAME(icbmsg_stats, tx_max_block_usage_count)
STATS_NAME(icbmsg_stats, rx_packet_count)
STATS_NAME(icbmsg_stats, rx_data_count)
STATS_NAME_END(icbmsg_stats);

struct icbmsg_msg_q_config {
	volatile struct icbmsg_shm_q *prod_shmq;
	volatile struct icbmsg_shm_q *cons_shmq;
};

struct icbmsg_msg_q_data {
	uint32_t tx_active_count;
	volatile uint32_t tx_local_cons_idx;
	volatile uint32_t rx_local_prod_idx;
	volatile uint32_t rx_local_hi_prio_cnt;
};

struct icbmsg_bound_packet {
	uint32_t magic[2];
	uint32_t buf_count[2];
};

struct icbmsg_ep_bound_packet {
	uint8_t ept_addr;
	uint32_t name_hash;
} __packed;

struct icbmsg_ep_unbound_packet {
	uint8_t ept_addr;
} __packed;

struct icbmsg_close_packet {
	uint8_t ept_addr;
} __packed;

struct icbmsg_control_packet {
	uint8_t msg_type;
	union {
		struct icbmsg_ep_bound_packet bound;
		struct icbmsg_ep_unbound_packet unbound;
	} data;
};

#define BOUND_PACKET_MAGIC_VALUE1 0x456789AB
#define BOUND_PACKET_MAGIC_VALUE2 0xAB896745
#define BOUND_PACKET_INIT(buf_count1, buf_count2) { \
	.magic = { BOUND_PACKET_MAGIC_VALUE1, BOUND_PACKET_MAGIC_VALUE2 }, \
	.buf_count = { buf_count1, buf_count2 }, \
}

struct icbmsg_block_header {
	uint16_t size;
	uint8_t index;
	uint8_t ept;
};

struct icbmsg_packet {
	struct icbmsg_block_header header;
	uint8_t data[];
};

struct icbmsg_heap_desc {
	uint8_t *blocks_ptr;
	size_t block_size;
	size_t block_count;
};

struct icbmsg_buf_heap {
	struct icbmsg_heap_desc desc;
#ifdef CONFIG_MULTITHREADING
	struct k_sem *block_wait_sem;
#endif
};

struct icbmsg_config {
	struct mbox_dt_spec mbox_tx;
	struct mbox_dt_spec mbox_rx;
	struct icbmsg_heap_desc rx;		/* RX heap config. */
	struct icbmsg_buf_heap tx;		/* TX block heap. */
	struct icbmsg_msg_q_config tx_msg_q;
	struct icbmsg_msg_q_config rx_msg_q;
	struct icbmsg_bound_packet bound_packet;
#ifdef CONFIG_STATS
	const char *stats_name;
#endif
};

struct ept_data {
	const struct ipc_ept_cfg *cfg;	/* Endpoint configuration. */
	uint32_t name_hash;
	uint8_t state;			/* Bounding state. */
	uint8_t addr;			/* Endpoint address. */
};

struct icbmsg_data {
	struct icbmsg_packet *rx_packet; /* Current RX packet. */
	struct icbmsg_msg_q_data msg_q;
	uint32_t tx_usage_mask;
	struct k_spinlock lock;
	STATS_SECT_DECL(icbmsg_stats) stats;
#ifdef CONFIG_MULTITHREADING
	struct k_sem block_wait_sem;	/* Semaphore for waiting for free blocks. */
#endif
	struct ept_data ept[NUM_EPT];	/* Array of registered endpoints. */
	int ep_cnt;
	uint8_t state;
};

BUILD_ASSERT(NUM_EPT <= EPT_CONTROL, "Too many endpoints");

#define CACHE_FLUSH(shmq) do {                                                 \
	sys_cache_data_flush_range((void *)shmq, sizeof(*shmq));               \
	compiler_barrier();                                                    \
} while (0)

#define CACHE_INV(shmq) do {                                                   \
	sys_cache_data_invd_range((void *)shmq, sizeof(*shmq));                \
	compiler_barrier();                                                    \
} while (0)

static inline struct icbmsg_packet *heap_packet_from_index(const struct icbmsg_heap_desc *heap,
								 size_t block_index)
{
	return (struct icbmsg_packet *)(heap->blocks_ptr + block_index * heap->block_size);
}

static inline struct icbmsg_packet *heap_packet_from_data(const void *data)
{
	return CONTAINER_OF(data, struct icbmsg_packet, data);
}

static inline size_t heap_max_data_size(const struct icbmsg_heap_desc *heap)
{
	return heap->block_size * heap->block_count - ICBMSG_BLOCK_HEADER_SIZE;
}

#define ICBMSG_HEAP_SAFE_CHECK 0

static struct icbmsg_packet *heap_get_packet(const struct icbmsg_heap_desc *heap, size_t blk_idx)
{
	size_t packet_size;
	size_t packet_blk_cnt;
	struct icbmsg_packet *packet;

	if (IS_ENABLED(ICBMSG_HEAP_SAFE_CHECK) && blk_idx >= heap->block_count) {
		LOG_ERR("Block index invalid %d", blk_idx);
		return NULL;
	}

	packet = heap_packet_from_index(heap, blk_idx);

	sys_cache_data_invd_range(packet, ICBMSG_BLOCK_HEADER_SIZE);
	compiler_barrier();

	packet_size = packet->header.size;
	packet_blk_cnt = DIV_ROUND_UP(packet_size + ICBMSG_BLOCK_HEADER_SIZE, heap->block_size);

	if (IS_ENABLED(ICBMSG_HEAP_SAFE_CHECK) && (blk_idx + packet_blk_cnt) > heap->block_count) {
		LOG_ERR("Block index invalid");
		return NULL;
	}

	sys_cache_data_invd_range(packet->data, packet_size);
	compiler_barrier();

	return packet;
}

static int heap_free_blocks(const struct device *instance, size_t block_index, size_t block_count, bool locked)
{
	struct icbmsg_data *data = instance->data;
	uint32_t mask = BIT_MASK(block_count) << block_index;
	int rv;

	if (locked) {
		if ((data->tx_usage_mask & mask) != mask) {
			rv = -EALREADY;
		} else {
			data->tx_usage_mask &= ~mask;
			rv = 0;
		}
		STATS_INCN(data->stats, tx_curr_block_usage_count, -block_count);
	} else {
		K_SPINLOCK(&data->lock) {
			if ((data->tx_usage_mask & mask) != mask) {
				rv = -EALREADY;
			} else {
				data->tx_usage_mask &= ~mask;
				rv = 0;
			}
			STATS_INCN(data->stats, tx_curr_block_usage_count, -block_count);
		}
	}
#ifdef CONFIG_MULTITHREADING
	if (rv == 0) {
		k_sem_give(&data->block_wait_sem);
	}
#endif
	return rv;
}

static int heap_free(const struct device *instance, struct icbmsg_packet *packet, bool locked)
{
	const struct icbmsg_config *config = instance->config;
	size_t block_count = DIV_ROUND_UP(packet->header.size + ICBMSG_BLOCK_HEADER_SIZE,
					  config->tx.desc.block_size);

	return heap_free_blocks(instance, packet->header.index, block_count, locked);
}

static void icbmsg_heap_downsize(const struct device *instance, struct icbmsg_packet *packet, size_t new_size)
{
	const struct icbmsg_config *config = instance->config;
	size_t new_total_size;
	size_t new_num_blocks;
	size_t prev_total_size;
	size_t prev_num_blocks;
	size_t index;
	size_t count;
	size_t size = packet->header.size;

	packet->header.size = new_size;
	if (size == new_size) {
		return;
	}

	__ASSERT_NO_MSG(size > new_size);

	prev_total_size = size + ICBMSG_BLOCK_HEADER_SIZE;
	new_total_size = new_size + ICBMSG_BLOCK_HEADER_SIZE;
	prev_num_blocks = DIV_ROUND_UP(prev_total_size, config->tx.desc.block_size);
	new_num_blocks = DIV_ROUND_UP(new_total_size, config->tx.desc.block_size);
	if (prev_num_blocks == new_num_blocks) {
		return;
	}

	index = packet->header.index + new_num_blocks;
	count = prev_num_blocks - new_num_blocks;

	(void)heap_free_blocks(instance, index, count, false);
}

static int heap_alloc_tx_buffer(const struct device *instance, struct ept_data *ept, size_t size,
			   struct icbmsg_packet **packet, k_timeout_t timeout)
{
	const struct icbmsg_config *config = instance->config;
	struct icbmsg_data *data = instance->data;
	size_t total_size;
	size_t num_blocks;
	size_t block_index = 0;
	bool max_size = size == 0;

	if ((ept != NULL) && (ept->state != EPT_READY)) {
		LOG_ERR("Endpoint %d is not ready", ept->addr);
		return -ENOENT;
	}

	total_size = size + ICBMSG_BLOCK_HEADER_SIZE;
	num_blocks = max_size ? config->tx.desc.block_count :
				DIV_ROUND_UP(total_size, config->tx.desc.block_size);
	while (true) {
		int off;

		K_SPINLOCK(&data->lock) {
			off = bitmask_find_gap(data->tx_usage_mask, num_blocks,
					       config->tx.desc.block_count, false);
			if (off >= 0) {
				data->tx_usage_mask |= BIT_MASK(num_blocks) << off;
				block_index = off;
				STATS_INCN(data->stats, tx_curr_block_usage_count, num_blocks);
				STATS_SET(data->stats, tx_max_block_usage_count,
					MAX(data->stats.tx_max_block_usage_count,
					    data->stats.tx_curr_block_usage_count));

				if (num_blocks == 1) {
					STATS_INC(data->stats, tx_packet_block1_count);
				} else if (num_blocks == 2) {
					STATS_INC(data->stats, tx_packet_block2_count);
				} else {
					STATS_INC(data->stats, tx_packet_block_more_count);
				}
			}
		}
		if (off >= 0) {
			break;
		} else if (max_size) {
			num_blocks--;
			if (num_blocks > 0) {
				continue;
			}
		}
#ifdef CONFIG_MULTITHREADING
		if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT) &&
		    !k_is_in_isr() && !k_is_pre_kernel()) {
			if (k_sem_take(&data->block_wait_sem, timeout) == 0) {
				continue;
			}
		}
#endif
		K_SPINLOCK(&data->lock) {
			STATS_INC(data->stats, tx_alloc_failed_count);
		}
		return -ENOMEM;
	}

	*packet = heap_packet_from_index(&config->tx.desc, block_index);
	(*packet)->header.index = block_index;
	(*packet)->header.size = config->tx.desc.block_size * num_blocks - ICBMSG_BLOCK_HEADER_SIZE;
	(*packet)->header.ept = ept == NULL ? EPT_CONTROL : ept->addr;

	return (*packet)->header.size;
}


static void msg_q_reset(const struct device *instance)
{
	struct icbmsg_data *data = instance->data;
	const struct icbmsg_config *config = instance->config;

	if (IS_ENABLED(CONFIG_IPC_SERVICE_BACKEND_ICBMSG_DEINIT)) {
		data->msg_q.tx_active_count = 0;
		data->msg_q.tx_local_cons_idx = 0;
		data->msg_q.rx_local_prod_idx = 0;
		data->msg_q.rx_local_hi_prio_cnt = 0;
	}

	config->tx_msg_q.prod_shmq->hdr.block_idx = 0;
	config->tx_msg_q.prod_shmq->hdr.hi_prio_cnt = 0;
	config->rx_msg_q.cons_shmq->hdr.block_idx = 0;
	config->rx_msg_q.cons_shmq->hdr.hi_prio_cnt = 0;

	CACHE_FLUSH(config->tx_msg_q.prod_shmq);
	CACHE_FLUSH(config->rx_msg_q.cons_shmq);
}

static int msg_q_consume(const struct device *instance, uint8_t block_index)
{
	const struct icbmsg_config *config = instance->config;
	struct icbmsg_data *data = instance->data;
	uint8_t idx;

	K_SPINLOCK(&data->lock) {
		idx = config->rx_msg_q.cons_shmq->hdr.block_idx % MAX_ACTIVE_COUNT;
		config->rx_msg_q.cons_shmq->slots[idx] = block_index;
		config->rx_msg_q.cons_shmq->hdr.block_idx++;
		LOG_DBG("%p Consume index: %d, block_index: %d, %p",
			(void *)config->rx_msg_q.cons_shmq,
			idx, config->rx_msg_q.cons_shmq->hdr.block_idx,
			(void *)&config->rx_msg_q.cons_shmq->slots[idx]);
	}
	CACHE_FLUSH(config->rx_msg_q.cons_shmq);

	return 0;
}


static void msg_q_garbage_collect(const struct device *instance)
{
	struct icbmsg_data *data = instance->data;
	const struct icbmsg_config *config = instance->config;
	uint32_t remote_idx;
	uint32_t local_idx;
	uint32_t cnt;

	/* TODO detect remote reset. */

	CACHE_INV(config->tx_msg_q.cons_shmq);

	/* Get all pending consumed blocks from both priority queues. */
	K_SPINLOCK(&data->lock) {
		remote_idx = config->tx_msg_q.cons_shmq->hdr.block_idx;
		local_idx = data->msg_q.tx_local_cons_idx;
		cnt = remote_idx - local_idx;
		LOG_DBG("%p Garbage collect remote_idx: %d local_idx: %d cnt:%d",
			(void *)config->tx_msg_q.cons_shmq, remote_idx,
			local_idx, cnt);
		__ASSERT_NO_MSG(cnt <= MAX_ACTIVE_COUNT);
		for (uint8_t i = 0; i < cnt; i++) {
			size_t idx = (local_idx + i) % MAX_ACTIVE_COUNT;
			size_t blk_idx = config->tx_msg_q.cons_shmq->slots[idx];
			struct icbmsg_packet *packet;

			LOG_DBG("Garbage collect index: idx:%d, block_index: %d",
				idx, blk_idx);
			packet = heap_get_packet(&config->tx.desc, blk_idx);
			(void)heap_free(instance, packet, true);
			data->msg_q.tx_local_cons_idx++;
			data->msg_q.tx_active_count--;
		}
	}
}
static int msg_q_produce(const struct device *instance, uint8_t block_index, int priority)
{
	struct icbmsg_data *data = instance->data;
	const struct icbmsg_config *config = instance->config;
	uint32_t active_count;
	uint32_t idx;
	int rv = 0;

	if (priority != 0) {
		block_index |= HI_PRIO_MASK;
	}

	K_SPINLOCK(&data->lock) {
		active_count = data->msg_q.tx_active_count;
		if (active_count == MAX_ACTIVE_COUNT) {
			rv = -ENOMEM;
			break;
		}

		idx = config->tx_msg_q.prod_shmq->hdr.block_idx % MAX_ACTIVE_COUNT;
		data->msg_q.tx_active_count++;
		STATS_INC(data->stats, tx_packet_count);
		STATS_INCN(data->stats, tx_data_count,
			   heap_packet_from_index(&config->tx.desc, block_index)->header.size);
		STATS_SET(data->stats, tx_max_active_count,
			  MAX(data->stats.tx_max_active_count, data->msg_q.tx_active_count));
		config->tx_msg_q.prod_shmq->slots[idx] = block_index;
		LOG_DBG("addr:%p Produce index: %d, block_index: %d, active_count: %d",
			(void *)&config->tx_msg_q.prod_shmq->slots[idx],
			idx, block_index, active_count);
		if (priority != 0) {
			config->tx_msg_q.prod_shmq->hdr.hi_prio_cnt++;
		}
		config->tx_msg_q.prod_shmq->hdr.block_idx++;
	}

	CACHE_FLUSH(config->tx_msg_q.prod_shmq);
	return rv;
}

static int msg_q_get_index(const struct device *instance)
{
	const struct icbmsg_config *config = instance->config;
	struct icbmsg_data *data = instance->data;
	uint32_t local_idx;
	uint32_t new_msgs;
	uint32_t hi_prio_msgs;

	local_idx = data->msg_q.rx_local_prod_idx;
	new_msgs = config->rx_msg_q.prod_shmq->hdr.block_idx - local_idx;
	hi_prio_msgs = config->rx_msg_q.prod_shmq->hdr.hi_prio_cnt - data->msg_q.rx_local_hi_prio_cnt;
	if (new_msgs == 0) {
		return -ENODATA;
	}
	if (new_msgs > MAX_ACTIVE_COUNT) {
		LOG_ERR("Broken message queue");
		return -EINVAL;
	}

	for (uint32_t i = 0; i < new_msgs; i++) {
		uint32_t idx = (local_idx + i) % MAX_ACTIVE_COUNT;
		uint32_t block_index = config->rx_msg_q.prod_shmq->slots[idx];

		if (hi_prio_msgs == 0) {
			LOG_DBG("Get index: %d, block_index: %d", idx, block_index);
			data->msg_q.rx_local_prod_idx++;
			return block_index;
		} else if (block_index & HI_PRIO_MASK) {
			data->msg_q.rx_local_prod_idx++;
			data->msg_q.rx_local_hi_prio_cnt++;
			block_index &= ~HI_PRIO_MASK;
			LOG_DBG("Get hi prio index: %d, block_index: %d", idx, block_index);
			return block_index;
		}
	}

	return -ENODATA;
}


/**
 * Send data contained in specified block. It will adjust data size and flush cache
 * if necessary. If sending failed, allocated blocks will be released.
 *
 * @param[in] msg_type		Message type: MSG_BOUND or MSG_DATA.
 * @param[in] packet		Packet to send.
 * @param[in] size		Size of the data to send.
 * @param[in] priority		Priority of the message.
 *
 * @return			number of bytes sent in the message or negative error code.
 */
static int send_packet(const struct device *instance, struct icbmsg_packet *packet, size_t size, int priority)
{
	int rv;
	const struct icbmsg_config *config = instance->config;
	uint32_t retry_cnt = 2;

	icbmsg_heap_downsize(instance, packet, size);
	sys_cache_data_flush_range(packet->data, size);

	LOG_DBG("send packet: %p [idx:%d, ept:%d prio:%d size:%d(%d)]",
		packet, packet->header.index, packet->header.ept, priority, packet->header.size, size);

	do {
		rv = msg_q_produce(instance, packet->header.index, priority);
		if (rv < 0) {
			retry_cnt--;
		} else {
			retry_cnt = 0;
			rv = mbox_send_dt(&config->mbox_tx, NULL);
		}

		if (packet->header.ept != EPT_CONTROL) {
			msg_q_garbage_collect(instance);
		}
	} while (retry_cnt > 0);

	if (rv < 0) {
		(void)heap_free(instance, packet, false);
	}

	return size;
}

static int handle_bound_request(const struct device *instance, struct icbmsg_packet *packet)
{
	struct icbmsg_data *data = instance->data;
	const struct icbmsg_config *config = instance->config;
	struct icbmsg_bound_packet *pkt = (struct icbmsg_bound_packet *)packet->data;

	LOG_DBG("Handle bound request: 0x%02X ep:%02x, magic: 0x%08X 0x%08X",
		packet->header.index, packet->header.ept, pkt->magic[0], pkt->magic[1]);
	if (data->state == STATE_CONNECTED) {
		// Remove device reset and sends new bound request.
		// Reset current state and send new bound request.
	}

	if (pkt->magic[0] != config->bound_packet.magic[0] || pkt->magic[1] != config->bound_packet.magic[1]) {
		LOG_ERR("Received invalid bound request");
		return -EINVAL;
	}

	if (pkt->buf_count[0] != config->bound_packet.buf_count[1] ||
	    pkt->buf_count[1] != config->bound_packet.buf_count[0]) {
		LOG_ERR("Received bound request but parameters do not match");
		return -EINVAL;
	}

	data->state = STATE_CONNECTED;

	return 0;
}

static int handle_ep_bound_request(const struct device *instance, struct icbmsg_control_packet *packet)
{
	struct icbmsg_data *data = instance->data;
	bool bound = false;
	size_t i;

	LOG_DBG("Received bound request: %p [ept:%d name hash:%08x]",
		packet, packet->data.bound.ept_addr, packet->data.bound.name_hash);

	for (i = 0; i < NUM_EPT; i++) {
		LOG_DBG("Checking endpoint %d name hash: %08x remote hash: %08x",
			i, data->ept[i].name_hash, packet->data.bound.name_hash);
		if ((data->ept[i].cfg != NULL) &&
		    (data->ept[i].name_hash == packet->data.bound.name_hash)) {
			data->ept[i].addr = packet->data.bound.ept_addr;
			LOG_DBG("Bound request bounded, remote id:%d local:%d",
				packet->data.bound.ept_addr, i);
			bound = true;
			goto bail;
		}
	}

	if (IS_ENABLED(CONFIG_IPC_SERVICE_BACKEND_ICBMSG_DEINIT)) {
		/* If deregistering is supported then there might be
		 * gaps in the array.
		 */
		for (i = 0; i < NUM_EPT; i++) {
			if (data->ept[i].cfg == NULL && data->ept[i].state == EPT_UNBOUND) {
				break;
			}
		}
	} else {
		i = data->ep_cnt;
	}
	__ASSERT_NO_MSG(i < NUM_EPT);
	data->ept[i].name_hash = packet->data.bound.name_hash;
	data->ept[i].addr = packet->data.bound.ept_addr;
	data->ep_cnt++;
	LOG_DBG("Bound request pending, remote id:%d local:%d", packet->data.bound.ept_addr, i);
bail:

	if (bound) {
		data->ept[i].state = EPT_READY;
		if (data->ept[i].cfg->cb.bound != NULL) {
			data->ept[i].cfg->cb.bound(data->ept[i].cfg->priv);
		}
	}

	return 0;
}

static int handle_ep_unbound_request(const struct device *instance, struct icbmsg_control_packet *packet)
{
	struct icbmsg_data *data = instance->data;
	uint8_t ept_addr = packet->data.unbound.ept_addr;
	typedef void (*unbound_callback_t)(void *priv);

	__ASSERT_NO_MSG(ept_addr < NUM_EPT);
	if (data->ept[ept_addr].cfg == NULL) {
		LOG_ERR("Endpoint %d is already unbounded", ept_addr);
		return 0;
	}

	LOG_DBG("Unbinding endpoint %d", ept_addr);

	unbound_callback_t unbound_callback = data->ept[ept_addr].cfg->cb.unbound;
	void *priv = data->ept[ept_addr].cfg->priv;

	data->ept[ept_addr].state = EPT_UNBOUND;
	data->ept[ept_addr].cfg = NULL;
	data->ept[ept_addr].name_hash = 0;
	data->ep_cnt--;

	if (unbound_callback != NULL) {
		unbound_callback(priv);
	}
	return 0;
}

static int received_control(const struct device *instance, struct icbmsg_packet *packet)
{
	struct icbmsg_data *data = instance->data;
	int rv;

	if (data->state != STATE_CONNECTED) {
		rv = handle_bound_request(instance, packet);
	} else {
		struct icbmsg_control_packet *pkt = (struct icbmsg_control_packet *)packet->data;

		if (pkt->msg_type == MSG_EP_BOUND) {
			rv = handle_ep_bound_request(instance, pkt);
		} else if (IS_ENABLED(CONFIG_IPC_SERVICE_BACKEND_ICBMSG_DEINIT) &&
			   (pkt->msg_type == MSG_EP_UNBOUND)) {
			rv = handle_ep_unbound_request(instance, pkt);
		} else {
			rv = -EINVAL;
		}
	}

	if (rv < 0) {
		LOG_ERR("Received invalid control packet");
	}

	msg_q_consume(instance, packet->header.index);

	return rv;
}

/**
 * Data message received.
 */
static int received_data(const struct device *instance, struct icbmsg_packet *packet)
{
	uint8_t ept_addr = packet->header.ept;
	struct icbmsg_data *data = instance->data;
	struct ept_data *ept;

	__ASSERT_NO_MSG(ept_addr < NUM_EPT);
	ept = &data->ept[ept_addr];
	if (ept->state != EPT_READY) {
		LOG_ERR("Received data packet in invalid state %d", ept->state);
		return -EINVAL;
	}

	LOG_DBG("Received data: %p [idx:%d, ept:%d] size:%d cb:%p",
		packet, packet->header.index, ept_addr, packet->header.size, ept->cfg->cb.received);
	STATS_INC(data->stats, rx_packet_count);
	STATS_INCN(data->stats, rx_data_count, packet->header.size);
	/* Call the endpoint callback. */
	ept->cfg->cb.received(packet->data, packet->header.size, ept->cfg->priv);

	/* If packet was not hold then release it.*/
	if (data->rx_packet != NULL) {
		msg_q_consume(instance, packet->header.index);
	}

	return 0;
}

static void mbox_callback(const struct device *dev, uint32_t channel,
			  void *user_data, struct mbox_msg *msg_data)
{
	const struct device *instance = user_data;
	struct icbmsg_data *data = instance->data;
	const struct icbmsg_config *config = instance->config;
	int index;
	int rv;
	struct icbmsg_packet *packet;

	CACHE_INV(config->rx_msg_q.prod_shmq);
	while ((index = msg_q_get_index(instance)) >= 0) {
		packet = heap_get_packet(&config->rx, index);
		if (packet == NULL) {
			LOG_ERR("Received invalid block index %d", index);
			continue;
		}
		data->rx_packet = packet;
		if (packet->header.ept == EPT_CONTROL) {
			received_control(instance, packet);
		} else if (data->state == STATE_CONNECTED) {
			rv = received_data(instance, packet);
			if (rv < 0) {
				LOG_ERR("Received invalid data packet");
			}
		}
	}
}

/**
 * Endpoint send callback function (with copy).
 */
static int send(const struct device *instance, void *token, const void *msg, size_t len)
{
	struct ept_data *ept = token;
	struct icbmsg_packet *packet;
	int rv;

	/* Allocate the buffer. */
	rv = heap_alloc_tx_buffer(instance, ept, len, &packet, K_NO_WAIT);
	if (rv < 0) {
		return rv;
	}

	/* Copy data to allocated buffer. */
	if (IS_ALIGNED(msg, 4) && !IS_ENABLED(CONFIG_SPEED_OPTIMIZATIONS)) {
		uint32_t *dst32 = (uint32_t *)packet->data;
		uint32_t *src32 = (uint32_t *)msg;
		uint8_t *dst8 = (uint8_t *)packet->data;
		uint8_t *src8 = (uint8_t *)msg;
		size_t len32 = len / sizeof(uint32_t);

		for (size_t i = 0; i < len32; i++) {
			dst32[i] = src32[i];
		}

		for (size_t i = len32 * sizeof(uint32_t); i < len; i++) {
			dst8[i] = src8[i];
		}
	} else {
		memcpy(packet->data, msg, len);
	}

	/* Send data message. */
	return send_packet(instance, packet, len, ept ? ept->cfg->prio : 0);
}

static int send_bound_request(const struct device *instance)
{
	const struct icbmsg_config *config = instance->config;

	return send(instance, NULL, &config->bound_packet, sizeof(config->bound_packet));
}

static int send_ep_bound_request(const struct device *instance, uint8_t id, uint32_t name_hash)
{
	struct icbmsg_control_packet pkt = {
		.msg_type = MSG_EP_BOUND,
		.data.bound.ept_addr = id,
		.data.bound.name_hash = name_hash,
	};

	return send(instance, NULL, &pkt, sizeof(pkt));
}

/**
 * Backend endpoint registration callback.
 */
static int register_ept(const struct device *instance, void **token,
			const struct ipc_ept_cfg *cfg)
{
	struct icbmsg_data *data = instance->data;
	uint32_t name_hash;
	k_spinlock_key_t key;
	uint8_t id = 0;
	bool bound = false;
	int ret = 0;

	name_hash = cfg->name == NULL ? 0 : sys_hash32_djb2(cfg->name, strlen(cfg->name));

	key = k_spin_lock(&data->lock);

	/* Start by searching if there is already pending request with a given hash. If
	 * yes then we are bounded on that side.
	 */
	for (size_t i = 0; i < NUM_EPT; i++) {
		if ((data->ept[i].cfg == NULL) && (data->ept[i].name_hash == name_hash)) {
			/* Remote bound request for that endpoint is already received. */
			LOG_DBG("Register EP (bounded), remote id:%d local:%d",
					data->ept[i].addr, i);
			bound = true;
			data->ept[i].cfg = cfg;
			*token = (void *)&data->ept[i];
			id = i;
			break;
		}
	}

	/* If there is no pending bounding request then store the current one. In both cases
	 * send bound request to the remote.
	 */
	if (!bound) {
		if (data->ep_cnt >= NUM_EPT) {
			ret = -ENOMEM;
		} else {
			if (IS_ENABLED(CONFIG_IPC_SERVICE_BACKEND_ICBMSG_DEINIT)) {
				/* If deregistering is supported then there might be
				 * gaps in the array.
				 */
				for (size_t i = 0; i < NUM_EPT; i++) {
					if (data->ept[i].cfg == NULL &&
					data->ept[i].state == EPT_UNBOUND) {
						id = i;
						break;
					}
				}
			} else {
				id = data->ep_cnt;
			}

			LOG_DBG("Register EP (pending), local:%d", data->ep_cnt);
			data->ept[id].cfg = cfg;
			data->ept[id].name_hash = name_hash;
			*token = (void *)&data->ept[id];
			data->ep_cnt++;
		}
	}

	k_spin_unlock(&data->lock, key);

	if (ret != 0) {
		return ret;
	}

	ret = send_ep_bound_request(instance, id, name_hash);
	if (ret < 0) {
		return ret;
	}

	if (bound) {
		data->ept[id].state = EPT_READY;
		if (cfg->cb.bound != NULL) {
			cfg->cb.bound(cfg->priv);
		}
	}

	return 0;
}

/**
 * Backend endpoint deregistration callback.
 */
static int deregister_ept(const struct device *instance, void *token)
{
	struct ept_data *ept = token;
	struct icbmsg_data *data = instance->data;
	int ret = 0;

	if (ept->state != EPT_READY) {
		return -ENOENT;
	}

	struct icbmsg_control_packet packet = {
		.msg_type = MSG_EP_UNBOUND,
		.data.unbound.ept_addr = ept->addr,
	};

	for (size_t i = 0; i < NUM_EPT; i++) {
		if (&data->ept[i] == ept) {
			ret = send(instance, NULL, &packet, sizeof(packet));
			data->ept[i].cfg = NULL;
			data->ept[i].name_hash = 0;
			data->ept[i].state = EPT_UNBOUND;
			data->ep_cnt--;
			return ret >= 0 ? 0 : ret;
		}
	}

	return -EINVAL;
}

/**
 * Returns maximum TX buffer size.
 */
static int get_tx_buffer_size(const struct device *instance, void *token)
{
	const struct icbmsg_config *conf = instance->config;

	return heap_max_data_size(&conf->tx.desc);
}

/**
 * Endpoint TX buffer allocation callback for nocopy sending.
 */
static int get_tx_buffer(const struct device *instance, void *token, void **buffer,
			 uint32_t *user_len, k_timeout_t wait)
{
	struct icbmsg_packet *packet;
	struct ept_data *ept = token;
	int rv;

	rv = heap_alloc_tx_buffer(instance, ept, *user_len, &packet, wait);
	if (rv < 0) {
		return rv;
	}

	*user_len = rv;
	*buffer = packet->data;

	return 0;
}

/**
 * Endpoint TX buffer release callback for nocopy sending.
 */
static int drop_tx_buffer(const struct device *instance, void *token, const void *buffer)
{
	ARG_UNUSED(token);
	return heap_free(instance, heap_packet_from_data(buffer), false);
}

/**
 * Endpoint nocopy sending.
 */
static int send_nocopy(const struct device *instance, void *token, const void *buffer,
		       size_t len)
{
	struct icbmsg_packet *packet = heap_packet_from_data(buffer);
	struct ept_data *ept = token;

	__ASSERT_NO_MSG(packet->header.ept == ept->addr);

	return send_packet(instance, packet, len, ept->cfg->prio);
}

/**
 * Holding RX buffer for nocopy receiving.
 */
static int hold_rx_buffer(const struct device *instance, void *token, void *buffer)
{
	struct icbmsg_data *dev_data = instance->data;
	struct icbmsg_packet *packet = heap_packet_from_data(buffer);

	__ASSERT_NO_MSG(token != NULL);
	if (packet == dev_data->rx_packet) {
		dev_data->rx_packet = NULL;
		return 0;
	}

	return -EINVAL;
}

/**
 * Release RX buffer that was previously held.
 */
static int release_rx_buffer(const struct device *instance, void *token, void *buffer)
{
	ARG_UNUSED(token);
	struct icbmsg_packet *packet = heap_packet_from_data(buffer);

	return msg_q_consume(instance, packet->header.index);
}

static int close(const struct device *instance)
{
	struct icbmsg_data *data = instance->data;
	const struct icbmsg_config *config = instance->config;

	if (data->ep_cnt != 0) {
		return -EBUSY;
	}

	LOG_DBG("Closing instance %p", instance);
	msg_q_reset(instance);
	data->state = STATE_IDLE;
	return mbox_set_enabled_dt(&config->mbox_rx, false);
}

/**
 * Open the backend instance callback.
 */
static int open(const struct device *instance)
{
	const struct icbmsg_config *config = instance->config;
	struct icbmsg_data *data = instance->data;
	int rv;

	if (!device_is_ready(instance) ||
	    !mbox_is_ready_dt(&config->mbox_rx) ||
	    !mbox_is_ready_dt(&config->mbox_tx)) {
		return -EAGAIN;
	}

	if (data->state != STATE_IDLE) {
		return -EALREADY;
	}

	LOG_DBG("Shm_q TX prod: 0x%08X, cons: 0x%08X",
		(uint32_t)config->tx_msg_q.prod_shmq, (uint32_t)config->tx_msg_q.cons_shmq);
	LOG_DBG("Shm_q RX prod: 0x%08X, cons: 0x%08X",
		(uint32_t)config->rx_msg_q.prod_shmq, (uint32_t)config->rx_msg_q.cons_shmq);
	LOG_DBG("  TX %d blocks of %d bytes at 0x%08X, max allocable %d bytes",
		(uint32_t)config->tx.desc.block_count,
		(uint32_t)config->tx.desc.block_size,
		(uint32_t)config->tx.desc.blocks_ptr,
		(uint32_t)heap_max_data_size(&config->tx.desc));
	LOG_DBG("  RX %d blocks of %d bytes at 0x%08X, max allocable %d bytes",
		(uint32_t)config->rx.block_count,
		(uint32_t)config->rx.block_size,
		(uint32_t)config->rx.blocks_ptr,
		(uint32_t)heap_max_data_size(&config->rx));

	data->state = STATE_CONNECTING;
	rv = send_bound_request(instance);
	if (rv < 0) {
		return rv;
	}

	CACHE_INV(config->rx_msg_q.prod_shmq);

	rv = mbox_register_callback_dt(&config->mbox_rx, mbox_callback, (void *)instance);
	if (rv < 0) {
		return rv;
	}

	return mbox_set_enabled_dt(&config->mbox_rx, true);
}



/**
 * Backend device initialization.
 */
static int backend_init(const struct device *instance)
{
#if defined(CONFIG_ARCH_POSIX)
	MAYBE_CONST struct icbmsg_config *conf = (struct icbmsg_config *)instance->config;

	native_emb_addr_remap((void **)&conf->tx.desc.blocks_ptr);
	native_emb_addr_remap((void **)&conf->rx.blocks_ptr);
	native_emb_addr_remap((void **)&conf->tx_msg_q.prod_shmq);
	native_emb_addr_remap((void **)&conf->tx_msg_q.cons_shmq);
	native_emb_addr_remap((void **)&conf->rx_msg_q.prod_shmq);
	native_emb_addr_remap((void **)&conf->rx_msg_q.cons_shmq);
#endif

#if defined(CONFIG_STATS_NAMES) || defined(CONFIG_MULTITHREADING)
	struct icbmsg_data *data = instance->data;
#endif

#ifdef CONFIG_MULTITHREADING
	k_sem_init(&data->block_wait_sem, 0, 1);
#endif

#ifdef CONFIG_STATS
	(void)stats_init(&data->stats.s_hdr,
		STATS_SIZE_INIT_PARMS(data->stats, STATS_SIZE_32),
		STATS_NAME_INIT_PARMS(icbmsg_stats));
	stats_register(((struct icbmsg_config *)instance->config)->stats_name,
			&(data->stats.s_hdr));
#endif
	msg_q_reset(instance);

	return 0;
}

/**
 * IPC service backend callbacks.
 */
const static struct ipc_service_backend backend_ops = {
	.open_instance = open,
	.close_instance = IS_ENABLED(CONFIG_IPC_SERVICE_BACKEND_ICBMSG_DEINIT) ? close : NULL,
	.send = send,
	.send_critical = send,
	.register_endpoint = register_ept,
	.deregister_endpoint = IS_ENABLED(CONFIG_IPC_SERVICE_BACKEND_ICBMSG_DEINIT) ?
		deregister_ept : NULL,
	.get_tx_buffer_size = get_tx_buffer_size,
	.get_tx_buffer = get_tx_buffer,
	.drop_tx_buffer = drop_tx_buffer,
	.send_nocopy = send_nocopy,
	.hold_rx_buffer = hold_rx_buffer,
	.release_rx_buffer = release_rx_buffer,
};

/**
 * Size of a single shared-memory message queue at the start of each region.
 */
#define GET_MSG_Q_AREA_SIZE(i) \
	ROUND_UP(sizeof(struct icbmsg_shm_q), GET_CACHE_ALIGNMENT(i))

/**
 * Total queue area per region (producer + consumer queues).
 */
#define GET_MSG_Q_REGION_SIZE(i) \
	(2 * GET_MSG_Q_AREA_SIZE(i))

/**
 * Address of the consumer queue in a shared memory region.
 */
#define GET_CONS_SHMQ_ADDR_INST(i, direction) \
	(GET_MEM_ADDR_INST(i, direction) + GET_MSG_Q_AREA_SIZE(i))

/**
 * Calculate aligned block size by evenly dividing remaining space after the queue area.
 */
#define GET_BLOCK_SIZE(i, total_size, local_blocks, remote_blocks) ROUND_DOWN(		\
	((total_size) - GET_MSG_Q_REGION_SIZE(i)) / (local_blocks), GET_CACHE_ALIGNMENT(i))

/**
 * Calculate offset where the blocks area starts (after the message queue).
 */
#define GET_BLOCKS_OFFSET(i, total_size, local_blocks, remote_blocks)			\
	((total_size) - GET_BLOCK_SIZE(i, (total_size), (local_blocks),			\
				       (remote_blocks)) * (local_blocks))

/**
 * Return shared memory start address aligned to block alignment and cache line.
 */
#define GET_MEM_ADDR_INST(i, direction) \
	ROUND_UP(DT_REG_ADDR(DT_INST_PHANDLE(i, direction##_region)),			\
		 GET_CACHE_ALIGNMENT(i))

/**
 * Return shared memory end address aligned to block alignment and cache line.
 */
#define GET_MEM_END_INST(i, direction)							\
	ROUND_DOWN(DT_REG_ADDR(DT_INST_PHANDLE(i, direction##_region)) +		\
		   DT_REG_SIZE(DT_INST_PHANDLE(i, direction##_region)),			\
		   GET_CACHE_ALIGNMENT(i))

/**
 * Return shared memory size aligned to block alignment and cache line.
 */
#define GET_MEM_SIZE_INST(i, direction) \
	(GET_MEM_END_INST(i, direction) - GET_MEM_ADDR_INST(i, direction))

/**
 * Returns address where area for blocks starts for specific instance and direction.
 * 'loc' and 'rem' parameters tells the direction. They can be either "tx, rx"
 *  or "rx, tx".
 */
#define GET_BLOCKS_ADDR_INST(i, loc, rem)					\
	GET_MEM_ADDR_INST(i, loc) +						\
	GET_BLOCKS_OFFSET(							\
		i,								\
		GET_MEM_SIZE_INST(i, loc),					\
		DT_INST_PROP(i, loc##_blocks),					\
		DT_INST_PROP(i, rem##_blocks))

/**
 * Returns block size for specific instance and direction.
 * 'loc' and 'rem' parameters tells the direction. They can be either "tx, rx"
 *  or "rx, tx".
 */
#define GET_BLOCK_SIZE_INST(i, loc, rem)					\
	GET_BLOCK_SIZE(								\
		i,								\
		GET_MEM_SIZE_INST(i, loc),					\
		DT_INST_PROP(i, loc##_blocks),					\
		DT_INST_PROP(i, rem##_blocks))


#define DEFINE_BACKEND_DEVICE(i)							\
	static struct icbmsg_data icbmsg_data_##i;					\
	static MAYBE_CONST struct icbmsg_config icbmsg_config_##i = {			\
		.mbox_tx = MBOX_DT_SPEC_INST_GET(i, tx),				\
		.mbox_rx = MBOX_DT_SPEC_INST_GET(i, rx),				\
		.rx = {								\
			.blocks_ptr = (uint8_t *)GET_BLOCKS_ADDR_INST(i, rx, tx),	\
			.block_count = DT_INST_PROP(i, rx_blocks),			\
			.block_size = GET_BLOCK_SIZE_INST(i, rx, tx),			\
		},								\
		.tx = {								\
			.desc = {						\
				.blocks_ptr = (uint8_t *)GET_BLOCKS_ADDR_INST(i, tx, rx), \
				.block_count = DT_INST_PROP(i, tx_blocks),		\
				.block_size = GET_BLOCK_SIZE_INST(i, tx, rx),		\
			},							\
			COND_CODE_1(CONFIG_MULTITHREADING,				\
				    (.block_wait_sem = &icbmsg_data_##i.block_wait_sem,), \
				    ())						\
		},								\
		.tx_msg_q = {							\
			.prod_shmq = (struct icbmsg_shm_q *)GET_MEM_ADDR_INST(i, tx),	\
			.cons_shmq = (struct icbmsg_shm_q *)GET_CONS_SHMQ_ADDR_INST(i, tx), \
		},								\
		.rx_msg_q = {							\
			.prod_shmq = (struct icbmsg_shm_q *)GET_MEM_ADDR_INST(i, rx),	\
			.cons_shmq = (struct icbmsg_shm_q *)GET_CONS_SHMQ_ADDR_INST(i, rx), \
		},								\
		.bound_packet = BOUND_PACKET_INIT(DT_INST_PROP(i, tx_blocks),		\
						 DT_INST_PROP(i, rx_blocks)),		\
		IF_ENABLED(CONFIG_STATS_NAMES, (.stats_name = STRINGIFY(ipc_icbmsg_##i),))	\
	};										\
	BUILD_ASSERT(IS_ALIGNED(GET_MEM_ADDR_INST(i, tx), GET_CACHE_ALIGNMENT(i)),	\
		     "TX producer queue is not aligned to cache alignment");		\
	BUILD_ASSERT(IS_ALIGNED(GET_MEM_ADDR_INST(i, rx), GET_CACHE_ALIGNMENT(i)),	\
		     "RX producer queue is not aligned to cache alignment");		\
	BUILD_ASSERT(IS_ALIGNED(GET_CONS_SHMQ_ADDR_INST(i, tx), GET_CACHE_ALIGNMENT(i)),\
		     "TX consumer queue is not aligned to cache alignment");		\
	BUILD_ASSERT(IS_ALIGNED(GET_CONS_SHMQ_ADDR_INST(i, rx), GET_CACHE_ALIGNMENT(i)),\
		     "RX consumer queue is not aligned to cache alignment");		\
	BUILD_ASSERT(IS_POWER_OF_TWO(GET_CACHE_ALIGNMENT(i)),				\
		     "This module supports only power of two cache alignment");		\
	BUILD_ASSERT(GET_MSG_Q_REGION_SIZE(i) <=					\
		     GET_BLOCKS_OFFSET(i, GET_MEM_SIZE_INST(i, tx),			\
				      DT_INST_PROP(i, tx_blocks),			\
				      DT_INST_PROP(i, rx_blocks)),			\
		     "TX region is too small for the message queues");			\
	BUILD_ASSERT(GET_MSG_Q_REGION_SIZE(i) <=					\
		     GET_BLOCKS_OFFSET(i, GET_MEM_SIZE_INST(i, rx),			\
				      DT_INST_PROP(i, rx_blocks),			\
				      DT_INST_PROP(i, tx_blocks)),			\
		     "RX region is too small for the message queues");			\
	BUILD_ASSERT((GET_BLOCK_SIZE_INST(i, tx, rx) >= BLOCK_ALIGNMENT) &&		\
		     (GET_BLOCK_SIZE_INST(i, tx, rx) <					\
		      GET_MEM_SIZE_INST(i, tx)),					\
		     "TX region is too small for provided number of blocks");		\
	BUILD_ASSERT((GET_BLOCK_SIZE_INST(i, rx, tx) >= BLOCK_ALIGNMENT) &&		\
		     (GET_BLOCK_SIZE_INST(i, rx, tx) <					\
		      GET_MEM_SIZE_INST(i, rx)),					\
		     "RX region is too small for provided number of blocks");		\
	BUILD_ASSERT(DT_INST_PROP(i, rx_blocks) <= 32, "Too many RX blocks");		\
	BUILD_ASSERT(DT_INST_PROP(i, tx_blocks) <= 32, "Too many TX blocks");		\
	DEVICE_DT_INST_DEFINE(i,							\
			      &backend_init,						\
			      NULL,							\
			      &icbmsg_data_##i,						\
			      &icbmsg_config_##i,					\
			      POST_KERNEL,						\
			      UTIL_INC(CONFIG_MBOX_INIT_PRIORITY),							\
			      &backend_ops);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_BACKEND_DEVICE)

#ifdef CONFIG_IPC_SERVICE_BACKEND_ICBMSG_SHELL

#include <zephyr/shell/shell.h>
#include <inttypes.h>

static const char *icbmsg_state_str(uint8_t state)
{
	switch (state) {
	case STATE_IDLE:
		return "idle";
	case STATE_CONNECTING:
		return "connecting";
	case STATE_CONNECTED:
		return "connected";
	default:
		return "unknown";
	}
}

static void icbmsg_print_instance_stats(const struct shell *sh,
					const struct device *instance)
{
	const struct icbmsg_config *conf = instance->config;
	struct icbmsg_data *data = instance->data;

	shell_print(sh, "ICBMsg instance: %s (%s)", instance->name, icbmsg_state_str(data->state));

	shell_print(sh, "  TX memory: %zu blocks of %zu B (max payload %zu B)",
		    conf->tx.desc.block_count, conf->tx.desc.block_size,
		    heap_max_data_size(&conf->tx.desc));
	shell_print(sh, "  TX max block usage: peak %u/%u (%u%%)",
		    data->stats.tx_max_block_usage_count, conf->tx.desc.block_count,
		    (unsigned int)((data->stats.tx_max_block_usage_count * 100U) /
		    conf->tx.desc.block_count));
	shell_print(sh, "  TX max slot usage: peak %u/%u (%u%%)",
		    data->stats.tx_max_active_count, MAX_ACTIVE_COUNT,
		    (unsigned int)((data->stats.tx_max_active_count * 100U) / MAX_ACTIVE_COUNT));

	shell_print(sh, "  RX memory: %zu blocks of %zu B (max payload %zu B)",
		    conf->rx.block_count, conf->rx.block_size,
		    heap_max_data_size(&conf->rx));

	shell_print(sh, "  Endpoints: %d / %d registered", data->ep_cnt, NUM_EPT);
	for (int i = 0; i < data->ep_cnt; i++) {
		shell_print(sh, "    [%d] %s", i, data->ept[i].cfg->name);
	}

	shell_print(sh, "  TX traffic:");
	shell_print(sh, "    packets: %u (%u bytes total)",
		    data->stats.tx_packet_count, data->stats.tx_data_count);
	shell_print(sh, "    by block count: 1=%u, 2=%u, 3+=%u",
		    data->stats.tx_packet_block1_count, data->stats.tx_packet_block2_count,
		    data->stats.tx_packet_block_more_count);
	shell_print(sh, "    allocation failures: %u", data->stats.tx_alloc_failed_count);

	shell_print(sh, "  RX traffic:");
	shell_print(sh, "    packets: %u (%u bytes total)",
		    data->stats.rx_packet_count, data->stats.rx_data_count);
}

#define ICBMSG_DEVICE(i) DEVICE_DT_INST_GET(i),

static int cmd_icbmsg_stats(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	static const struct device *const icbmsg_devices[] = {
		DT_INST_FOREACH_STATUS_OKAY(ICBMSG_DEVICE)
	};

	for (size_t i = 0; i < ARRAY_SIZE(icbmsg_devices); i++) {
		if (!device_is_ready(icbmsg_devices[i])) {
			shell_warn(sh, "ICBMsg instance %s is not ready", icbmsg_devices[i]->name);
			continue;
		}

		icbmsg_print_instance_stats(sh, icbmsg_devices[i]);
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_icbmsg,
			       SHELL_CMD(stats, NULL, "Print ICBMsg statistics", cmd_icbmsg_stats),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(icbmsg, &sub_icbmsg, "ICBMsg IPC backend commands", NULL);

#endif /* CONFIG_IPC_SERVICE_BACKEND_ICBMSG_SHELL */

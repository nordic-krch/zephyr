/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/logging/log_frontend.h>
#include <zephyr/logging/log_frontend_dict.h>
#include <zephyr/logging/log_output_dict.h>
#include <zephyr/logging/log_internal.h>
#include <zephyr/sys/cobs.h>

/* Message contains:
 * Msg_header (4 bytes)
 *   - type, level, source
 *   - data present flag + timestamp details
 *   - atomically incremented counter to maintain ordering (and detect dropping)
 * Package (8 - n bytes)
 * (optional) Hexdump data 2 bytes of length followed by (1 - n bytes)
 * Timestamp (0-8 bytes)
 *
 * Shortest message has 13 bytes.
 *
 * ts_bytes (timestamp bytes encoding):
 * - 0 - 8 bytes
 * - 1 - 1 byte
 * - 2 - 2 bytes
 * - 3 - 3 bytes
 * - 4 - 4 bytes
 * - 7 - not timestamp
 */
struct log_frontend_msg_hdr {
	uint32_t type:2;
	uint32_t level:3;
	uint32_t ts_bytes:3;
	uint32_t source:12;
	uint32_t data:1;
	uint32_t cnt:10;
	uint32_t reserved:1;
};

struct log_frontend_msg_dropped_hdr {
	uint16_t type:2;
	uint16_t count:14;
} __packed;

BUILD_ASSERT(sizeof(struct log_frontend_msg_hdr) == sizeof(uint32_t));

struct log_frontend_pkt_hdr {
	MPSC_PBUF_HDR;
	uint16_t len: 12;
	uint16_t noff: 2;
} __packed;

BUILD_ASSERT(sizeof(struct log_frontend_pkt_hdr) == sizeof(uint16_t));

struct log_frontend_generic_pkt {
	struct log_frontend_pkt_hdr hdr;
	uint8_t data[];
} __packed;

struct log_frontend_generic_std_pkt {
	struct log_frontend_pkt_hdr hdr;
#if !defined(CONFIG_LOG_FRONTEND_DICT_COBS)
	uint16_t padding;
#endif
	/* We want to have the data correctly aligned. */
	uint8_t data[];
} __packed;

struct log_frontend_dropped_pkt {
	struct log_frontend_pkt_hdr hdr;
	struct log_frontend_msg_dropped_hdr msg;
	/* if COBS/R is used. */
	uint8_t delimiter;
} __packed;

struct log_frontend_log_pkt {
	struct log_frontend_pkt_hdr hdr;
	struct log_frontend_msg_dropped_hdr msg;
	uint8_t data[];
} __packed;

/* Union needed to avoid warning when casting to packed structure. */
union log_frontend_pkt {
	struct log_frontend_generic_pkt *generic;
	struct log_frontend_std_pkt *std;
	struct log_frontend_dropped_pkt *dropped;
	struct log_frontend_log_pkt *log;
	const union mpsc_pbuf_generic *ro_pkt;
	union mpsc_pbuf_generic *rw_pkt;
	void *buf;
};

static uint32_t dbuf[CONFIG_LOG_FRONTEND_DICT_BUF_SIZE / sizeof(uint32_t)];

static uint32_t get_wlen(const union mpsc_pbuf_generic *packet);
static void notify_drop(const struct mpsc_pbuf_buffer *buffer,
			const union mpsc_pbuf_generic *packet);

static const struct mpsc_pbuf_buffer_config config = {
	.buf = dbuf,
	.size = ARRAY_SIZE(dbuf),
	.notify_drop = notify_drop,
	.get_wlen = get_wlen,
	.flags = 0
};

static struct mpsc_pbuf_buffer buf;
static atomic_t dropped;
static atomic_t tx_active;
static uint32_t full_ts_cnt;
static uint64_t prev_ts;
static struct k_work sink_work;
static bool in_panic;

static atomic_t log_cnt;
#define PKT_WSIZE(nargs) \
	(sizeof(struct log_frontend_log_pkt) + (3 + nargs) * sizeof(uint32_t)) / sizeof(uint32_t)

#define PKT_SIZE(nargs) \
	(sizeof(struct log_frontend_log_pkt) + (2 + nargs) * sizeof(uint32_t)) + 2

static uint32_t get_wlen(const union mpsc_pbuf_generic *packet)
{
	struct log_frontend_generic_pkt *hdr = (struct log_frontend_generic_pkt *)packet;

	return (uint32_t)hdr->hdr.len;
}

static void notify_drop(const struct mpsc_pbuf_buffer *buffer,
			const union mpsc_pbuf_generic *packet)
{
}

static inline uint16_t get_source_id(const void * source)
{
	return (source != NULL) ?
				(IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ?
					log_dynamic_source_id((void *)source) :
					log_const_source_id(source)) :
				0U;
}


static inline void hdr_set(struct log_frontend_msg_hdr *hdr, uint32_t level, const void *source,
		uint8_t ts_code, bool data)
{
	hdr->type = MSG_NORMAL;
	hdr->level = level;
	hdr->ts_bytes = ts_code;
	hdr->source = get_source_id(source);
	hdr->data = data ? 1 : 0;
	hdr->cnt = atomic_inc(&log_cnt);
}

static bool pkt_send(void)
{
	union log_frontend_pkt pkt;
	uint8_t *data;
	size_t len;

	do {
		pkt.ro_pkt = mpsc_pbuf_claim(&buf);
		if (pkt.ro_pkt == NULL) {
			atomic_set(&tx_active, 0);
			return false;
		}

		if (IS_ENABLED(CONFIG_LOG_FRONTEND_DICT_COBS)) {
			size_t cobs_len = 0/*TODO*/ + sizeof(uint8_t) +
					  sizeof(struct log_dict_output_normal_msg_hdr_t);

			if (cobs_r_encode((uint8_t *)&pkt.log->cobs_hdr, cobs_len)) {
				pkt.log->hdr.noff--;
			}

			data = (uint8_t *)&pkt.log->cobs_hdr;
			len = 0;
		} else {
			data = (uint8_t *)&pkt.log->data_hdr;
			len = pkt.generic->hdr.len * sizeof(uint32_t) - pkt.generic->hdr.noff -
				offsetof(struct log_frontend_generic_pkt, data);
		}

		if (in_panic || !IS_ENABLED(CONFIG_LOG_FRONTEND_DICT_ASYNC)) {
			(void)log_frontend_dict_tx_blocking(data, len, in_panic);
			mpsc_pbuf_free(&buf, pkt.ro_pkt);
		} else {
			(void)log_frontend_dict_tx_async(data, len);
		}
	} while (in_panic);

	return true;
}

void log_frontend_dict_tx_from_cb(void)
{
	if (!pkt_send()) {
		atomic_set(&tx_active, 0);
	}
}

static void work_handler(struct k_work *work)
{
	if (pkt_send()) {
		k_work_submit(work);
	}
}

static void pkt_try_send(void)
{
	if (in_panic) {
		pkt_send();
		return;
	}

	if (k_is_pre_kernel()) {
		return;
	}

	if (IS_ENABLED(CONFIG_LOG_FRONTEND_DICT_ASYNC)) {
		if ((atomic_cas(&tx_active, 0, 1) == false)) {
			return;
		}

		pkt_send();
	} else {
		(void)k_work_submit(&sink_work);
	}
}

static void *pkt_alloc(size_t len)
{
	size_t wlen = DIV_ROUND_UP(len, sizeof(uint32_t));

	return (void *)mpsc_pbuf_alloc(&buf, wlen, K_NO_WAIT);
}

static void pkt_store(union log_frontend_pkt pkt, size_t len)
{
	pkt.rw_pkt->hdr.valid = 1;
	mpsc_pbuf_put_data(&buf, (const uint32_t *)pkt.rw_pkt, len);
}

static void pkt_free(union log_frontend_pkt pkt)
{
	mpsc_pbuf_free(&buf, pkt.ro_pkt);
}

static void pkt_blocking_process(union log_frontend_pkt pkt, size_t plen)
{
	if (atomic_cas(&tx_active, 0, 1)) {
		pkt_blocking_tx(pkt, plen);
	} else {
		pkt_store(pkt, plen);
		pkt_try_send();
	}
}

static void pkt_commit_process(union log_frontend_pkt pkt, size_t len)
{
	mpsc_pbuf_commit(&buf, pkt.rw_pkt);
	pkt_try_send();
}

void pkt_process(union log_frontend_pkt pkt, size_t plen)
{
	if (IS_ENABLED(CONFIG_LOG_FRONTEND_DICT_BLOCKING)) {
		pkt_blocking_process(pkt, plen);
		return;
	}

	pkt_store(pkt, plen);
	pkt_try_send();
}

#define NO_TS 0
#define FULL_TS 7

/* Get timestamp. Timestamp can be compressed, encoded as diff between previous
 * timestamp and the current one.
 */
uint8_t get_ts(uint64_t *ts)
{
	if (!IS_ENABLED(CONFIG_LOG_FRONTEND_DICT_TIMESTAMP)) {
		return NO_TS;
	}

	bool full_ts;
	k_spinlock_key_t key = k_spin_lock(&lock);
	uint64_t t = z_log_timestamp();
	uint64_t rt;
	full_ts_cnt--;

	if (full_ts_cnt == 0) {
		full_ts_cnt = CONFIG_LOG_FRONTEND_DICT_FULL_TS_PERIOD;
		prev_ts = t;
		rt = t;
		full_ts = true;
	} else {
		rt = t - prev_ts;
		prev_ts = t;
		full_ts = false;
	}

	k_spin_unlock(&lock, key);

	*ts = rt;

	if (full_ts) {
		return FULL_TS;
	}

	if (rt == 0) {
		return NO_TS;
	} else if (rt <= 0xFFFFFFFFULL) {
		return 4 - __builtin_clz((uint32_t)rt) / 8;
	} else {
		return FULL_TS;
	}
}

void log_frontend_msg(const void *source,
		      const struct log_msg_desc desc,
		      uint8_t *package, const void *data)
{
	void *buf;
	uint16_t strl[4];
	int plen = cbprintf_package_copy(package, desc.package_len, NULL, 0,
					 CBPRINTF_PACKAGE_CONVERT_RW_STR,
					 strl, ARRAY_SIZE(strl));
	size_t dlen = desc.data_len;
	size_t total_len = plen + dlen + sizeof(struct log_frontend_log_pkt);

	buf = pkt_alloc(total_len);
	if (buf == NULL) {
		atomic_inc(&dropped);
		return;
	}

	union log_frontend_pkt pkt = { .buf = buf };
	uint32_t *dst_package = (uint32_t *)pkt.log->data;

	hdr_set(pkt.log, desc.level, source, plen, 0);
	int err = cbprintf_package_copy(package, desc.package_len, dst_package, plen,
					 CBPRINTF_PACKAGE_CONVERT_RW_STR,
					 strl, ARRAY_SIZE(strl));
	if (err < 0) {
		pkt_free(pkt);
		atomic_inc(&dropped);
		return;
	}

	if (dlen != 0) {
		memcpy(&pkt.log->data[plen], data, dlen);
	}

	pkt_commit_process(pkt, total_len);
}

static inline get_pkt_opt_len(uint8_t ts_cnt, arg_cnt)
{
	return sizeof(struct log_frontend_generic_short_pkt) + ts_cnt +
		(2 + arg_cnt) * sizeof(uint32_t) +
		(IS_ENABLED(CONFIG_LOG_FRONTEND_DICT_COBS) ? 2 : 0);
}


void log_frontend_simple_0(const void *source, uint32_t level, const char *fmt)
{
	static const uint32_t arg_cnt = 0;
	uint64_t ts;
	uint8_t *p_ts = (uint8_t *)&ts;
	uint8_t ts_code = get_ts(&ts);
	uint8_t ts_len = ts_code > 4 ? 8 : ts_code;
	size_t pkt_len = get_pkt_opt_len(ts_len, arg_cnt);
	size_t len32 = DIV_ROUND_UP(pkt_len, sizeof(uint32_t));
	uint32_t pkt_buf[len32];
	union log_frontend_pkt pkt = { .buf = pkt_buf };
	struct log_frontend_pkt_hdr hdr;
	uint8_t *data = &pkt.std->data;
	static const uint8_t package_wlen = 2 + arg_cnt;
	static const union cbprintf_package_hdr package_hdr = {
		.desc = { .len = package_wlen }
	};

	/* Add header */
	*(struct log_frontend_pkt_hdr *)data = hdr;
	data += sizeof(hdr);

	/* Add package header */
	*(uint32_t *)data = (uint32_t)(uintptr_t)package_hdr.raw;
	data += sizeof(uint32_t);

	*(uint32_t *)data = (uintptr_t)fmt;
	data += sizeof(uint32_t);

	/* Add timestamp */
	for (size_t i = 0; i < ts_len; i++) {
		*data++ = p_ts[i];
	}

	pkt_process(pkt, len32);
}

void log_frontend_simple_1(const void *source, uint32_t level, const char *fmt, uint32_t arg)
{
	static const uint32_t arg_cnt = 0;
	uint64_t ts;
	uint8_t *p_ts = (uint8_t *)&ts;
	uint8_t ts_code = get_ts(&ts);
	uint8_t ts_len = ts_code > 4 ? 8 : ts_code;
	size_t pkt_len = get_pkt_opt_len(ts_len, arg_cnt);
	size_t len32 = DIV_ROUND_UP(pkt_len, sizeof(uint32_t));
	uint32_t pkt_buf[len32];
	union log_frontend_pkt pkt = { .buf = pkt_buf };
	struct log_frontend_pkt_hdr hdr;
	uint8_t *data = &pkt.std->data;
	static const uint8_t package_wlen = 2 + arg_cnt;
	static const union cbprintf_package_hdr package_hdr = {
		.desc = { .len = package_wlen }
	};

	/* Add header */
	*(struct log_frontend_pkt_hdr *)data = hdr;
	data += sizeof(hdr);

	/* Add package header */
	*(uint32_t *)data = (uint32_t)(uintptr_t)package_hdr.raw;
	data += sizeof(uint32_t);

	*(uint32_t *)data = (uintptr_t)fmt;
	data += sizeof(uint32_t);

	*(uint32_t *)data = arg;
	data += sizeof(arg);

	/* Add timestamp */
	for (size_t i = 0; i < ts_len; i++) {
		*data++ = p_ts[i];
	}

	pkt_process(pkt, len32);
}

void log_frontend_simple_2(const void *source, uint32_t level,
			   const char *fmt, uint32_t arg0, uint32_t arg1)
{
	static const uint32_t arg_cnt = 0;
	uint64_t ts;
	uint8_t *p_ts = (uint8_t *)&ts;
	uint8_t ts_code = get_ts(&ts);
	uint8_t ts_len = ts_code > 4 ? 8 : ts_code;
	size_t pkt_len = get_pkt_opt_len(ts_len, arg_cnt);
	static const size_t len32 = DIV_ROUND_UP(pkt_len, sizeof(uint32_t));
	uint32_t pkt_buf[len32];
	union log_frontend_pkt pkt = { .buf = pkt_buf };
	struct log_frontend_pkt_hdr hdr;
	uint8_t *data = &pkt.std->data;
	static const uint8_t package_wlen = 2 + arg_cnt;
	static const union cbprintf_package_hdr package_hdr = {
		.desc = { .len = package_wlen }
	};

	/* Add header */
	*(struct log_frontend_pkt_hdr *)data = hdr;
	data += sizeof(hdr);

	/* Add package header */
	*(uint32_t *)data = (uint32_t)(uintptr_t)package_hdr.raw;
	data += sizeof(uint32_t);

	*(uint32_t *)data = (uint32_t)(uintptr_t)fmt;
	data += sizeof(uint32_t);

	*(uint32_t *)data = arg0;
	data += sizeof(arg1);

	*(uint32_t *)data = arg1;
	data += sizeof(arg1);

	/* Add timestamp. */
	for (size_t i = 0; i < ts_len; i++) {
		*data++ = p_ts[i];
	}

	pkt_process(pkt, len32);
}

void log_frontend_init(void)
{
	full_ts_cnt = 16;
	mpsc_pbuf_init(&buf, &config);
}

void log_frontend_panic(void)
{
	in_panic = true;
}

static int sync_init(void)
{
	if (!IS_ENABLED(CONFIG_LOG_FRONTEND_DICT_ASYNC)) {
		k_work_init(&sink_work, work_handler);
	}

	return log_frontend_dict_init();
}

SYS_INIT(sync_init, POST_KERNEL, 0);

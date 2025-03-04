/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ipc/shmpsc_pbuf.h>
#include <zephyr/cache.h>
LOG_MODULE_REGISTER(shmpsc, 1);

#define PADDING_MARK 0xFFFFFFFF

#define WLEN(len) DIV_ROUND_UP(len, sizeof(uint32_t))

#define MAX_LEN BIT_MASK(15)

void shmpsc_pbuf_init(struct shmpsc_pbuf *pb,
		      const struct shmpsc_pbuf_config *config)
{
	memset(pb, 0, sizeof(*pb));
	pb->buf = config->buf;
	pb->size = config->size / sizeof(uint32_t);
	pb->min_free_spc = pb->size - 1;
	pb->lock = config->lock;
	LOG_INF("init buf:%p size:%d (in words)", (void *)pb->buf, pb->size);

	sys_cache_data_flush_range((void *)pb, sizeof(*pb));
}

static uint32_t inc(uint32_t v, uint32_t i, uint32_t len)
{
	v += i;

	return (v >= len) ? (v - len) : v;
}

static uint32_t get_free_space(uint32_t wr_idx, uint32_t rd_idx, uint32_t size)
{
	if (rd_idx > wr_idx) {
		return rd_idx - wr_idx - 1;
	}

	uint32_t tail_spc = size - wr_idx  - 1;
	uint32_t head_spc = rd_idx - 1;

	return MAX(tail_spc, head_spc);
}

void *shmpsc_pbuf_alloc(struct shmpsc_pbuf *pb, size_t len)
{
	uint32_t req_space = WLEN(len) + 1;
	uint32_t tmp_idx, idx, rd_idx, pblen;
	void *ret = NULL;
	k_spinlock_key_t key;

	if (len >= MAX_LEN) {
		return NULL;
	}

	key = k_spin_lock(pb->lock);

	sys_cache_data_invd_range((void *)(&pb->rd_idx), sizeof(pb->rd_idx));
	rd_idx = pb->rd_idx;
	tmp_idx = pb->tmp_wr_idx;
	idx = pb->wr_idx;
	pblen = pb->size;

	if (rd_idx > tmp_idx) {
		if (req_space >= (rd_idx - tmp_idx)) {
			goto no_space;
		}
	} else {
		if (req_space >= (pblen - tmp_idx)) {
			if (req_space >= rd_idx) {
				goto no_space;
			} else {
				idx = inc(idx, pblen - tmp_idx, pblen);
				pb->buf[tmp_idx] = PADDING_MARK;

				sys_cache_data_flush_range((void *)&pb->buf[tmp_idx], sizeof(uint32_t));
				LOG_WRN("%p add padding:%d", pb, tmp_idx);
				tmp_idx = 0;
			}
		}
	}
	pb->buf[tmp_idx] = len;
	sys_cache_data_flush_range((void *)&pb->buf[tmp_idx], sizeof(uint32_t));
	ret = &pb->buf[tmp_idx + 1];
	tmp_idx = inc(tmp_idx, req_space, pblen);
	pb->tmp_wr_idx = tmp_idx;
	pb->wr_idx = idx;
	sys_cache_data_flush_range((void *)&pb->tmp_wr_idx, 2 * sizeof(uint32_t));
	if (ret) {
		LOG_INF("alloc %p len:%d(%d) wr:%d(%d) rd:%d",
			ret, len, req_space, idx, tmp_idx, rd_idx);
	}
no_space:
	k_spin_unlock(pb->lock, key);

	return ret;
}

void shmpsc_pbuf_commit(struct shmpsc_pbuf *pb, void *buf, uint32_t len)
{
	__ASSERT(len <= MAX_LEN, "Unexpected length:%d", len);
	uint32_t *buf32 = buf;
	uint32_t *len_loc = &buf32[-1];
	uint32_t alloc_len = *len_loc;
	uint32_t free_spc;
	uint32_t wr_idx;
	uint32_t rd_idx;
	uint32_t size;
	uint32_t min_free_spc;
	k_spinlock_key_t key;

       	key = k_spin_lock(pb->lock);
	wr_idx = pb->wr_idx;
	rd_idx = pb->rd_idx;
	size = pb->size;
	min_free_spc = pb->min_free_spc;
	if (len < alloc_len) {
		/* Committing less then requested. If no new packet is allocated
		 * after that packet then packet can be trimmed.
		 */
		uint32_t tmp_wr_idx = pb->tmp_wr_idx;

		if ((tmp_wr_idx - wr_idx) == (WLEN(alloc_len) + 1)) {
			alloc_len = len;
			tmp_wr_idx = inc(tmp_wr_idx, WLEN(alloc_len) + 1, size);
			pb->tmp_wr_idx = tmp_wr_idx;
		}
	}

	wr_idx = inc(wr_idx, WLEN(alloc_len) + 1, size);
	if (wr_idx > size) {
		LOG_ERR("wr_idx:%d len: %d, size:%d", wr_idx, len, size);
		k_panic();
	}
	free_spc = get_free_space(wr_idx, rd_idx, size);

	if (free_spc < min_free_spc) {
		pb->min_free_spc = free_spc;
	}
	/* Set bit 31 to indicate that buffer is ready. */
	*len_loc = BIT(31) | alloc_len | (len << 16);
	sys_cache_data_flush_range((void *)len_loc, len + sizeof(uint32_t));
	pb->wr_cnt++;
	pb->wr_idx = wr_idx;
	sys_cache_data_flush_range((void *)&pb->tmp_wr_idx, 2 * sizeof(uint32_t));

	LOG_INF("commit: %p len:%d len_loc:%p", buf, len, (void *)len_loc);
	k_spin_unlock(pb->lock, key);
}

void *shmpsc_pbuf_claim(struct shmpsc_pbuf *pb, uint32_t *len, uint32_t *cnt)
{
	sys_cache_data_invd_range((void *)&pb->wr_idx, sizeof(uint32_t));

	/* Counter is used to detect if the producer was reset. If it was reset then new
	 * counter value will be lower than the one stored on the consumer side.
	 * If reset occurred then cached read index is invalid.
	 */
	LOG_INF("claim %p", pb);
	if (cnt) {
		uint32_t wr_cnt = pb->wr_cnt;

		if ((int)(wr_cnt - *cnt) < 0) {
			sys_cache_data_invd_range((void *)&pb->rd_idx, sizeof(uint32_t));
		}
		*cnt = wr_cnt;
	}

	uint32_t *buf = pb->buf;
	uint32_t wr_idx = pb->wr_idx;
	uint32_t rd_idx = pb->rd_idx;
	uint32_t head;
	void *ret;

	if (wr_idx == rd_idx) {
		return NULL;
	}

	sys_cache_data_invd_range((void *)&pb->buf[rd_idx], sizeof(uint32_t));
	head = buf[rd_idx];

	if (head == PADDING_MARK) {
		LOG_INF("found padding: %d", rd_idx);
		rd_idx = 0;
		sys_cache_data_invd_range((void *)&buf[rd_idx], sizeof(uint32_t));
		head = buf[rd_idx];
		pb->rd_idx = 0;
	}

	if ((head & BIT(31)) == 0) {
		/* Head packet is not yet completed. */
		return NULL;
	}

	/* Head has ready bit at bit 31, packet length at bit 16 (15 bits) and
	 * allocated length at bit 0 (15 bits).
	 */
	*len = (head & ~BIT(31)) >> 16;
	sys_cache_data_invd_range((void *)&buf[rd_idx + 1], *len);
	ret = &buf[rd_idx + 1];

	LOG_INF("claim %p len:%d", (void *)ret, *len);
	return ret;
}

void shmpsc_pbuf_free(struct shmpsc_pbuf *pb, void *buf)
{
	uint32_t *buf32 = buf;
	uint32_t len = buf32[-1] & 0x7FFF;
	uint32_t rd_idx = pb->rd_idx;
	uint32_t size = pb->size;

	LOG_INF("free %d", len);
	rd_idx = inc(rd_idx, WLEN(len) + 1, size);
	pb->rd_idx = rd_idx;
	sys_cache_data_flush_range((void *)&pb->rd_idx, sizeof(pb->rd_idx));
}

void shmpsc_pbuf_get_utilization(struct shmpsc_pbuf *pb, uint32_t *min, uint32_t *now)
{
	sys_cache_data_invd_range((void *)pb, sizeof(*pb));
	*min = pb->min_free_spc;
	*now = get_free_space(pb->wr_idx, pb->rd_idx, pb->size) * sizeof(uint32_t);
}

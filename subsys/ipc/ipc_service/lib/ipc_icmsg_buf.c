/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <string.h>
#include <errno.h>
#include <cache.h>
#include <ipc/ipc_icmsg_buf.h>

struct icmsg_buf *icmsg_buf_init(void *buf, size_t blen)
{
	/* blen must be big enough to contain icmsg_buf struct, byte of data
	 * and message len (2 bytes).
	 */
	struct icmsg_buf *ib = buf;

	__ASSERT_NO_MSG(blen > (sizeof(*ib) + sizeof(uint16_t)));

	ib->len = blen - sizeof(*ib);
	ib->wr_idx = 0;
	ib->rd_idx = 0;

	__sync_synchronize();
	sys_cache_data_range(ib, sizeof(*ib), K_CACHE_WB);

	return ib;
}

int icmsg_buf_write(struct icmsg_buf *ib, const char *buf, uint16_t len)
{
	/* The length of buffer is immutable - avoid reloading that may happen
	 * due to memory bariers.
	 */
	const uint32_t iblen = ib->len;

	sys_cache_data_range(ib, sizeof(*ib), K_CACHE_INVD);
	__sync_synchronize();

	uint32_t wr_idx = ib->wr_idx;
	uint32_t rd_idx = ib->rd_idx;
	uint32_t space = len + sizeof(len); /* data + length field */

	if (wr_idx >= rd_idx) {
		uint32_t remaining = iblen - wr_idx;

		if (unlikely(space > iblen - 1)) {
			return -ENOMEM;
		} else if (remaining > space || (remaining == space && rd_idx > 0)) {
			/* Packet will fit */
		} else {
			if (rd_idx > space) {
				/* Packet will fit, padding must be added. */
				ib->data[wr_idx] = (uint8_t)0xFF;
				wr_idx = 0;
			} else {
				return -ENOMEM;
			}
		}
	} else if (rd_idx - wr_idx <= space) {
		return -ENOMEM;
	}

	ib->data[wr_idx] = (uint8_t)len;
	ib->data[wr_idx + 1] = (uint8_t)(len >> 8);
	memcpy(&ib->data[wr_idx + 2], buf, len);
	__sync_synchronize();
	sys_cache_data_range(&ib->data[wr_idx], space, K_CACHE_WB);

	wr_idx += 2 + len;
	if (wr_idx == iblen) {
		wr_idx = 0;
	}

	ib->wr_idx = wr_idx;
	__sync_synchronize();
	sys_cache_data_range(ib, sizeof(*ib), K_CACHE_WB);

	return len;
}

int icmsg_buf_read(struct icmsg_buf *ib, char *buf, uint16_t len)
{
	/* The length of buffer is immutable - avoid reloading. */
	const uint32_t iblen = ib->len;

	sys_cache_data_range(ib, sizeof(*ib), K_CACHE_INVD);
	__sync_synchronize();

	uint32_t rd_idx = ib->rd_idx;
	uint32_t wr_idx = ib->wr_idx;

	if (rd_idx == wr_idx) {
		/* The buffer is empty. */
		return 0;
	}

	sys_cache_data_range(&ib->data[rd_idx], sizeof(len), K_CACHE_INVD);
	__sync_synchronize();

	if (ib->data[rd_idx] == 0xFF) {
		/* Padding detected. */
		rd_idx = 0;
	}

	uint16_t mlen = ib->data[rd_idx] + ((uint16_t)ib->data[rd_idx + 1] << 8);

	if (!buf) {
		return mlen;
	}

	if (len < mlen) {
		return -EINVAL;
	}

	sys_cache_data_range(&ib->data[rd_idx + 2], sizeof(mlen), K_CACHE_INVD);
	memcpy(buf, &ib->data[rd_idx + 2], mlen);

	/* Update read index - make other side aware data was read. */
	rd_idx += 2 + mlen;
	if (rd_idx == iblen) {
		rd_idx = 0;
	}

	ib->rd_idx = rd_idx;

	__sync_synchronize();
	sys_cache_data_range(ib, sizeof(*ib), K_CACHE_WB);

	return mlen;
}

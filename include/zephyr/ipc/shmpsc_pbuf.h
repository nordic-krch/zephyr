/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/spinlock.h>

#ifndef ZEPHYR_INCLUDE_IPC_SHMPSC_PBUF_H_
#define ZEPHYR_INCLUDE_IPC_SHMPSC_PBUF_H_

struct shmpsc_pbuf {
	/** Read index. */
	uint32_t rd_idx;

	uint32_t padding2[7];

	/** Temporary write index. */
	uint32_t tmp_wr_idx;

	/** Write index. */
	uint32_t wr_idx;

	uint32_t wr_cnt;

	/* Minimum free space available. */
	uint32_t min_free_spc;

	/* Buffer. */
	uint32_t *buf;

	/* Buffer size in 32 bit words. */
	uint32_t size;

	/** Lock. */
	struct k_spinlock *lock;

	uint32_t padding[1];
};

BUILD_ASSERT(sizeof(struct shmpsc_pbuf) % 32 == 0);
BUILD_ASSERT(offsetof(struct shmpsc_pbuf, rd_idx) % 32 == 0);
BUILD_ASSERT(offsetof(struct shmpsc_pbuf, tmp_wr_idx) % 32 == 0);

/** @brief MPSC packet buffer configuration. */
struct shmpsc_pbuf_config {
	/* Pointer to a memory used for storing packets. */
	uint32_t *buf;

	/* Buffer size in 32 bit words. */
	uint32_t size;

	/** Lock. */
	struct k_spinlock *lock;
};

void shmpsc_pbuf_init(struct shmpsc_pbuf *buffer, const struct shmpsc_pbuf_config *config);

void *shmpsc_pbuf_alloc(struct shmpsc_pbuf *buffer, uint32_t len);
void shmpsc_pbuf_commit(struct shmpsc_pbuf *buffer, void *buf, uint32_t len);

void *shmpsc_pbuf_claim(struct shmpsc_pbuf *buffer, uint32_t *len, uint32_t *cnt);
void shmpsc_pbuf_free(struct shmpsc_pbuf *buffer, void *buf);

void shmpsc_pbuf_get_utilization(struct shmpsc_pbuf *buffer, uint32_t *size, uint32_t *now);

#endif /* ZEPHYR_INCLUDE_IPC_SHMPSC_PBUF_H_ */

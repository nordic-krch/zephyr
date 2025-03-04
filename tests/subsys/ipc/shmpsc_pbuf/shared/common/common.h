/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __COMMON_H__
#define __COMMON_H__

#include <zephyr/ipc/shmpsc_pbuf.h>

#define LEN_BITS (16 - MPSC_PBUF_HDR_BITS)
#define TYPE_BITS 3
#define RSP_CNT_BITS (16 - TYPE_BITS)

#define PKT_TYPE_TEST_START 0
#define PKT_TYPE_TEST_END 1
#define PKT_TYPE_TEST_PKT 2

struct test_hdr {
	uint32_t type: 4;
	uint32_t rsp:  1;
	uint32_t data: 27;
};

struct test_data {
	struct test_hdr hdr;
	uint8_t data[];
};

union test_item {
	struct test_data data;
	struct test_hdr generic;
};

struct shmpsc_pbuf *buffer_init(struct k_spinlock *lock, uint32_t *buf,
				       uint32_t size, bool tx);

int sync(void);

#endif /* __COMMON_H__ */

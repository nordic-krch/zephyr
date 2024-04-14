/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DMA_NRF_MVDMA_H_
#define ZEPHYR_INCLUDE_DRIVERS_DMA_NRF_MVDMA_H_

#include <stddef.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NRF_MVDMA_ATTR_BYTE_SWAP 0
#define NRF_MVDMA_ATTR_JOB_LINK 1
#define NRF_MVDMA_ATTR_ZERO_FILL 2
#define NRF_MVDMA_ATTR_FIXED_PARAMS 3
#define NRF_MVDMA_ATTR_FIXED_ADDR 4
#define NRF_MVDMA_ATTR_SHORT_BURSTS 5
#define NRF_MVDMA_ATTR_DEFAULT 7

#define NRF_MVDMA_EXT_ATTR_PERIPH 1
#define NRF_MVDMA_EXT_ATTR_INT 2


typedef void (*nrf_mvdma_handler_t)(void *user_data);

#define NRF_MVDMA_JOB_DESC(_ptr, _size, _attr, _ext_attr) \
	(uint32_t)_ptr, \
	(_size & 0xFFFFFF) | (_attr << 24) | (_ext_attr << 30)

#define NRF_MVDMA_JOB_TERMINATE 0

struct nrf_mvdma_jobs_desc {
	const uint32_t*source;
	size_t source_desc_size;
	const uint32_t *sink;
	size_t sink_desc_size;
	nrf_mvdma_handler_t handler;
	void *user_data;
};

int nrf_mvdma_xfer(const struct nrf_mvdma_jobs_desc *jobs_desc);


#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_DMA_NRF_MVDMA_H_ */


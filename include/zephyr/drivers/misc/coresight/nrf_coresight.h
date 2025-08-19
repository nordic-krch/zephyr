/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _ZEPHYR_DRIVERS_MISC_CORESIGHT_NRF_CORESIGHT_H_
#define _ZEPHYR_DRIVERS_MISC_CORESIGHT_NRF_CORESIGHT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>


int nrf_coresight_init_etr(uintptr_t buf, size_t buf_word_len);
int nrf_coresight_init_tpiu(void);

#ifdef __cplusplus
}
#endif

#endif /* _ZEPHYR_DRIVERS_MISC_CORESIGHT_NRF_CORESIGHT_H_ */

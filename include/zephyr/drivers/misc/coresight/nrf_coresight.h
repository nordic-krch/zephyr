/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _ZEPHYR_DRIVERS_MISC_CORESIGHT_NRF_CORESIGHT_H_
#define _ZEPHYR_DRIVERS_MISC_CORESIGHT_NRF_CORESIGHT_H_

#ifdef __cplusplus
extern "C" {
#endif

enum nrf_coresight_trace_mode {
    NRF_CORESIGHT_MODE_STM_TPIU,
};

/**
 * @brief Initialize the CoreSight trace subsystem.
 *
 * @param mode The trace mode to initialize.
 * @return int 0 on success, negative error code on failure.
 */
int nrf_coresight_init(enum nrf_coresight_trace_mode mode);

#ifdef __cplusplus
}
#endif

#endif /* _ZEPHYR_DRIVERS_MISC_CORESIGHT_NRF_CORESIGHT_H_ */

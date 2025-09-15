/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_NORDIC_IRONSIDE_INCLUDE_NRF_IRONSIDE_KRCH_H_
#define ZEPHYR_SOC_NORDIC_IRONSIDE_INCLUDE_NRF_IRONSIDE_KRCH_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>

/**
 * @defgroup nrf_ironside_krch IronSide KRCH Service
 * @brief Service for KRCH arbitrary memory writes during development
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

// IRONSIDE_CALL_ID_KRCH_SERVICE_V0 is now defined in the server-side implementation

/**
 * @brief Write to arbitrary memory address.
 *
 * @param address The target address for the write operation.
 * @param value The 32-bit value to write.
 * @retval 0 on success.
 * @retval Negative errno value on failure.
 */
int ironside_krch_memory_write(uint32_t address, uint32_t value);
int ironside_krch_memory_read(uint32_t address, uint32_t *value);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SOC_NORDIC_IRONSIDE_INCLUDE_NRF_IRONSIDE_KRCH_H_ */

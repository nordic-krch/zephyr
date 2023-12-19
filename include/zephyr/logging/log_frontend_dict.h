/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_LOGGING_LOG_FRONTEND_DICT_H_
#define ZEPHYR_INCLUDE_LOGGING_LOG_FRONTEND_DICT_H_

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int log_frontend_dict_init(void);
extern int log_frontend_dict_tx_blocking(const uint8_t *buf, size_t len, bool panic);
extern int log_frontend_dict_tx_async(const uint8_t *buf, size_t len);

void log_frontend_dict_tx_from_cb(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_LOGGING_LOG_FRONTEND_DICT_H_ */

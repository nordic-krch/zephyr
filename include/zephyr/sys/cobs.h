/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SYS_COBS_H_
#define ZEPHYR_INCLUDE_SYS_COBS_H_

#include <zephyr/kernel.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup cobs_api Constant Overhead Byte Stuffing (COBS)
 * @ingroup kernel_apis
 * @{
 */

/**
 * @brief Encode using COBS/R and 0xFF delimiter.
 *
 * In order to allow in-place encoding input buffer must provide space up front
 * thus %p off should be positive. Offset shall be at minimum equal to 1 + length / 253.
 *
 * Encoder is using 0xFF as delimiter. Not like typical examples where 0x00 is used.
 * 0xFF is chosen to speed up the encoding since 0xFF is less common than 0x00 so
 * less memory writes is expected. Additionally, there is an optimized algorithm
 * for detecting 0xFF presence in 32 bit word.
 *
 * Delimiter is not appended to the packet.
 *
 * Implementation is optimized for short packets (<253 bytes).
 *
 * @param data Pointer to the buffer where encoded packet is written.
 * @param length Amount of input data in the buffer.
 * @param off Location where data to be encoded starts.
 *
 * @return Encoded packet length.
 */
int cobs_r_encode(uint8_t *data, size_t length, size_t off);

/**
 * @brief Decode using COBS/R and 0xFF delimiter.
 *
 * Decoding is not optimized as it was added only for test purposes.
 * Decoding is done in-place. Decoded packet starts from %p data.
 *
 * @param data Encoded packet.
 * @param length Packet length.
 *
 * @return Length of decoded packet.
 */
int cobs_r_decode(uint8_t *data, size_t length);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_COBS_H_ */

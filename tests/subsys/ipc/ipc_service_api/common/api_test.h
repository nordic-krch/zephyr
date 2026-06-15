/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef API_TEST_H_
#define API_TEST_H_

#include <stdint.h>

#define API_TEST_MSG_DATA_SIZE 16

enum api_test_cmd {
	API_TEST_CMD_PING = 1,
	API_TEST_CMD_PONG,
	API_TEST_CMD_ECHO,
	API_TEST_CMD_ECHO_RSP,
	API_TEST_CMD_PUSH,
	API_TEST_CMD_HOLD_RX,
	API_TEST_CMD_HOLD_RX_RSP,
	API_TEST_CMD_CLOSE_AFTER_UNBOUND,
	API_TEST_CMD_DEREGISTER,
};

struct api_test_msg {
	uint32_t cmd;
	uint8_t data[API_TEST_MSG_DATA_SIZE];
};

#endif /* API_TEST_H_ */

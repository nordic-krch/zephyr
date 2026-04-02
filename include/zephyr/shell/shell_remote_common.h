/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SHELL_SHELL_REMOTE_COMMON_H_
#define ZEPHYR_INCLUDE_SHELL_SHELL_REMOTE_COMMON_H_

#include <zephyr/shell/shell.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHELL_REMOTE_MSG_PRINT 1
#define SHELL_REMOTE_MSG_CMD_GET 2
#define SHELL_REMOTE_MSG_CMD 3
#define SHELL_REMOTE_MSG_CMD_FAILED 4
#define SHELL_REMOTE_MSG_EXEC 5
#define SHELL_REMOTE_MSG_RESULT 6

struct shell_remote_msg_generic {
	uint8_t id;
};

struct shell_remote_msg_print {
	uint8_t id;
	uint8_t color;
	uint8_t data[0];
};

struct shell_remote_msg_exec {
	uint8_t id;
	uint8_t argc;
	uint8_t cmd_lvl;
	shell_cmd_handler handler;
	uint8_t data[0];
};

struct shell_remote_msg_result {
	uint8_t id;
	int result;
};

struct shell_remote_msg_cmd_get {
	uint8_t id;
	const struct shell_static_entry *parent;
	size_t idx;
};

struct shell_remote_msg_cmd {
	uint8_t id;
	const union shell_cmd_entry *subcmd;
	const struct shell_static_entry *entry;
	shell_cmd_handler handler;
	struct shell_static_args args;
	char data[0]; /* syntax followed by optional help. */
};

union shell_remote_msg {
	struct shell_remote_msg_generic	*generic;
	struct shell_remote_msg_print *print;
	struct shell_remote_msg_cmd *cmd;
	struct shell_remote_msg_cmd_get *cmd_get;
	struct shell_remote_msg_cmd_failed *cmd_failed;
	struct shell_remote_msg_exec *exec;
	struct shell_remote_msg_result *result;
};

void shell_remote_cmd_process(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SHELL_SHELL_REMOTE_COMMON_H_ */

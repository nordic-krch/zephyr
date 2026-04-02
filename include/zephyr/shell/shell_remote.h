/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SHELL_SHELL_REMOTE_H_
#define ZEPHYR_INCLUDE_SHELL_SHELL_REMOTE_H_

#include "zephyr/shell/shell_remote_common.h"
#include <zephyr/shell/shell.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/sys/slist.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_SHELL_REMOTE_TMP_BUF_SIZE
#define CONFIG_SHELL_REMOTE_TMP_BUF_SIZE 128
#endif

struct shell_remote_cmd {
	struct shell_static_entry cmd;
	const struct shell_static_entry *rem_cmd;
	char tmp_buf[CONFIG_SHELL_REMOTE_TMP_BUF_SIZE];
};

struct shell_remote_data {
	struct ipc_ept ept;
	struct k_sem sem;
	struct k_work work;
	struct k_spinlock lock;
	int result;
	size_t len;
	void *msg;
	struct shell_remote_cmd cmds[2];
	struct shell_remote_cmd *current_cmd;
	const struct shell *sh;
	sys_slist_t print_list;
};

struct shell_remote {
	const char *name;
	const struct device *ipc;
	struct ipc_ept_cfg ep_cfg;
};

struct shell_remote_print{
	sys_snode_t node;
	uint32_t padding;
	struct shell_remote_msg_print msg;
};

#define SHELL_REMOTE_CONN(_name, _ep_name, ipc_node) \
	static struct shell_remote_data shell_remote_data_##_name;\
	static const STRUCT_SECTION_ITERABLE(shell_remote, _name) = { \
		.name = STRINGIFY(_name), \
		.ipc = DEVICE_DT_GET(ipc_node), \
		.ep_cfg = { \
			.name = STRINGIFY(_ep_name), \
			.cb = { \
				.bound = shell_remote_ep_bound, \
				.received = shell_remote_ep_recv, \
			}, \
			.priv = &shell_remote_data_##_name \
		} \
	}

const struct shell_static_entry *z_shell_remote_cmd_get(
					const struct shell_static_entry *parent,
					size_t idx,
					struct shell_static_entry *dloc);

int z_shell_remote_cmd_exec(const struct shell *shell, const struct shell_static_entry *cmd,
		uint8_t argc, const char **argv, size_t cmd_lvl);

void shell_remote_ep_bound(void *priv);
void shell_remote_ep_recv(const void *data, size_t len, void *priv);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SHELL_SHELL_REMOTE_H_ */

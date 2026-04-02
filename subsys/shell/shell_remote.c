/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/sys/__assert.h"
#include <zephyr/logging/log.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <zephyr/shell/shell_remote_common.h>
#include <zephyr/shell/shell_remote.h>
LOG_MODULE_REGISTER(shell_remote, 2);

/* Take a print message, make a copy on heap and add it to the list which is processed
 * with the work queue.
 */
static void msg_print_handle(struct shell_remote_data *remote_data,
			      struct shell_remote_msg_print *msg, size_t len)
{
	size_t buf_len = len + offsetof(struct shell_remote_print, msg);
	struct shell_remote_print *buf = aligned_alloc(8, buf_len);

	if (buf == NULL) {
		LOG_ERR("Failed to allocate message with remote shell printing");
		return;
	}

	memcpy(&buf->msg, msg, len);

	k_spinlock_key_t key = k_spin_lock(&remote_data->lock);
	sys_slist_append(&remote_data->print_list, &buf->node);
	k_spin_unlock(&remote_data->lock, key);

	k_work_submit(&remote_data->work);
}

static void msg_cmd_handle(struct shell_remote_data *shremote,
			   const struct shell_remote_msg_cmd *msg, size_t len)
{
	#define TRUNCATED_MSG "..."
	bool truncated = false;

	shremote->result = 0;
	shremote->current_cmd->rem_cmd = msg->entry;
	shremote->current_cmd->cmd.subcmd = msg->subcmd;
	shremote->current_cmd->cmd.handler = msg->handler;
	shremote->current_cmd->cmd.args.mandatory = msg->args.mandatory;
	shremote->current_cmd->cmd.args.optional = msg->args.optional;
	shremote->current_cmd->cmd.args.ipc_cmd = SHELL_CMD_FLAG_REMOTE_SUBCMD;

	size_t strings_len = len - offsetof(struct shell_remote_msg_cmd, data);
	size_t syntax_len = strlen(msg->data);

	if (strings_len > CONFIG_SHELL_REMOTE_TMP_BUF_SIZE) {
		truncated = true;
		strings_len = CONFIG_SHELL_REMOTE_TMP_BUF_SIZE;
	}
	memcpy(shremote->current_cmd->tmp_buf, msg->data, strings_len);
	shremote->current_cmd->cmd.syntax = shremote->current_cmd->tmp_buf;
	LOG_INF("len:%d curr:%p cmd get syntax: %s", strings_len, shremote->current_cmd, shremote->current_cmd->cmd.syntax);
	if (truncated) {
		/* Truncate help message */
		LOG_ERR("truncated");
		memcpy(&shremote->current_cmd->tmp_buf[
				CONFIG_SHELL_REMOTE_TMP_BUF_SIZE - sizeof(TRUNCATED_MSG)],
			TRUNCATED_MSG, sizeof(TRUNCATED_MSG));
	}
	shremote->current_cmd->cmd.help = &shremote->current_cmd->tmp_buf[syntax_len + 1];
	LOG_INF("curr:%p", shremote->current_cmd);
	k_sem_give(&shremote->sem);
}

static void msg_result_handle(struct shell_remote_data *shremote,
			      struct shell_remote_msg_result *msg)
{
	/* Store result and signal to shell thread. */
	shremote->result = msg->result;
	k_sem_give(&shremote->sem);
}

void shell_remote_ep_recv(const void *data, size_t len, void *priv)
{
	struct shell_remote_data *shremote = priv;
	union shell_remote_msg msg;

	msg.generic = (struct shell_remote_msg_generic *)data;

	switch (msg.generic->id) {
	case SHELL_REMOTE_MSG_CMD:
		msg_cmd_handle(shremote, msg.cmd, len);
		break;
	case SHELL_REMOTE_MSG_PRINT:
		msg_print_handle(shremote, msg.print, len);
		break;
	case SHELL_REMOTE_MSG_RESULT:
		msg_result_handle(shremote, msg.result);
		break;
	default:
		__ASSERT(0, "Unexpected message:%d", msg.generic->id);
	}
}

static const struct shell_remote *get_remote(size_t id)
{
	const struct shell_remote *shremote;
	size_t cnt;

	STRUCT_SECTION_COUNT(shell_remote, &cnt);
	if (id >= cnt) {
		return NULL;
	}

       	STRUCT_SECTION_GET(shell_remote, id, &shremote);

	return shremote;
}

static struct shell_remote_data *get_remote_data(size_t id)
{
	const struct shell_remote *shremote = get_remote(id);

	return shremote ? (struct shell_remote_data *)shremote->ep_cfg.priv : NULL;
}

const struct shell_static_entry *z_shell_remote_cmd_get(
					const struct shell_static_entry *parent,
					size_t idx,
					struct shell_static_entry *dloc)
{
	uint32_t ipc_id = parent->args.ipc_id;
	struct shell_remote_data *shremote = get_remote_data(ipc_id);
	const struct shell_static_entry *rem_parent;
	int err;

	LOG_INF("shremote %p current: %p, cmd0:%p, cmd1:%p", shremote,
		shremote->current_cmd, &shremote->cmds[0], &shremote->cmds[1]);

	LOG_INF("parent: %p, syntax: %s, shremote->cmds[0].cmd.syntax: %s, "
		"shremote->cmds[1].cmd.syntax: %s",
		(void *)parent, parent->syntax ? parent->syntax : "NULL",
		shremote->cmds[0].cmd.syntax ? shremote->cmds[0].cmd.syntax : "NULL",
		shremote->cmds[1].cmd.syntax ? shremote->cmds[1].cmd.syntax : "NULL");
	if (parent->args.ipc_cmd == SHELL_CMD_FLAG_REMOTE_ROOT) {
		rem_parent = NULL;
		shremote->current_cmd = &shremote->cmds[0];
	} else if (strcmp(parent->syntax, shremote->cmds[0].cmd.syntax) == 0) {
		rem_parent = shremote->cmds[0].rem_cmd;
		shremote->current_cmd = &shremote->cmds[1];
	} else if (strcmp(parent->syntax, shremote->cmds[1].cmd.syntax) == 0) {
		rem_parent = shremote->cmds[1].rem_cmd;
		shremote->current_cmd = &shremote->cmds[0];
	} else {
		LOG_ERR("Unexpected parent %p with syntax:%s", parent, parent->syntax);
		return NULL;
	}

	/* Inherit IPC ID for the current command. */
	shremote->current_cmd->cmd.args.ipc_id = ipc_id;

	LOG_DBG("Command get parent:%s idx:%d", parent->syntax, idx);
	struct shell_remote_msg_cmd_get cmd_get = {
		.id = SHELL_REMOTE_MSG_CMD_GET,
		.parent = rem_parent,
		.idx = idx
	};

	err = ipc_service_send(&shremote->ept, (const void *)&cmd_get, sizeof(cmd_get));
	if (err < 0) {
		LOG_ERR("Failed to send result");
	}

	err = k_sem_take(&shremote->sem, K_MSEC(1000));
	if (err < 0) {
		LOG_ERR("Failed to get command get response");
		return NULL;
	}

	if (shremote->result != 0) {
		if (shremote->result == -ENODEV) {
			return NULL;
		}

		if (shremote->result == -ENOEXEC) {
			dloc->syntax = "";
			dloc->subcmd = NULL;
			dloc->handler = NULL;
			dloc->help = NULL;
			return dloc;
		}
		__ASSERT_NO_MSG(0);
		return NULL;
	}

	return &shremote->current_cmd->cmd;
}

int z_shell_remote_cmd_exec(const struct shell *shell, const struct shell_static_entry *cmd,
		uint8_t argc, const char **argv, size_t cmd_lvl)
{
	uint32_t ipc_id = cmd->args.ipc_id;
	struct shell_remote_data *shremote = get_remote_data(ipc_id);
	struct shell_remote_msg_exec *msg;
	uint8_t *tmp_buf = shremote->cmds[0].tmp_buf;
	uint8_t skip_argc = 2;
	int err;
	size_t msg_len;
	uint8_t *arg_buf;
	size_t slen[argc];

	shremote->sh = shell;
	msg_len = offsetof(struct shell_remote_msg_exec, data);
	/* Skip first two commands (remote_shell root command and remote name) */
	for (uint8_t i = skip_argc; i < argc; i++) {
		slen[i] = strlen(argv[i]);
		msg_len += slen[i] + 1;
	}

	if (msg_len > CONFIG_SHELL_REMOTE_TMP_BUF_SIZE) {
		return -ENOMEM;
	}

	msg = (struct shell_remote_msg_exec *)tmp_buf;
	arg_buf = msg->data;
	msg->id = SHELL_REMOTE_MSG_EXEC;
	msg->argc = argc - skip_argc;
	msg->cmd_lvl = cmd_lvl - skip_argc;
	msg->handler = cmd->handler;

	for (uint8_t i = skip_argc; i < argc; i++) {
		memcpy(arg_buf, argv[i], slen[i]);
		arg_buf[slen[i]] = '\0';
		arg_buf += slen[i] + 1;
	}

	err = ipc_service_send(&shremote->ept, tmp_buf, msg_len);
	if (err < 0) {
		LOG_ERR("Failed to send exec command");
		return err;
	}

	err = k_sem_take(&shremote->sem, K_MSEC(1000));
	if (err < 0) {
		LOG_ERR("Failed to get command get response");
		return -EIO;
	}

	return shremote->result;
}

static void ack_result(struct shell_remote_data *shremote)
{
	static const struct shell_remote_msg_result rsp = {
		.id = SHELL_REMOTE_MSG_RESULT,
		.result = 0
	};
	int err;

	err = ipc_service_send(&shremote->ept, (const void *)&rsp, sizeof(rsp));
	if (err < 0) {
		LOG_ERR("Failed to send result");
	}
}

static void print_process(struct k_work *work)
{
	struct shell_remote_data *shremote = CONTAINER_OF(work, struct shell_remote_data, work);
	k_spinlock_key_t key;
	sys_snode_t *node;

	key = k_spin_lock(&shremote->lock);
	node = sys_slist_get(&shremote->print_list);
	k_spin_unlock(&shremote->lock, key);

	if (node != NULL) {
		void *data;
		struct shell_remote_print *print_msg;

		print_msg = CONTAINER_OF(node, struct shell_remote_print, node);
		data = (void *)ROUND_UP((uintptr_t)print_msg->msg.data, sizeof(uint64_t));
		shell_cbpprintf(shremote->sh, print_msg->msg.color, data);
		free(print_msg);

		ack_result(shremote);
	}

	if (sys_slist_is_empty(&shremote->print_list) == false) {
		k_work_submit(work);
	}
}

void shell_remote_ep_bound(void *priv)
{
	struct shell_remote_data *remote_data = priv;

	k_sem_give(&remote_data->sem);
}

static int shell_remote_init(void)
{
	int ret;

	STRUCT_SECTION_FOREACH(shell_remote, remote) {
		struct shell_remote_data *data = remote->ep_cfg.priv;

		k_sem_init(&data->sem, 0, 1);

		ret = ipc_service_open_instance(remote->ipc);
		if ((ret < 0) && (ret != -EALREADY)) {
			LOG_ERR("ipc_service_open_instance() failure (remote:%s)", remote->name);
			return ret;
		}

		ret = ipc_service_register_endpoint(remote->ipc, &data->ept, &remote->ep_cfg);
		if (ret < 0) {
			LOG_ERR("ipc_service_register_endpoint() failure (remote: %s)", remote->name);
			return ret;
		}

		ret = k_sem_take(&data->sem, K_MSEC(1000));
		if (ret < 0) {
			LOG_ERR("Failed to bound to the IPC endpoint");
			return ret;
		}

		k_work_init(&data->work, print_process);

		LOG_INF("shell remote communication established (remote: %s).", remote->name);
	}

	return 0;
}

SYS_INIT(shell_remote_init, APPLICATION, 0);

static void dynamic_cmd_get(size_t idx, struct shell_static_entry *entry)
{
	const struct shell_remote *remote = get_remote(idx);

	if (remote == NULL) {
		entry->syntax = NULL;
		return;
	}

	entry->syntax = remote->name;
	entry->handler  = NULL;
	entry->subcmd = NULL;
	entry->help = NULL;
	entry->args.ipc_cmd = SHELL_CMD_FLAG_REMOTE_ROOT;
	entry->args.ipc_id = idx;
}

SHELL_DYNAMIC_CMD_CREATE(sub_remote_shell, dynamic_cmd_get);

SHELL_CMD_REGISTER(remote_shell, &sub_remote_shell, "Execute commands on remote shell", NULL);

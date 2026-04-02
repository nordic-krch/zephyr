/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(remote, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("shell rpc sample");
	return 0;
}

static int cmd_comment(const struct shell *sh, size_t argc, char **argv)
{
	shell_fprintf(sh, SHELL_NORMAL, "command test1 %d\n", 100);
	return 0;
}

static int cmd_ala(const struct shell *sh, size_t argc, char **argv)
{
	shell_fprintf(sh, SHELL_NORMAL, "command ala %d\n", 100);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(test_cmd_sub,
	SHELL_CMD(test1, NULL, "Logger backends commands.", cmd_comment),
	SHELL_COND_CMD_ARG(1, ala, NULL, "Logger backends commands.", cmd_ala, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(test_cmd, &test_cmd_sub, "test command", NULL);


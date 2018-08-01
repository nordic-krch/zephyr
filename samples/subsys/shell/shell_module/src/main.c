/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <misc/printk.h>
#include <shell/cli.h>
#include <shell/shell_uart.h>
#include <version.h>
#include <logging/log.h>

SHELL_UART_DEFINE(shell_transport_uart);
SHELL_DEFINE(uart_shell, "uart:~$ ", &shell_transport_uart, '\r', 10);

static void cmd_demo_ping(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_fprintf(shell, SHELL_NORMAL, "pong\r\n");
}

static void cmd_demo_params(const struct shell *shell, size_t argc, char **argv)
{
	int cnt;

	shell_fprintf(shell, SHELL_NORMAL, "argc = %d\r\n", argc);
	for (cnt = 0; cnt < argc; cnt++) {
		shell_fprintf(shell, SHELL_NORMAL,
				"  argv[%d] = %s\r\n", cnt, argv[cnt]);
	}
}

static void cmd_version(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_fprintf(shell, SHELL_NORMAL,
		      "Zephyr version %s\r\n", KERNEL_VERSION_STRING);
}

SHELL_CREATE_STATIC_SUBCMD_SET(sub_demo)
{
	/* Alphabetically sorted. */
	SHELL_CMD(params, NULL, "Print params command.", cmd_demo_params),
	SHELL_CMD(ping, NULL, "Ping command.", cmd_demo_ping),
	SHELL_SUBCMD_SET_END /* Array terminated. */
};

SHELL_CMD_REGISTER(demo, &sub_demo, "Demo commands", cmd_version);

SHELL_CMD_REGISTER(version, NULL, "Show kernel version", cmd_version);


void main(void)
{
	(void)shell_init(&uart_shell, NULL, true, true, LOG_LEVEL_INF);
}

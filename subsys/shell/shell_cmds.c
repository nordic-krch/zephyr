/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <shell/shell.h>
#include "shell_utils.h"
#include "shell_ops.h"
#include "shell_vt100.h"

#define SHELL_HELP_CLEAR		"Clear screen."
#define SHELL_HELP_COLORS		"Toggle colored syntax."
#define SHELL_HELP_COLORS_OFF		"Disable colored syntax."
#define SHELL_HELP_COLORS_ON		"Enable colored syntax."
#define SHELL_HELP_STATISTICS		"CLI statistics."
#define SHELL_HELP_STATISTICS_SHOW	\
	"Get CLI statistics for the Logger module."
#define SHELL_HELP_STATISTICS_RESET	\
	"Reset CLI statistics for the Logger module."
#define SHELL_HELP_RESIZE						\
	"Console gets terminal screen size or assumes 80 in case "	\
	"the readout fails. It must be executed after each terminal "	\
	"width change to ensure correct text display."
#define SHELL_HELP_RESIZE_DEFAULT				\
	"Assume 80 chars screen width and send this setting "	\
	"to the terminal."
#define SHELL_HELP_HISTORY	"Command history."
#define SHELL_HELP_ECHO		"Toggle CLI echo."
#define SHELL_HELP_ECHO_ON	"Enable CLI echo."
#define SHELL_HELP_ECHO_OFF	\
	"Disable CLI echo. Arrows and buttons: Backspace, Delete, End, Home, " \
	"Insert are not handled."
#define SHELL_HELP_CLI		"Useful, not Unix-like CLI commands."

#define SHELL_MSG_UNKNOWN_PARAMETER	" unknown parameter: "

#define SHELL_MAX_TERMINAL_SIZE		(250u)

/* 10 == {esc, [, 2, 5, 0, ;, 2, 5, 0, '\0'} */
#define SHELL_CURSOR_POSITION_BUFFER	(10u)

/* Function reads cursor position from terminal. */
static int cursor_position_get(const struct shell *shell)
{
	size_t cnt;
	u16_t x = 0; /* horizontal position */
	u16_t y = 0; /* vertical position */
	char c = 0;
	u16_t buff_idx = 0;

	/* clear temp buffer */
	memset(shell->ctx->temp_buff, 0, sizeof(shell->ctx->temp_buff));

	/* escape code asking terminal about its size */
	static char const cmd_get_terminal_size[] = "\033[6n";

	shell_raw_fprintf(shell->fprintf_ctx, cmd_get_terminal_size);

	/* fprintf buffer needs to be flushed to start sending prepared
	 * escape code to the terminal.
	 */
	shell_fprintf_buffer_flush(shell->fprintf_ctx);

	/* timeout for terminal response = ~1s */
	for (u16_t i = 0; i < 1000; i++)
	{
		do
		{
			(void)shell->iface->api->read(shell->iface, &c,
						      sizeof(c), &cnt);
			if (cnt == 0) {
				k_sleep(1);
				continue;
			}
			if ((c != SHELL_VT100_ASCII_ESC) &&
			    (shell->ctx->temp_buff[0] != SHELL_VT100_ASCII_ESC))
			{
				continue;
			}

			if (c == 'R') /* end of response from the terminal */
			{
				shell->ctx->temp_buff[buff_idx] = '\0';
				if (shell->ctx->temp_buff[1] != '[') {
					shell->ctx->temp_buff[0] = 0;
					return -EIO;
				}

				/* Index start position in the buffer where 'y'
				 * is stored.
				 */
				buff_idx = 2;

				while (shell->ctx->temp_buff[buff_idx] != ';') {
					y = y * 10 +
					(shell->ctx->temp_buff[buff_idx++] -
									  '0');
					if (buff_idx >=
						CONFIG_SHELL_CMD_BUFF_SIZE) {
						return -EMSGSIZE;
					}
				}

				if (++buff_idx >= CONFIG_SHELL_CMD_BUFF_SIZE) {
					return -EIO;
				}

				while (shell->ctx->temp_buff[buff_idx]
							     != '\0') {
					x = x * 10 +
					(shell->ctx->temp_buff[buff_idx++] -
									   '0');

					if (buff_idx >=
						CONFIG_SHELL_CMD_BUFF_SIZE) {
						return -EMSGSIZE;
					}
				}
				/* horizontal cursor position */
				if (x > SHELL_MAX_TERMINAL_SIZE) {
					shell->ctx->vt100_ctx.cons.cur_x =
						SHELL_MAX_TERMINAL_SIZE;
				} else {
					shell->ctx->vt100_ctx.cons.cur_x = x;
				}
				/* vertical cursor position */
				if (y > SHELL_MAX_TERMINAL_SIZE) {
					shell->ctx->vt100_ctx.cons.cur_y =
						SHELL_MAX_TERMINAL_SIZE;
				} else {
					shell->ctx->vt100_ctx.cons.cur_y = y;
				}

				shell->ctx->temp_buff[0] = 0;

				return 0;
			} else {
				shell->ctx->temp_buff[buff_idx] = c;
			}

			if (++buff_idx > SHELL_CURSOR_POSITION_BUFFER - 1) {
				shell->ctx->temp_buff[0] = 0;
				/* data_buf[SHELL_CURSOR_POSITION_BUFFER - 1]
				 * is reserved for '\0' */
				return -ENOMEM;
			}

		} while (cnt > 0);
	}

	return -ETIMEDOUT;
}

/* Function gets terminal width and height. */
static int terminal_size_get(const struct shell * shell, u16_t * p_length,
			     u16_t * p_height)
{
	assert(p_length);
	assert(p_height);

	u16_t x;
	u16_t y;

	if (cursor_position_get(shell) == 0) {
		x = shell->ctx->vt100_ctx.cons.cur_x;
		y = shell->ctx->vt100_ctx.cons.cur_y;

		/* Assumption: terminal width and height < 999. */
		/* Move to last column. */
		shell_op_cursor_vert_move(shell, SHELL_MAX_TERMINAL_SIZE);

		/* Move to last row. */
		shell_op_cursor_horiz_move(shell, -SHELL_MAX_TERMINAL_SIZE);
	} else {
		return -ENOTSUP;
	}

	if (cursor_position_get(shell) == 0) {
		*p_length = shell->ctx->vt100_ctx.cons.cur_x;
		*p_height = shell->ctx->vt100_ctx.cons.cur_y;
		shell_op_cursor_vert_move(shell, x - *p_length);
		shell_op_cursor_horiz_move(shell, *p_height - y);

		return 0;
	}

	return -ENOTSUP;
}

static void cmd_clear(const struct shell *shell, size_t argc, char **argv)
{
	(void)argv;

	if ((argc == 2) && (shell_help_requested(shell))) {
		shell_help_print(shell, NULL, 0);
		return;
	}
	SHELL_VT100_CMD(shell, SHELL_VT100_CURSORHOME);
	SHELL_VT100_CMD(shell, SHELL_VT100_CLEARSCREEN);
}

static void cmd_cli(const struct shell *shell, size_t argc, char **argv)
{
	(void)argv;

	if ((argc == 1) || ((argc == 2) && shell_help_requested(shell))) {
		shell_help_print(shell, NULL, 0);
		return;
	}

	shell_fprintf(shell, SHELL_ERROR, SHELL_MSG_SPECIFY_SUBCOMMAND);
}

static void cmd_colors_off(const struct shell *shell, size_t argc, char **argv)
{
	if (!shell_cmd_precheck(shell, (argc == 1), NULL, 0)) {
		return;
	}

	shell->ctx->internal.flags.use_colors = 0;
}

static void cmd_colors_on(const struct shell *shell, size_t argc, char **argv)
{
	if (!shell_cmd_precheck(shell, (argc == 1), NULL, 0)) {
		return;
	}
	shell->ctx->internal.flags.use_colors = 1;
}

static void cmd_colors(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help_print(shell, NULL, 0);
		return;
	}

	if (!shell_cmd_precheck(shell, (argc == 2), NULL, 0)) {
		return;
	}

	shell_fprintf(shell, SHELL_ERROR, "%s:%s%s\r\n", argv[0],
		      SHELL_MSG_UNKNOWN_PARAMETER, argv[1]);
}

static void cmd_echo(const struct shell *shell, size_t argc, char **argv)
{
	if (!shell_cmd_precheck(shell, (argc <= 2), NULL, 0)) {
		return;
	}

	if (argc == 2) {
		shell_fprintf(shell, SHELL_ERROR, "%s:%s%s\r\n", argv[0],
			      SHELL_MSG_UNKNOWN_PARAMETER, argv[1]);
		return;
	}

	shell_fprintf(shell, SHELL_NORMAL, "Echo status: %s\r\n",
		      flag_echo_is_set(shell) ? "on" : "off");
}

static void cmd_echo_off(const struct shell *shell, size_t argc, char **argv)
{
	if (!shell_cmd_precheck(shell, (argc == 1), NULL, 0)) {
		return;
	}

	shell->ctx->internal.flags.echo = 0;
}

static void cmd_echo_on(const struct shell *shell, size_t argc, char **argv)
{
	if (!shell_cmd_precheck(shell, (argc == 1), NULL, 0)) {
		return;
	}

	shell->ctx->internal.flags.echo = 1;
}

static void cmd_history(const struct shell *shell, size_t argc, char **argv)
{
	size_t i= 0;
	size_t len;

	if (!IS_ENABLED(CONFIG_SHELL_HISTORY)) {
		shell_fprintf(shell, SHELL_ERROR, "Command not supported.\r\n");
		return;
	}

	if (!shell_cmd_precheck(shell, (argc == 1), NULL, 0)) {
		return;
	}

	while (1) {
		shell_history_get(shell->history, true,
				  shell->ctx->temp_buff, &len);

		if (len) {
			shell_fprintf(shell, SHELL_NORMAL, "[%3d] %s\r\n",
				      i++, shell->ctx->temp_buff);

		} else {
			break;
		}
	}

	shell->ctx->temp_buff[0] = '\0';
}

static void cmd_cli_stats(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help_print(shell, NULL, 0);
		return;
	}

	if (argc == 2) {
		shell_fprintf(shell, SHELL_ERROR, "%s:%s%s\r\n", argv[0],
			      SHELL_MSG_UNKNOWN_PARAMETER, argv[1]);
		return;
	}

	(void)shell_cmd_precheck(shell, (argc <= 2), NULL, 0);
}

static void cmd_cli_stats_show(const struct shell *shell, size_t argc,
			       char **argv)
{
	if (!IS_ENABLED(CONFIG_SHELL_STATS)) {
		shell_fprintf(shell, SHELL_ERROR, "Command not supported.\r\n");
		return;
	}

	if (!shell_cmd_precheck(shell, (argc == 1), NULL, 0)) {
		return;
	}

	shell_fprintf(shell, SHELL_NORMAL, "Lost logs: %u\r\n",
		      shell->stats->log_lost_cnt);
}

static void cmd_cli_stats_reset(const struct shell *shell,
				size_t argc, char **argv)
{
	if (!IS_ENABLED(CONFIG_SHELL_STATS)) {
		shell_fprintf(shell, SHELL_ERROR, "Command not supported.\r\n");
		return;
	}

	if (!shell_cmd_precheck(shell, (argc == 1), NULL, 0)) {
		return;
	}

	shell->stats->log_lost_cnt = 0;
}

static void cmd_resize_default(const struct shell *shell,
				     size_t argc, char **argv)
{
	if (!shell_cmd_precheck(shell, (argc == 1), NULL, 0)) {
		return;
	}

	SHELL_VT100_CMD(shell, SHELL_VT100_SETCOL_80);
	shell->ctx->vt100_ctx.cons.terminal_wid = SHELL_DEFAULT_TERMINAL_WIDTH;
	shell->ctx->vt100_ctx.cons.terminal_hei = SHELL_DEFAULT_TERMINAL_HEIGHT;
}

static void cmd_resize(const struct shell *shell, size_t argc, char **argv)
{
	int err;

	if (!IS_ENABLED(CONFIG_SHELL_CMD_RESIZE)) {
		shell_fprintf(shell, SHELL_ERROR, "Command not supported.\r\n");
		return;
	}

	if (!shell_cmd_precheck(shell, (argc <= 2), NULL, 0)) {
		return;
	}

	if (argc != 1) {
		shell_fprintf(shell, SHELL_ERROR, "%s:%s%s\r\n", argv[0],
			      SHELL_MSG_UNKNOWN_PARAMETER, argv[1]);
		return;
	}

	err = terminal_size_get(shell,
				&shell->ctx->vt100_ctx.cons.terminal_wid,
				&shell->ctx->vt100_ctx.cons.terminal_hei);
	if (err != 0) {
		shell->ctx->vt100_ctx.cons.terminal_wid =
				SHELL_DEFAULT_TERMINAL_WIDTH;
		shell->ctx->vt100_ctx.cons.terminal_hei =
				SHELL_DEFAULT_TERMINAL_HEIGHT;
		shell_fprintf(shell, SHELL_WARNING,
			      "No response from the terminal, assumed 80x24 screen size\r\n");
	}
}

SHELL_CREATE_STATIC_SUBCMD_SET(m_sub_colors)
{
	SHELL_CMD(off, NULL, SHELL_HELP_COLORS_OFF, cmd_colors_off),
	SHELL_CMD(on, NULL, SHELL_HELP_COLORS_ON, cmd_colors_on),
	SHELL_SUBCMD_SET_END
};

SHELL_CREATE_STATIC_SUBCMD_SET(m_sub_echo)
{
	SHELL_CMD(off, NULL, SHELL_HELP_ECHO_OFF, cmd_echo_off),
	SHELL_CMD(on, NULL, SHELL_HELP_ECHO_ON, cmd_echo_on),
	SHELL_SUBCMD_SET_END
};

SHELL_CREATE_STATIC_SUBCMD_SET(m_sub_cli_stats)
{
	SHELL_CMD(reset, NULL, SHELL_HELP_STATISTICS_RESET, cmd_cli_stats_reset),
	SHELL_CMD(show, NULL, SHELL_HELP_STATISTICS_SHOW, cmd_cli_stats_show),
	SHELL_SUBCMD_SET_END
};

SHELL_CREATE_STATIC_SUBCMD_SET(m_sub_cli)
{
	SHELL_CMD(colors, &m_sub_colors, SHELL_HELP_COLORS, cmd_colors),
	SHELL_CMD(echo, &m_sub_echo, SHELL_HELP_ECHO, cmd_echo),
	SHELL_CMD(stats, &m_sub_cli_stats, SHELL_HELP_STATISTICS, cmd_cli_stats),
	SHELL_SUBCMD_SET_END
};

SHELL_CREATE_STATIC_SUBCMD_SET(m_sub_resize)
{
	SHELL_CMD(default, NULL, SHELL_HELP_RESIZE_DEFAULT, cmd_resize_default),
	SHELL_SUBCMD_SET_END
};

SHELL_CMD_REGISTER(clear, NULL, SHELL_HELP_CLEAR, cmd_clear);
SHELL_CMD_REGISTER(cli, &m_sub_cli, SHELL_HELP_CLI, cmd_cli);
SHELL_CMD_REGISTER(history, NULL, SHELL_HELP_HISTORY, cmd_history);
SHELL_CMD_REGISTER(resize, &m_sub_resize, SHELL_HELP_RESIZE, cmd_resize);

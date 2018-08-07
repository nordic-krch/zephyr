/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <lib/fnmatch/fnmatch.h>
#include "shell_wildcard.h"
#include "shell_utils.h"

static void subcmd_get(const struct shell_cmd_entry *cmd,
		       size_t idx, const struct shell_static_entry **entry,
		       struct shell_static_entry *d_entry) {
	assert(entry != NULL);
	assert(st_entry != NULL);

	if (cmd == NULL) {
		*entry = NULL;
		return;
	}

	if (cmd->is_dynamic) {
		cmd->u.dynamic_get(idx, d_entry);
		*entry = (d_entry->syntax != NULL) ? d_entry : NULL;
	} else {
		*entry = (cmd->u.entry[idx].syntax != NULL) ?
				&cmd->u.entry[idx] : NULL;
	}
}

static shell_wildcard_status_t command_add(char *buff, u16_t *buff_len,
					   char const *cmd, char const *pattern)
{
	u16_t cmd_len = shell_strlen(cmd);
	char *completion_addr;
	u16_t shift;

	/* +1 for space */
	if ((*buff_len + cmd_len + 1) > CONFIG_SHELL_CMD_BUFF_SIZE) {
		return SHELL_WILDCARD_CMD_MISSING_SPACE;
	}

	completion_addr = strstr(buff, pattern);

	if (!completion_addr) {
		return SHELL_WILDCARD_CMD_NO_MATCH_FOUND;
	}

	shift = shell_strlen(completion_addr);

	/* make place for new command: + 1 for space + 1 for EOS */
	memmove(completion_addr + cmd_len + 1, completion_addr, shift + 1);
	memcpy(completion_addr, cmd, cmd_len);
	/* adding space to not brake next command in the buffer */
	completion_addr[cmd_len] = ' ';

	*buff_len += cmd_len + 1; // + 1 for space

	return SHELL_WILDCARD_CMD_ADDED;
}

/**
 * @internal @brief Function for searching and adding commands to the temporary
 * shell buffer matching to wildcard pattern.
 *
 * This function is internal to shell module and shall be not called directly.
 *
 * @param[in/out] shell		Pointer to the CLI instance.
 * @param[in]	  cmd		Pointer to command which will be processed
 * @param[in]	  pattern	Pointer to wildcard pattern.
 *
 * @retval WILDCARD_CMD_ADDED	All matching commands added to the buffer.
 * @retval WILDCARD_CMD_ADDED_MISSING_SPACE  Not all matching commands added
 *					     because CONFIG_SHELL_CMD_BUFF_SIZE
 *					     is too small.
 * @retval WILDCARD_CMD_NO_MATCH_FOUND No matching command found.
 */
static shell_wildcard_status_t commands_expand(const struct shell *shell,
					      const struct shell_cmd_entry *cmd,
					      const char *pattern)
{
	shell_wildcard_status_t ret_val = SHELL_WILDCARD_CMD_NO_MATCH_FOUND;
	struct shell_static_entry const * p_static_entry = NULL;
	struct shell_static_entry static_entry;
	size_t cmd_idx = 0;
	size_t cnt = 0;

	do {
		subcmd_get(cmd, cmd_idx++, &p_static_entry, &static_entry);

		if (!p_static_entry) {
			break;
		}

		if (0 == fnmatch(pattern, p_static_entry->syntax, 0)) {
			ret_val = command_add(shell->ctx->temp_buff,
					      &shell->ctx->cmd_tmp_buff_len,
					      p_static_entry->syntax, pattern);
			if (ret_val == SHELL_WILDCARD_CMD_MISSING_SPACE) {
				shell_fprintf(shell,
					      SHELL_WARNING,
					      "Command buffer is too short to"
					      "expand all commands matching "
					      "wildcard pattern: %s\r\n",
					      pattern);
				break;
			} else if (ret_val != SHELL_WILDCARD_CMD_ADDED)
			{
				break;
			}
			cnt++;
		}
	} while(cmd_idx);

	if (cnt > 0)
	{
		shell_pattern_remove(shell->ctx->temp_buff,
				     &shell->ctx->cmd_tmp_buff_len, pattern);
	}

	return ret_val;
}

bool shell_wildcard_character_exist(const char *str)
{
	size_t str_len = shell_strlen(str);

	for (size_t i = 0; i < str_len; i++) {
		if ((str[i] == '?') || (str[i] == '*')) {
			return true;
		}
	}

	return false;
}

void shell_wildcard_prepare(const struct shell *shell)
{
	/* Wildcard can be correctly handled under following conditions:
	 - wildcard command does not have a handler
	 - wildcard command is on the deepest commands level
	 - other commands on the same level as wildcard command shall also not
	   have a handler

	 Algorithm:
	 1. Command buffer is copied to Temp buffer.
	 2. Algorithm goes through Command buffer to find handlers and
	    subcommands.
	 3. If algorithm will find a wildcard character it switches to Temp
	    buffer.
	 4. In the Temp buffer command with found wildcard character is changed
	    into matching command(s).
	 5. Algorithm switch back to Command buffer and analyzes next command.
	 6. When all arguments are analyzed from Command buffer, Temp buffer is
	    copied to Command buffer.
	 7. Last found handler is executed with all arguments in the Command
	    buffer.
	 */

	memset(shell->ctx->temp_buff, 0, sizeof(shell->ctx->temp_buff));
	memcpy(shell->ctx->temp_buff,
			shell->ctx->cmd_buff,
			shell->ctx->cmd_buff_len);

	/* Function shell_spaces_trim must be used instead of shell_make_argv.
	 * At this point it is important to keep temp_buff as a one string.
	 * It will allow to find wildcard commands easily with strstr function.
	 */
	shell_spaces_trim(shell->ctx->temp_buff);

	/* +1 for EOS*/
	shell->ctx->cmd_tmp_buff_len = shell_strlen(shell->ctx->temp_buff) + 1;
}


shell_wildcard_status_t shell_wildcard_process(const struct shell *shell,
					      const struct shell_cmd_entry *cmd,
					      const char *pattern)
{
	shell_wildcard_status_t ret_val = SHELL_WILDCARD_NOT_FOUND;

	if (cmd == NULL) {
		return ret_val;
	}

	if (!shell_wildcard_character_exist(pattern)) {
		return ret_val;
	}

	/* Function will search commands tree for
	 * commands matching wildcard pattern stored in
	 * argv[cmd_lvl]. If match is found wildcard
	 * pattern will be replaced by matching commands
	 * in temp_buffer. If there is no space to add
	 * all matching commands function will add as
	 * many as possible. Next it will continue to
	 * search for next wildcard pattern and it will
	 * try to add matching commands.
	 */
	ret_val = commands_expand(shell, cmd, pattern);

	return ret_val;
}

void shell_wildcard_finalize(const struct shell *shell)
{
	/* Copy temp_buff to cmd_buff */
	memcpy(shell->ctx->cmd_buff,
	       shell->ctx->temp_buff,
	       shell->ctx->cmd_tmp_buff_len);
	shell->ctx->cmd_buff_len = shell->ctx->cmd_tmp_buff_len;
}

static void cmd_wildcard(const struct shell *shell, size_t argc, char **argv)
{
	for (size_t i = 1; i < argc; i++) {
		shell_fprintf(shell, SHELL_INFO, "argv[%d]=%s\r\n", i, argv[i]);
	}
}


SHELL_CREATE_STATIC_SUBCMD_SET(m_sub_dupa)
{
	SHELL_CMD(ali, NULL, NULL, NULL),
	SHELL_CMD(aliny, NULL, NULL, NULL),
	SHELL_CMD(tomasza,  NULL, NULL, NULL),
};

SHELL_CREATE_STATIC_SUBCMD_SET(m_sub_wildcard)
{
	SHELL_CMD(aaala, NULL, NULL, NULL),
	SHELL_CMD(aatrybut, NULL, NULL, NULL),
	SHELL_CMD(atomik,	  NULL, NULL, NULL),
	SHELL_CMD(azombik,  NULL, NULL, NULL),
	SHELL_CMD(dupa, &m_sub_dupa, NULL, NULL),
	SHELL_CMD(kupa, NULL, NULL, NULL),
	SHELL_SUBCMD_SET_END
};

SHELL_CMD_REGISTER(wildcard, &m_sub_wildcard, NULL, cmd_wildcard);


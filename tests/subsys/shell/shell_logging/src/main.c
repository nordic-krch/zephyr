/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 *  @brief Interactive shell test suite
 *
 */

#include <zephyr/zephyr.h>
#include <zephyr/ztest.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>

static const struct shell *dummy_shell;

static void test_shell_execute_cmd(const char *cmd, int result)
{
	int ret;

	ret = shell_execute_cmd(NULL, cmd);

	TC_PRINT("shell_execute_cmd(%s): %d\n", cmd, ret);

	zassert_true(ret == result, "cmd: %s, got:%d, expected:%d",
							cmd, ret, result);
}

ZTEST(shell_logging, test_block)
{

}

static void *setup(void)
{
	dummy_shell = shell_backend_dummy_get_ptr();
}

ZTEST_SUITE(shell_logging, NULL, setup, NULL, NULL, NULL);

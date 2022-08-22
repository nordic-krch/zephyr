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

ZTEST(shell_logging, test_block)
{

}

static void *setup(void)
{
	dummy_shell = shell_backend_dummy_get_ptr();
}

static void before(void)
{
	/* Disable all execpt dummy backend. */
	STRUCT_SECTION_FOREACH(log_backend, backend) {
		if (backend != dummy_shell->log_backend->backend) {
			log_backend_deactivate(backend);
		} else {
			log_backend_activate(backend, backend->cb->ctx);
		}
	}
}

static void after(void)
{
	/* Enable all but disable dummy shell backend. */
	STRUCT_SECTION_FOREACH(log_backend, backend) {
		if (backend != dummy_shell->log_backend->backend) {
			log_backend_deactivate(backend);
		} else {
			log_backend_activate(backend, backend->cb->ctx);
		}
	}
}

ZTEST_SUITE(shell_logging, NULL, setup, before, after, NULL);

/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ext_log_system_adapter.h"
#include "ext_log_system.h"

#define LOG_MODULE_NAME ext_log_system
#include <logging/log.h>
LOG_MODULE_REGISTER();

/** @brief Translation of custom log levels to logging subsystem levels. */
static const u8_t log_level_lut[] = {
	LOG_LEVEL_ERR,  /* EXT_LOG_CRITICAL */
	LOG_LEVEL_ERR,  /* EXT_LOG_ERROR */
	LOG_LEVEL_WRN,  /* EXT_LOG_WARNING */
	LOG_LEVEL_INF,  /* EXT_LOG_NOTICE */
	LOG_LEVEL_INF,  /* EXT_LOG_INFO */
	LOG_LEVEL_DBG   /* EXT_LOG_DEBUG */
};

static void log_handler(enum ext_log_level level, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_generic(_LOG_SRC_LVL(log_level_lut[level], LOG_CURRENT_MODULE_ID()),
		    fmt, ap);
	va_end(ap);
}

void ext_log_system_log_adapt(void)
{
	ext_log_handler_set(log_handler);
}

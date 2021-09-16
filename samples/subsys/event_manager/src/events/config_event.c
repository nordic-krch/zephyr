/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include "config_event.h"


static int log_config_event(const struct event_header *eh, char *buf,
			    size_t buf_len)
{
	struct config_event *event = cast_config_event(eh);

	return snprintf(buf, buf_len, "init_val_1=%d", event->init_value1);
}

EVENT_TYPE_DEFINE(config_event,
		  true,
		  log_config_event,
		  NULL);

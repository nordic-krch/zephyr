/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/zephyr.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(app);
static void timeout(struct k_timer *timer)
{
	LOG_ERR("interrupt");
}

K_TIMER_DEFINE(timer, timeout, NULL);

void main(void)
{
	k_timer_start(&timer, K_MSEC(5), K_MSEC(5));

	while (1) {
		LOG_WRN("thread");
		k_msleep(10);
	}
}

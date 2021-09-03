/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(main);

void main(void)
{
	int cnt = 0;

	while (1) {
//		LOG_INF("Test %d", cnt++);
		k_msleep(500);
	}
}


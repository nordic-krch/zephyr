/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <logging/log.h>
#include <hal/nrf_gpio.h>
LOG_MODULE_REGISTER(main);

void main(void)
{
	nrf_gpio_cfg_output(32+14);
	nrf_gpio_cfg_output(32+15);

	int cnt = 0;

		LOG_INF("Test %d", cnt++);
	while (1) {
//		printk("Test %d\n", cnt);
		cnt++;
		LOG_INF("Test %d", cnt);
		k_msleep(500);
	}
}

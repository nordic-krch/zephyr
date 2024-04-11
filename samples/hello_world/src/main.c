/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <hal/nrf_gpio.h>

int main(void)
{
	nrf_gpio_cfg_output(9*32);
	nrf_gpio_cfg_output(9*32+1);
	nrf_gpio_cfg_output(9*32+2);
	nrf_gpio_cfg_output(9*32+3);
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	return 0;
}

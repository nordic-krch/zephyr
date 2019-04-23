/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <misc/printk.h>
#include <soc.h>
#include <nrf_clock.h>

void main(void)
{
	printk("Hello World! %s\n", CONFIG_BOARD);

	NRF_CLOCK->LFCLKSRC = NRF_CLOCK_LFCLK_Xtal;
	NRF_CLOCK->TASKS_LFCLKSTART = 1;

	k_busy_wait(1000000);

	NRF_RTC0->TASKS_START = 1;

	k_busy_wait(1000000);
	printk("1 second Xtal: %d\n", NRF_RTC0->COUNTER);

	NRF_RTC0->TASKS_STOP = 1;
	NRF_RTC0->TASKS_CLEAR = 1;
	NRF_CLOCK->TASKS_LFCLKSTOP= 1;
	k_busy_wait(100000);
	NRF_CLOCK->LFCLKSRC = NRF_CLOCK_LFCLK_RC;
	NRF_CLOCK->TASKS_LFCLKSTART = 1;

	k_busy_wait(1000000);

	NRF_RTC0->TASKS_START = 1;
	k_busy_wait(1000000);
	printk("1 second RC: %d\n", NRF_RTC0->COUNTER);
}

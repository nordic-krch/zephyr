/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <random/rand32.h>

static void timeout(struct k_timer *timer)
{

}

void main(void)
{
	int cnt = 0;
	int i = 0;
	struct k_timer timer;

	k_timer_init(&timer, timeout, NULL);

	printk("Hello World! %s\n", CONFIG_BOARD);
	while (1) {
		printk("%d\n", cnt++);
		k_timer_start(&timer, K_USEC(100), K_NO_WAIT);
		k_busy_wait(80 + i);
		i++;
		if (i > 30) i = 0;

		k_timer_status_sync(&timer);
	}
}

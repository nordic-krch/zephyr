/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>

void main(void)
{
	struct k_mutex mutex;
	int ret;

	ret = k_mutex_init(&mutex);
	if (ret < 0) {
		printk("err: %d\n", ret);
	}


	ret = k_mutex_lock(&mutex, K_NO_WAIT);
	if (ret < 0) {
		printk("err: %d\n", ret);
	}

	ret = k_mutex_unlock(&mutex);
	if (ret < 0) {
		printk("err: %d\n", ret);
	}

	printk("Hello World! %s\n", CONFIG_BOARD);
}

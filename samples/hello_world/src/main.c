/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/random/rand32.h>

K_THREAD_STACK_DEFINE(stack1, 1024);
K_THREAD_STACK_DEFINE(stack2, 1024);

static struct k_thread thread1;
static struct k_thread thread2;

uint32_t tcnt;
static void timeout(struct k_timer *timer)
{
	tcnt++;
}

K_TIMER_DEFINE(timer, timeout, NULL);

static bool volatile in_thr2;
static int pre_cnt;

static void thread1_func(void *unused1, void *unused2, void *unused3)
{
	/* Higher priority thread */
	while (1) {
		uint32_t r = sys_rand32_get();
		uint32_t s = 200 + (r & 0x1f);
		uint32_t t = 100 + ((r >> 8) & 0x1f);

		if (in_thr2) {
			/* Calculate number of preemptions. */
			pre_cnt++;
		}

		k_sleep(K_USEC(s));
		/* Attempt to (re)start the timer (same as in other thread. */
		k_timer_start(&timer, K_USEC(t), K_NO_WAIT);

	}
}

static void thread2_func(void *unused1, void *unused2, void *unused3)
{
	/* Lower priority thread */
	while (1) {
		uint32_t r = sys_rand32_get();
		uint32_t s = 200 + (r & 0x1f);
		uint32_t t = 100 + ((r >> 8) & 0x1f);

		k_sleep(K_USEC(s));

		in_thr2 = true;

		/* Attempt to (re)start the timer (same as in other thread. */
		k_timer_start(&timer, K_USEC(t), K_NO_WAIT);

		in_thr2 = false;
	}
}

int main(void)
{
	/* Create two preemptive threads with different priority. */
	(void)k_thread_create(&thread1,
				stack1, 1024,
				thread1_func,
				NULL, NULL, NULL,
				K_PRIO_PREEMPT(1), 0, K_NO_WAIT);

	(void)k_thread_create(&thread2,
				stack2, 1024,
				thread2_func,
				NULL, NULL, NULL,
				K_PRIO_PREEMPT(2), 0, K_NO_WAIT);

	while (1) {
		k_msleep(3000);
		printk("pre_cnt:%d tcnt:%d\n", pre_cnt, tcnt);
	}
	return 0;
}

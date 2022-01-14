/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <string.h>

static k_tid_t idle_tid[CONFIG_MP_NUM_CPUS];

#define GET_IDLE_TID(i, tid) do {\
	if (strcmp(tname, "idle 0" STRINGIFY(i)) == 0) { \
		idle_tid[i] = tid; \
	} \
} while (0);

static void thread_cb(const struct k_thread *cthread, void *user_data)
{
	const char *tname = k_thread_name_get((struct k_thread *)cthread);

	UTIL_LISTIFY(CONFIG_MP_NUM_CPUS, GET_IDLE_TID, (k_tid_t)cthread)
}

static void timeout_cb(struct k_timer *timer)
{
	static uint64_t prev_idle_cycles;
	static uint64_t total_cycles;
	k_thread_runtime_stats_t rt_stats_all;
	uint64_t idle_cycles = 0;
	int err = 0;

	for (int i = 0; i < CONFIG_MP_NUM_CPUS; i++) {
		k_thread_runtime_stats_t thread_stats;

		err = k_thread_runtime_stats_get(idle_tid[i], &thread_stats);
		if (err < 0) {
			return;
		}

		idle_cycles += thread_stats.execution_cycles;
	}

	err = k_thread_runtime_stats_all_get(&rt_stats_all);
	if (err < 0) {
		return;
	}

	int load = 1000 - (1000 * (idle_cycles - prev_idle_cycles) /
			(rt_stats_all.execution_cycles - total_cycles));

	prev_idle_cycles = idle_cycles;
	total_cycles = rt_stats_all.execution_cycles;

	printk("load: %d.%d%%\n", load / 10, load % 10);
}

K_TIMER_DEFINE(timer, timeout_cb, NULL);

void main(void)
{
	k_thread_foreach(thread_cb, NULL);

	k_timer_start(&timer, K_MSEC(1000), K_MSEC(1000));

	for (int i = 0; i < CONFIG_MP_NUM_CPUS; i++) {
		printk("\t%s\n", k_thread_name_get(idle_tid[i]));
	}

	printk("Hello World! %s\n", CONFIG_BOARD);
}

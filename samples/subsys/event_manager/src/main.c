/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <event_manager/event_manager.h>
#include <config_event.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE);

#define INIT_VALUE1 3

static k_tid_t idle_tid;
static k_tid_t logging_tid;
static void timeout(struct k_timer *timer) {


	k_thread_runtime_stats_t rt_stats_thread;
	k_thread_runtime_stats_t rt_stats_logging;
	k_thread_runtime_stats_t rt_stats_all;
	int err = 0;

	err = k_thread_runtime_stats_get(idle_tid, &rt_stats_thread);
	if (err < 0) {
		return;
	}

	err = k_thread_runtime_stats_get(logging_tid, &rt_stats_logging);
	if (err < 0) {
		return;
	}

	err = k_thread_runtime_stats_all_get(&rt_stats_all);
	if (err < 0) {
		return;
	}

	int load = 10000 - (10000 * (rt_stats_thread.execution_cycles + rt_stats_logging.execution_cycles) /
			(rt_stats_all.execution_cycles));
	printk("CPU load %d\n", load);
	k_panic();
}

static void thread_cb(const struct k_thread *cthread, void *user_data)
{
	const char *tname = k_thread_name_get((struct k_thread *)cthread);

	printk("%s\n", tname);
	if (strcmp(tname, "idle 00") == 0) {
		idle_tid = (struct k_thread *)cthread;
	}
	if (strcmp(tname, "logging") == 0) {
		logging_tid = (struct k_thread *)cthread;
	}
}

K_TIMER_DEFINE(timer, timeout, NULL);
void main(void)
{
	k_thread_foreach(thread_cb, NULL);
	k_msleep(10);

	if (!idle_tid) {
		printk("Failed to identify idle thread. CPU load will not be tracked\n");
	}

	k_timer_start(&timer, K_MSEC(20000), K_NO_WAIT);
	if (event_manager_init()) {
		LOG_ERR("Event Manager not initialized");
	} else {
		struct config_event *event = new_config_event();

		event->init_value1 = INIT_VALUE1;
		EVENT_SUBMIT(event);
	}
}

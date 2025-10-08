/*
 * Copyright (c) 2018 Intel Corporation.
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/pm/policy.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys_clock.h>
#include <zephyr/sys/time_units.h>

/** Lock to synchronize access to the events list. */
static struct k_spinlock events_lock;
/** List of events. */
static sys_slist_t events_list;
/** Pointer to Next Event. */
static uint64_t next_event = INT64_MAX;

/* Called under spinlock. */
static void update_next_event(void)
{
	struct pm_policy_event *evt;

	next_event = INT64_MAX;
	SYS_SLIST_FOR_EACH_CONTAINER(&events_list, evt, node) {
		if (evt->time < next_event) {
			next_event = evt->time;
		}
	}
}

int64_t pm_policy_next_event_ticks(void)
{
	int64_t ticks = -1;

	K_SPINLOCK(&events_lock) {
		if (next_event == INT64_MAX) {
			K_SPINLOCK_BREAK;
		}

#if PM_POLICY_EVENT_USE_CYC
		int64_t cyc = next_event - k_cycle_get_64();

		ticks = (cyc < 0) ? 0 : k_cyc_to_ticks_floor64(cyc);
#else
		ticks = next_event->uptime_ticks - k_uptime_ticks();
		if (ticks < 0) {
			ticks = 0;
		}
#endif
	}

	return ticks;
}

static ALWAYS_INLINE void event_register(struct pm_policy_event *evt, int64_t time,
					 bool abs, bool us)
{
	K_SPINLOCK(&events_lock) {
		if (abs) {
			if (IS_ENABLED(PM_POLICY_EVENT_USE_CYC)) {
				evt->time = k_ticks_to_cyc_floor64(time);
			} else {
				evt->time = time;
			}
		} else if (us) {
			if (IS_ENABLED(PM_POLICY_EVENT_USE_CYC)) {
				evt->time = k_us_to_cyc_floor32(us) + k_cycle_get_64();
			} else {
				evt->time = k_us_to_ticks_floor32(us) + k_uptime_ticks();
			}
		} else {
			if (IS_ENABLED(PM_POLICY_EVENT_USE_CYC)) {
				evt->time = k_ticks_to_cyc_floor64(time) + k_cycle_get_64();
			} else {
				evt->time = k_uptime_ticks() + time;
			}
		}
		evt->time = time;
		sys_slist_append(&events_list, &evt->node);
		if (evt->time < next_event) {
			next_event = evt->time;
		}
	}
}

void pm_policy_event_register(struct pm_policy_event *evt, int64_t uptime_ticks)
{
	event_register(evt, uptime_ticks, true, false);
}

void pm_policy_event_register_rel(struct pm_policy_event *evt, uint32_t ticks)
{
	event_register(evt, ticks, false, false);
}

void pm_policy_event_register_rel_us(struct pm_policy_event *evt, uint32_t us)
{
	event_register(evt, us, false, true);
}

static inline void event_update(struct pm_policy_event *evt, int64_t time, bool abs, bool us)
{
	K_SPINLOCK(&events_lock) {
		uint64_t prev = evt->time;

		if (abs) {
			if (IS_ENABLED(PM_POLICY_EVENT_USE_CYC)) {
				evt->time = k_ticks_to_cyc_floor64(time);
			} else {
				evt->time = time;
			}
		} else if (us) {
			if (IS_ENABLED(PM_POLICY_EVENT_USE_CYC)) {
				evt->time = k_us_to_cyc_floor32(us) + k_cycle_get_64();
			} else {
				evt->time = k_us_to_ticks_floor32(us) + k_uptime_ticks();
			}
		} else {
			if (IS_ENABLED(PM_POLICY_EVENT_USE_CYC)) {
				evt->time = k_ticks_to_cyc_floor64(time) + k_cycle_get_64();
			} else {
				evt->time = k_uptime_ticks() + time;
			}
		}

		if (evt->time < next_event) {
			next_event = evt->time;
		} else if (prev <= next_event) {
			update_next_event();
		}
	}
}

void pm_policy_event_update(struct pm_policy_event *evt, int64_t uptime_ticks)
{
	event_update(evt, uptime_ticks, true, false);
}

void pm_policy_event_update_rel(struct pm_policy_event *evt, uint32_t ticks)
{
	event_update(evt, ticks, false, false);
}

void pm_policy_event_update_rel_us(struct pm_policy_event *evt, uint32_t us)
{
	event_update(evt, us, false, true);
}

void pm_policy_event_unregister(struct pm_policy_event *evt)
{
	K_SPINLOCK(&events_lock) {
		(void)sys_slist_find_and_remove(&events_list, &evt->node);
		if (evt->time == next_event) {
			update_next_event();
		}
	}
}

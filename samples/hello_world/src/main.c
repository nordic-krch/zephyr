/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/kernel.h>
#include <zephyr/timing/timing.h>
#include <zephyr/pm/policy.h>

#ifdef CONFIG_SOC_SERIES_NRF54LX
#define CPU_MHZ 128
#else
#define CPU_MHZ 320
#endif

#define MEAS_START(ts) ts = DWT->CYCCNT

#define MEAS_END(ts, str) \
	ts = DWT->CYCCNT - ts; \
	printk(str " took %d cyc %dns\n", ts, (ts * 1000) / CPU_MHZ)

int main(void)
{
	uint32_t t;

	timing_init();

#ifdef CONFIG_SOC_SERIES_NRF54LX
	MEAS_START(t);
	int handle = rramc_wakeup_request(10, true);
	MEAS_END(t, "short rrams wakeup");

	MEAS_START(t);
	int handle2 = rramc_wakeup_request(10, true);
	MEAS_END(t, "repeated short rrams wakeup");

	MEAS_START(t);
	rramc_wakeup_release(handle);
	MEAS_END(t, "rrams wakeup release (still busy)");

	MEAS_START(t);
	rramc_wakeup_release(handle2);
	MEAS_END(t, "rrams wakeup release");

	MEAS_START(t);
	handle = rramc_wakeup_request(100, true);
	MEAS_END(t, "rrams wakeup scheduled");

	MEAS_START(t);
	rramc_wakeup_release(handle);
	MEAS_END(t, "rrams wakeup release scheduled");
#endif

	MEAS_START(t);
	uint64_t k = k_cycle_get_64();
	MEAS_END(t, "k_cycle_get");

	struct pm_policy_event evt;
	MEAS_START(t);
	uint64_t tick = sys_clock_tick_get();
	pm_policy_event_register(&evt, tick + 50);
	MEAS_END(t, "register event");

	printf("Hello World! %s %lld tick:%lld\n", CONFIG_BOARD_TARGET, k, tick);

	return 0;
}

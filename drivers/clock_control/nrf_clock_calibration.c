
/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <sensor.h>
#include <drivers/clock_control.h>
#include <drivers/sensor_monitor.h>
#include <init.h>
#include "nrf_clock_calibration.h"
#include <drivers/clock_control/nrf_clock_control.h>
#include <hal/nrf_clock.h>
#include <logging/log.h>
#include <stdlib.h>

LOG_MODULE_DECLARE(clock_control, CONFIG_CLOCK_CONTROL_LOG_LEVEL);

/**
 * Terms:
 * - calibration - overall process of LFRC clock calibration which is performed
 *   periodically, calibration may include temperature monitoring, hf XTAL
 *   starting and stopping.
 * - cycle - all calibration phases (waiting, temperature monitoring,
 *   calibration).
 * - process - calibration process which may consists of hf XTAL clock
 *   requesting, performing hw calibration and releasing hf clock.
 * - hw_cal - calibration action performed by the hardware.
 *
 * Those terms are later on used in function names.
 */

static int total_cnt; /* Total number of calibrations. */
static int total_skips_cnt; /* Total number of skipped calibrations. */

/* Callback called on hfclk started. */
static void cal_hf_on_callback(struct device *dev, void *user_data);
static struct clock_control_async_data clk_async_on_data;
static struct device *clk_dev;
static bool init_done;
static bool active;

static void timer_handler(struct k_timer *timer);
K_TIMER_DEFINE(timer, timer_handler, NULL);

/* Convert sensor value to 0.25'C units. */
static inline s16_t sensor_value_to_temp_unit(struct sensor_value *val)
{
	return (s16_t)(4 * val->val1 + val->val2 / 250000);
}

/* Function checks if temperature change exceeded diff. If function returns
 * true, action (calibration) will be performed.
 */
static bool decision_func(const struct sensor_monitor *monitor,
			  struct sensor_value *value,
			  void *user_data)
{
	s16_t diff;
	static s16_t prev_temperature = INT16_MAX; /* Previous temperature measurement. */
	s16_t temperature = sensor_value_to_temp_unit(value);
	bool decision;

	diff = abs(temperature - prev_temperature);
	prev_temperature = temperature;

	decision = (diff >= CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_TEMP_DIFF);

	if (!decision) {
		total_skips_cnt++;
	}

	return decision;
}

/* Function starts calibration process by requesting hf clock. */
static void hf_req(void)
{
	clk_async_on_data.cb = cal_hf_on_callback;
	clock_control_async_on(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_HF,
				&clk_async_on_data);
}

/* Start HW calibration assuming that HFCLK XTAL is on. */
static void start_hw_cal(void)
{
	LOG_DBG("Starting HW calibration");

	/* Workaround for Errata 192 */
	if (IS_ENABLED(CONFIG_SOC_SERIES_NRF52X)) {
		*(volatile uint32_t *)0x40000C34 = 0x00000002;
	}

	nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_CAL);
}

static void start_process_action(void)
{
	if (IS_ENABLED(CONFIG_SOC_SERIES_NRF53X)) {
		/* nrf53 is autonomously managing hf xtal clock. */
		start_hw_cal();
	} else {
		hf_req();
	}
}

static void lfclk_granted_cb(struct device *dev, void *user_data)
{
	start_process_action();
}

static void start_process(void)
{
	LOG_DBG("Starting calibration process");
	active = true;
	if (!IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_K32SRC_ALWAYS_ON)) {
		/* request clk to ensure that it is not stopped in between */
		clk_async_on_data.cb = lfclk_granted_cb;
		clock_control_async_on(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_LF,
					&clk_async_on_data);
	} else {
		start_process_action();
	}
}

static void timer_handler(struct k_timer *timer)
{
	start_process();
}

static void action_cb(const struct sensor_monitor *monitor,
		      enum sensor_monitor_reason reason, void *user_data)
{
	start_process();
}

SENSOR_MONITOR_STATIC_DEFINE(temp_monitor, DT_INST_0_NORDIC_NRF_TEMP_LABEL,
		SENSOR_CHAN_DIE_TEMP,
		250*CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_PERIOD,
		CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_MAX_SKIP,
		decision_func, action_cb, NULL);

/* Returns true if temperature monitoring should be used. */
static bool use_temp_monitor(void)
{
	return CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_TEMP_DIFF &&
			(CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_MAX_SKIP > 0);
}

static void start_cycle(void)
{
	/* Trigger unconditional calibration initially and start periodic. */
	if (use_temp_monitor()) {
		sensor_monitor_start(&temp_monitor);
	} else {
		k_timer_start(&timer,
		    K_MSEC(1),
		    K_MSEC(250*CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_PERIOD));
	}

	LOG_DBG("Started calibration cycle %s",
			use_temp_monitor() ? "(temp sensor used)" : "");
}

static void stop_cycle(void)
{
	if (use_temp_monitor()) {
		sensor_monitor_stop(&temp_monitor);
	} else {
		k_timer_stop(&timer);
	}
}

void z_nrf_clock_calibration_force_start(void)
{
	if (!active) {
		/* Restart cycle, since it initially starts with calibration. */
		stop_cycle();
		start_cycle();
	}
}

void z_nrf_clock_calibration_lfclk_started(struct device *dev)
{
	clk_dev = dev;
	if (!init_done) {
		return;
	}

	start_cycle();
}

void z_nrf_clock_calibration_stop(struct device *dev)
{
	int key;

	LOG_DBG("Stop calibration");
	if (IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_K32SRC_ALWAYS_ON)) {
		__ASSERT(false, "Unexpected call. Clock is always on.");
	}

	key = irq_lock();

	stop_cycle();

	active = false;
	clk_dev = NULL;
	irq_unlock(key);
}

static void init(void)
{
	LOG_DBG("Calibration init");
	/* Anomaly 36: After watchdog timeout reset, CPU lockup reset, soft
	 * reset, or pin reset EVENTS_DONE and EVENTS_CTTO are not reset.
	 */
	nrf_clock_event_clear(NRF_CLOCK, NRF_CLOCK_EVENT_DONE);

	nrf_clock_int_enable(NRF_CLOCK, NRF_CLOCK_INT_DONE_MASK);
	nrf_clock_cal_timer_timeout_set(NRF_CLOCK,
			CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_PERIOD);

}

/* Called when HFCLK XTAL is on. Start calibration if process was not stopped.
 */
static void cal_hf_on_callback(struct device *dev, void *user_data)
{
	start_hw_cal();
}

/* When calibration is done, module returns to idle. It may happen that lfclk
 * was requested to be stopped when calibration was ongoing.
 */
static void on_hw_cal_done(void)
{
	/* Workaround for Errata 192 */
	if (IS_ENABLED(CONFIG_SOC_SERIES_NRF52X)) {
		*(volatile uint32_t *)0x40000C34 = 0x00000000;
	}

	total_cnt++;
	LOG_DBG("Calibration done.");

	if (!IS_ENABLED(CONFIG_SOC_SERIES_NRF53X)) {
		clock_control_off(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_HF);
	}

	if (!IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_K32SRC_ALWAYS_ON)) {
		/* Release lf clock which was held to protect against stopping
		 * the clock while in calibration process.
		 */
		clock_control_off(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_LF);
	}
	active = false;
}

static bool clock_event_check_and_clean(u32_t evt, u32_t intmask)
{
	bool ret = nrf_clock_event_check(NRF_CLOCK, evt) &&
			nrf_clock_int_enable_check(NRF_CLOCK, intmask);

	if (ret) {
		nrf_clock_event_clear(NRF_CLOCK, evt);
	}

	return ret;
}

void z_nrf_clock_calibration_isr(void)
{
	if (clock_event_check_and_clean(NRF_CLOCK_EVENT_DONE,
					NRF_CLOCK_INT_DONE_MASK)) {
		on_hw_cal_done();
	}
}

int z_nrf_clock_calibration_count(void)
{
	if (!IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_DEBUG)) {
		return -1;
	}

	return total_cnt;
}

int z_nrf_clock_calibration_skips_count(void)
{
	if (!IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_DEBUG)) {
		return -1;
	}

	return total_skips_cnt;
}

static int enable_calibration(struct device *dev)
{
	bool do_start;
	int key;

	key = irq_lock();
	do_start = (clk_dev != NULL);
	init_done = true;
	irq_unlock(key);

	init();
	if (do_start) {
		start_cycle();
	}
	LOG_DBG("Enabled calibration, LFCLK %s running", do_start ? "" : "not");

	return 0;
}

SYS_INIT(enable_calibration, POST_KERNEL, 0);

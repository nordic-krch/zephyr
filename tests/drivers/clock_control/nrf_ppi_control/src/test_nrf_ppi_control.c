/*
 * Copyright (c) 2019, Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <ztest.h>
#include <clock_control.h>
#include <nrfx_timer.h>
#include <logging/log.h>
#include <drivers/clock_control/nrf_clock_control.h>
LOG_MODULE_REGISTER(test);

static nrfx_timer_t timer = NRFX_TIMER_INSTANCE(1);

static void timer_handler(nrf_timer_event_t event_type, void *context)
{
	/* empty */
}

static void timer_init(void)
{
	static bool init = false;

	if (init) {
		return;
	}
	nrfx_timer_config_t config = NRFX_TIMER_DEFAULT_CONFIG;

	config.bit_width = NRF_TIMER_BIT_WIDTH_32;

	nrfx_err_t err = nrfx_timer_init(&timer, &config, timer_handler);
	zassert_equal(err, NRFX_SUCCESS, "");
	init = true;
}

static int ppi_clock_start(struct device *dev, clock_control_subsys_t sys,
				u32_t ms_delay)
{
	u32_t ticks;
	u32_t evt;
	int err;

	ticks = nrfx_timer_capture(&timer, NRF_TIMER_CC_CHANNEL0) +
		nrfx_timer_ms_to_ticks(&timer, ms_delay);

	nrfx_timer_compare(&timer, NRF_TIMER_CC_CHANNEL0, ticks, false);
	evt = nrfx_timer_compare_event_address_get(&timer, 0);

	err = z_clock_control_nrf_ppi_request(dev, sys, evt);
	if (err >= 0) {
		nrfx_timer_enable(&timer);
	}

	return err;
}

static int ppi_clock_stop(struct device *dev, clock_control_subsys_t sys)
{
	nrfx_timer_disable(&timer);

	return z_clock_control_nrf_ppi_release(dev, sys);
}

static bool clock_is_running(struct device *dev, clock_control_subsys_t sys)
{
	struct device *lfclk_dev =
		device_get_binding(DT_INST_0_NORDIC_NRF_CLOCK_LABEL "_32K");

	if (dev == lfclk_dev) {
		return nrf_clock_lf_is_running(NRF_CLOCK);
	}

	return nrf_clock_hf_is_running(NRF_CLOCK,
					NRF_CLOCK_HFCLK_HIGH_ACCURACY);
}

void test_ppi_control(struct device *dev, clock_control_subsys_t sys,
			u32_t startup_us)
{
	u32_t delay_ms = 100;
	enum clock_control_status status;
	int err;

	timer_init();

	status = clock_control_get_status(dev, sys);
	zassert_equal(CLOCK_CONTROL_STATUS_OFF, status,
			"Unexpected status (%d)", status);

	zassert_false(clock_is_running(dev, sys), "Expected clock to be off");

	err = ppi_clock_start(dev, sys, delay_ms);
	zassert_equal(err, 0, "");

	k_busy_wait(1000*(delay_ms + 10 + startup_us/1000));

	//zassert_true(clock_is_running(dev, sys), "Expected clock to be on");
	status = clock_control_get_status(dev, sys);
	zassert_equal(CLOCK_CONTROL_STATUS_ON, status,
			"Unexpected status (%d)", status);

	err = ppi_clock_stop(dev, sys);
	zassert_equal(err, 0, "");
}

void test_hfclk_ppi_control(void)
{
	struct device *dev =
		device_get_binding(DT_INST_0_NORDIC_NRF_CLOCK_LABEL "_16M");
	zassert_not_null(dev, "");

	test_ppi_control(dev, 0, 400);
}

void test_main(void)
{
	ztest_test_suite(test_clock_control,
		ztest_unit_test(test_hfclk_ppi_control)
			 );
	ztest_run_test_suite(test_clock_control);
}

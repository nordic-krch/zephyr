/*
 * Copyright (c) 2019, Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <ztest.h>
#include <clock_control.h>
#include <drivers/clock_control/nrf_clock_control.h>
#include <hal/nrf_clock.h>
#include <nrfx_timer.h>

#ifdef DPPI_PRESENT
#include <nrfx_dppi.h>
#else
#include <nrfx_ppi.h>
#endif

#include <logging/log.h>
LOG_MODULE_REGISTER(test);

#ifndef CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC
#error "LFCLK must use RC source"
#endif

#if CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_PERIOD != 1
#error "Expected 250ms calibration period"
#endif

static void turn_off_clock(struct device *dev, clock_control_subsys_t subsys)
{
	int err;

	do {
		err = clock_control_off(dev, subsys);
	} while (err == 0);
}

static void lfclk_started_cb(struct device *dev, void *user_data)
{
	struct k_sem *sem = user_data;
	k_sem_give(sem);
}

static void start_lfclock(void)
{
	struct device *clk_dev =
		device_get_binding(DT_INST_0_NORDIC_NRF_CLOCK_LABEL);
	struct k_sem sem = Z_SEM_INITIALIZER(sem, 0, 1);
	struct clock_control_async_data lfclk_data = {
		.cb = lfclk_started_cb,
		.user_data = &sem
	};

	/* In case calibration needs to be completed. */
	k_busy_wait(50000);

	turn_off_clock(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_LF);
	turn_off_clock(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_HF);

	clock_control_async_on(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_LF,
				&lfclk_data);
	k_sem_take(&sem, K_MSEC(100));
}

/* Test checks if calibration clock is running and generates interrupt as
 * expected  and starts calibration. Validates that HF clock is turned on
 * for calibration and turned off once calibration is done.
 */
static void test_clock_calibration(void)
{
	struct device *clk_dev =
		device_get_binding(DT_INST_0_NORDIC_NRF_CLOCK_LABEL);
	int cal_count;

	start_lfclock();
	cal_count = z_nrf_clock_calibration_count();

	k_sleep(100 + 250*CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_PERIOD);

	cal_count = z_nrf_clock_calibration_count() - cal_count;
	zassert_equal(cal_count, 2,
		"Unexpected number of calibrations %d (exp: %d)", cal_count, 2);
	cal_count = z_nrf_clock_calibration_count();

	k_sleep(50 + 250*CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_PERIOD);
	cal_count = z_nrf_clock_calibration_count() - cal_count;
	zassert_equal( cal_count, 1,
		"Unexpected number of calibrations %d (exp: %d)", cal_count, 1);

	clock_control_off(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_LF);
}

/* Test checks that when calibration is active then LF clock is not stopped.
 * Stopping is deferred until calibration is done. Test validates that after
 * completing calibration LF clock is shut down.
 */
void test_stopping_when_calibration(void)
{
	struct device *clk_dev =
		device_get_binding(DT_INST_0_NORDIC_NRF_CLOCK_LABEL);
	int cal_count;

	start_lfclock();
	/* it should attempt to turn off lfclk while doing calibration. It
	 * should not interrupt calibration. */
	clock_control_off(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_LF);

	/* Check that calibration is able to perform */
	while (clock_control_get_status(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_LF) ==
			CLOCK_CONTROL_STATUS_ON) {
		/* wait until clock is off */
	}
	cal_count = z_nrf_clock_calibration_count();
	clock_control_on(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_LF);
	k_sleep(100 + 250*CONFIG_CLOCK_CONTROL_NRF_CALIBRATION_PERIOD);
	zassert_equal(z_nrf_clock_calibration_count() - cal_count, 2,
			"Unexpected number of calibrations");

	clock_control_off(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_LF);
}

void test_clock_calibration_force(void)
{
	struct device *clk_dev =
		device_get_binding(DT_INST_0_NORDIC_NRF_CLOCK_LABEL);
	int cal_count;

	start_lfclock();

	k_sleep(50);

	for (int i = 0; i < 5; i++) {
		cal_count = z_nrf_clock_calibration_count();

		z_nrf_clock_calibration_force_start();
		k_sleep(100);

		zassert_equal(z_nrf_clock_calibration_count() - cal_count, 1,
				"Unexpected number of calibrations");
	}

	clock_control_off(clk_dev, CLOCK_CONTROL_NRF_SUBSYS_LF);
}

void test_main(void)
{
	ztest_test_suite(test_nrf_clock_calibration,
		ztest_unit_test(test_clock_calibration),
		ztest_unit_test(test_stopping_when_calibration),
		ztest_unit_test(test_clock_calibration_force)
			 );
	ztest_run_test_suite(test_nrf_clock_calibration);
}

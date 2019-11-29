/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * @addtogroup test_adc_basic_operations
 * @{
 * @defgroup t_adc_basic_basic_operations test_adc_sample
 * @brief TestPurpose: verify ADC driver handles different sampling scenarios
 * @}
 */

#include <drivers/sensor_monitor.h>
#include <zephyr.h>
#include <ztest.h>

static u32_t dummy;
static bool decision;
static bool exp_reason;
static volatile int cb_cnt;

static bool decision_func(const struct sensor_monitor *monitor,
			  struct sensor_value *value,
			  void *user_data)
{
	zassert_equal(user_data, &dummy, "Unexpected user data");
	return decision;
}

static void callback(const struct sensor_monitor *monitor,
		     enum sensor_monitor_reason reason, void *user_data)
{
	zassert_equal(user_data, &dummy, "Unexpected user data");
	zassert_equal(exp_reason, reason, "Unexpected reason");
	cb_cnt++;
}

SENSOR_MONITOR_STATIC_DEFINE(temp_monitor, DT_INST_0_NORDIC_NRF_TEMP_LABEL,
		SENSOR_CHAN_DIE_TEMP, 50, 5, decision_func, callback, &dummy);

void test_sensor_monitor_expire(void)
{
	u32_t skip_period = temp_monitor.period_ms*(temp_monitor.max_skip + 1);
	int err;

	decision = false;
	exp_reason = SENSOR_MONITOR_REASON_EXPIRY;
	cb_cnt = 0;

	err = sensor_monitor_start(&temp_monitor);
	zassert_equal(err, 0, "Unexpected err:%d", err);

	/* Because first measurement is right after start decrease sleep time */
	k_sleep(K_MSEC(skip_period - temp_monitor.period_ms - 10));
	zassert_equal(cb_cnt, 0, "Unexpected number of callbacks (%d)", cb_cnt);

	k_sleep(K_MSEC(10 + 10));
	zassert_equal(cb_cnt, 1, "Unexpected number of callbacks (%d)", cb_cnt);

	k_sleep(K_MSEC(skip_period + 10));
	zassert_equal(cb_cnt, 2, "Unexpected number of callbacks (%d)", cb_cnt);

	err = sensor_monitor_stop(&temp_monitor);
	zassert_equal(err, 0, "Unexpected err:%d", err);

	/* no callback after stop */
	k_sleep(K_MSEC(skip_period + 10));
	zassert_equal(cb_cnt, 2, "Unexpected number of callbacks (%d)", cb_cnt);
}

SENSOR_MONITOR_DEFINE(temp_monitor2, DT_INST_0_NORDIC_NRF_TEMP_LABEL,
		SENSOR_CHAN_DIE_TEMP, 50, 2, decision_func, callback, &dummy);

void test_sensor_monitor_force(void)
{
	int err;

	decision = false;
	exp_reason = SENSOR_MONITOR_REASON_EXPIRY;
	cb_cnt = 0;

	err = sensor_monitor_start(&temp_monitor2);
	zassert_equal(err, 0, "Unexpected err:%d", err);

	k_sleep(K_MSEC(temp_monitor2.period_ms + 10));
	zassert_equal(cb_cnt, 0, "Unexpected number of callbacks");

	/* now change the decision and expect callback */
	decision = true;
	exp_reason = SENSOR_MONITOR_REASON_FORCED;
	k_sleep(K_MSEC(temp_monitor2.period_ms));
	zassert_equal(cb_cnt, 1, "Unexpected number of callbacks");
	k_sleep(K_MSEC(temp_monitor2.period_ms));
	zassert_equal(cb_cnt, 2, "Unexpected number of callbacks");

	decision = false;
	exp_reason = SENSOR_MONITOR_REASON_EXPIRY;

	/* no callback for skip period */
	k_sleep(K_MSEC(temp_monitor2.period_ms * temp_monitor2.max_skip));
	zassert_equal(cb_cnt, 2, "Unexpected number of callbacks");

	/* callback after skip period */
	k_sleep(K_MSEC(temp_monitor2.period_ms));
	zassert_equal(cb_cnt, 3, "Unexpected number of callbacks");

	err = sensor_monitor_stop(&temp_monitor2);
	zassert_equal(err, 0, "Unexpected err:%d", err);
}

void test_sensor_monitor_stop(void)
{
	int err;
	int key;

	err = sensor_monitor_stop(&temp_monitor);
	zassert_equal(err, -EINVAL, "Unexpected err:%d", err);

	key = irq_lock();
	err = sensor_monitor_start(&temp_monitor);
	zassert_equal(err, 0, "Unexpected err:%d", err);

	err = sensor_monitor_stop(&temp_monitor);
	zassert_equal(err, -EINVAL, "Unexpected err:%d", err);
	irq_unlock(key);

	err = sensor_monitor_start(&temp_monitor);
	zassert_equal(err, 0, "Unexpected err:%d", err);

	cb_cnt = 0;
	decision = true;
	exp_reason = SENSOR_MONITOR_REASON_FORCED;

	k_sleep(K_MSEC(temp_monitor.period_ms - 10));
	zassert_equal(cb_cnt, 1, "Unexpected number of callbacks (%d)", cb_cnt);
}

void test_main(void)
{
	ztest_test_suite(sensor_monitor_basic_test,
			 ztest_unit_test(test_sensor_monitor_expire),
			 ztest_unit_test(test_sensor_monitor_force),
			 ztest_unit_test(test_sensor_monitor_stop)
			 );
	ztest_run_test_suite(sensor_monitor_basic_test);
}


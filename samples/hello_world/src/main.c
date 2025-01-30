/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/misc/ppi/nrfx_dppi.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/counter/nrf_counter.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_dppi.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

/* Test is using one counter to periodically generate COMPARE events and
 * second counter device to work in counter mode and count it.
 */
static void counter_test1(const struct device *dev0, const struct device *dev1)
{
	static const struct counter_alarm_cfg alarm_cfg = {
		.callback = (counter_alarm_callback_t)1, // not used but must be non NULL
		.ticks = 1000,
		.flags = COUNTER_ALARM_CFG_NO_IRQ | COUNTER_ALARM_CFG_RESET_COUNTER
	};
	uint8_t ch = 0;
	uint32_t eep = nrf_counter_get_compare_evt_ep(dev0, ch);
	uint32_t tep = nrf_counter_get_tsk_ep(dev1, NRF_COUNTER_TSK_EP_COUNT);
	uint32_t handle;
	uint32_t val;
	int err;

	LOG_WRN("test dev0:%p dev1:%p", dev0, dev1);
	/* Allocate DPPI connection. */
	err = nrf_dppi_conn_alloc(eep, tep, &handle);
	if (err < 0) {
		LOG_ERR("Failed to setup DPPI.");
	}

	counter_set_channel_alarm(dev0, ch, &alarm_cfg);

	nrf_counter_set_mode(dev1, true);
	counter_start(dev1);

	/* Enable DPPI */
	nrf_dppi_conn_ctrl(handle, true);

	/* Start counter that drivers it. */
	counter_start(dev0);

	err = counter_get_value(dev1, &val);
	LOG_INF("dev1:%p counter before start: %d",dev1, val);
	k_msleep(100);

	err = counter_get_value(dev1, &val);
	if (err < 0) {
		LOG_ERR("Failed to get value");
	} else {
		LOG_INF("dev1:%p counter: %d",dev1, val);
	}

	/* Clear and free DPPI */
	nrf_dppi_conn_ctrl(handle, false);
	nrf_dppi_conn_free(eep, tep, handle);

	/* Stop counters. */
	counter_stop(dev0);
	counter_stop(dev1);

	/* Clear counter mode in dev1. */
	nrf_counter_set_mode(dev1, false);
}

#ifdef CONFIG_SOC_SERIES_NRF54LX
const struct device *timer00 = DEVICE_DT_GET(DT_NODELABEL(timer00));
const struct device *timer10 = DEVICE_DT_GET(DT_NODELABEL(timer10));
const struct device *timer20 = DEVICE_DT_GET(DT_NODELABEL(timer20));
const struct device *timer21 = DEVICE_DT_GET(DT_NODELABEL(timer21));

void ppi_test(void)
{
	/* Test various connections. */
	counter_test1(timer20, timer21);

	counter_test1(timer10, timer21);
	counter_test1(timer21, timer10);

	counter_test1(timer00, timer21);
	counter_test1(timer20, timer00);

	counter_test1(timer00, timer10);
	counter_test1(timer10, timer00);
}
#elif CONFIG_SOC_NRF54H20_CPURAD
int nrf_dppi_service_alloc(uint32_t producer, uint32_t consumer, nrf_dppi_handle_t *handle)
{
	*handle = 0;
	return 0;
}
void nrf_dppi_service_free(nrf_dppi_handle_t handle)
{
}
const struct device *timer020 = DEVICE_DT_GET(DT_NODELABEL(timer020));
const struct device *timer021 = DEVICE_DT_GET(DT_NODELABEL(timer021));
void ppi_test(void)
{
	int err;
	uint32_t handle;
	uint32_t eep = (uint32_t)&NRF_TIMER020->EVENTS_COMPARE[0];
	uint32_t tep = (uint32_t)&NRF_ECB030->TASKS_START;
	uint32_t eep2 = (uint32_t)&NRF_TIMER021->EVENTS_COMPARE[0];
	uint32_t eep3 = (uint32_t)&NRF_TIMER120->EVENTS_COMPARE[0];

	counter_test1(timer020, timer021);
	err = nrf_dppi_conn_alloc(eep, tep, &handle);
	if (err < 0) {
		LOG_ERR("Failed to setup DPPI.");
	}

	nrf_dppi_conn_ctrl(handle, true);
	err = nrf_dppi_ep_attach(handle, eep2);
	if (err < 0) {
		LOG_ERR("Failed to attach eep2.");
	}
	nrf_dppi_ep_clear(eep2);

	err = nrf_dppi_ep_attach(handle, eep3);
	if (err != -EINVAL) {
		LOG_ERR("Attaching eep3, unexpected result");
	}

	nrf_dppi_conn_ctrl(handle, false);
	nrf_dppi_conn_free(eep, tep, handle);
}
#endif

int main(void)
{

	ppi_test();
	return 0;
}

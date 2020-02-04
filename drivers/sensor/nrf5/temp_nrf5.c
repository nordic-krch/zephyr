/*
 * Copyright (c) 2016 ARM Ltd.
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <device.h>
#include <drivers/sensor.h>
#include <drivers/clock_control.h>
#include <drivers/clock_control/nrf_clock_control.h>
#include <logging/log.h>
#include <hal/nrf_temp.h>

LOG_MODULE_REGISTER(temp_nrf5, CONFIG_SENSOR_LOG_LEVEL);


/* The nRF5 temperature device returns measurements in 0.25C
 * increments.  Scale to mDegrees C.
 */
#define TEMP_NRF5_TEMP_SCALE (1000000 / 4)

struct temp_nrf5_data {
	struct k_sem device_sync_sem;
	s32_t sample;
	struct onoff_service *clk_srv;
};

static void hfclk_on_callback(struct onoff_service *srv,
			      struct onoff_client *cli,
			      void *user_data,
			      int res)
{
	bool *do_start = user_data;

	/* Callback will be called twice: on start and stop. Use flag to ensure
	 * that sensor is started only on start.
	 */
	if (*do_start) {
		nrf_temp_task_trigger(NRF_TEMP, NRF_TEMP_TASK_START);
		*do_start = false;
	}
}

static int temp_nrf5_sample_fetch(struct device *dev, enum sensor_channel chan)
{
	struct temp_nrf5_data *data = dev->driver_data;
	struct onoff_client cli;
	bool do_start = true;

	int r;

	/* Error if before sensor initialized */
	if (data->clk_srv == NULL) {
		return -EAGAIN;
	}

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_DIE_TEMP) {
		return -ENOTSUP;
	}

	onoff_client_init_callback(&cli, hfclk_on_callback, &do_start);
	r = onoff_request(data->clk_srv, &cli);
	__ASSERT_NO_MSG(r >= 0);

	k_sem_take(&data->device_sync_sem, K_FOREVER);


	r = onoff_release(data->clk_srv, &cli);
	__ASSERT_NO_MSG(!r);

	data->sample = nrf_temp_result_get(NRF_TEMP);
	LOG_DBG("sample: %d", data->sample);
	nrf_temp_task_trigger(NRF_TEMP, NRF_TEMP_TASK_STOP);

	return 0;
}

static int temp_nrf5_channel_get(struct device *dev,
				enum sensor_channel chan,
				struct sensor_value *val)
{
	struct temp_nrf5_data *data = dev->driver_data;
	s32_t uval;


	if (chan != SENSOR_CHAN_DIE_TEMP) {
		return -ENOTSUP;
	}

	uval = data->sample * TEMP_NRF5_TEMP_SCALE;
	val->val1 = uval / 1000000;
	val->val2 = uval % 1000000;

	LOG_DBG("Temperature:%d,%d", val->val1, val->val2);

	return 0;
}

static void temp_nrf5_isr(void *arg)
{
	struct device *dev = (struct device *)arg;
	struct temp_nrf5_data *data = dev->driver_data;

	nrf_temp_event_clear(NRF_TEMP, NRF_TEMP_EVENT_DATARDY);
	k_sem_give(&data->device_sync_sem);
}

static const struct sensor_driver_api temp_nrf5_driver_api = {
	.sample_fetch = temp_nrf5_sample_fetch,
	.channel_get = temp_nrf5_channel_get,
};

DEVICE_DECLARE(temp_nrf5);

static int temp_nrf5_init(struct device *dev)
{
	struct temp_nrf5_data *data = dev->driver_data;
	struct device *clk_dev;

	LOG_DBG("");

	clk_dev = device_get_binding(DT_INST_0_NORDIC_NRF_CLOCK_LABEL);
	__ASSERT_NO_MSG(clk_dev);
	/* A null clk_srv indicates sensor has not been initialized */
	data->clk_srv =
		z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	__ASSERT_NO_MSG(data->clk_srv);

	k_sem_init(&data->device_sync_sem, 0, UINT_MAX);
	IRQ_CONNECT(
		DT_INST_0_NORDIC_NRF_TEMP_IRQ_0,
		DT_INST_0_NORDIC_NRF_TEMP_IRQ_0_PRIORITY,
		temp_nrf5_isr,
		DEVICE_GET(temp_nrf5),
		0);
	irq_enable(DT_INST_0_NORDIC_NRF_TEMP_IRQ_0);

	nrf_temp_int_enable(NRF_TEMP, NRF_TEMP_INT_DATARDY_MASK);

	return 0;
}

static struct temp_nrf5_data temp_nrf5_driver;

DEVICE_AND_API_INIT(temp_nrf5,
		    DT_INST_0_NORDIC_NRF_TEMP_LABEL,
		    temp_nrf5_init,
		    &temp_nrf5_driver,
		    NULL,
		    POST_KERNEL,
		    CONFIG_SENSOR_INIT_PRIORITY,
		    &temp_nrf5_driver_api);

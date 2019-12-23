/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/i2c_ll.h>
#include <drivers/i2c_mngr.h>
#include <dt-bindings/i2c/i2c.h>
#include <drivers/i2c.h>
#include <nrfx_twim.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(i2c_nrfx_twim, CONFIG_I2C_LOG_LEVEL);

struct i2c_ll_nrfx_twim_data {
	struct i2c_mngr mngr;
	i2c_ll_cb_t callback;
	void *user_data;
	u32_t dev_config;
};

BUILD_ASSERT_MSG(offsetof(struct i2c_ll_nrfx_twim_data, mngr) == 0,
				"Manager instance must be the first element");

struct i2c_ll_nrfx_twim_config {
	nrfx_twim_t twim;
	nrfx_twim_config_t config;
};

static inline struct i2c_ll_nrfx_twim_data *get_dev_data(struct device *dev)
{
	return dev->driver_data;
}

static inline
const struct i2c_ll_nrfx_twim_config *get_dev_config(struct device *dev)
{
	return dev->config->config_info;
}

int configure(struct device *dev, u32_t dev_config,
		i2c_ll_cb_t cb, void *user_data)
{
	nrfx_twim_t const *inst = &(get_dev_config(dev)->twim);

	if (cb) {
		get_dev_data(dev)->callback = cb;
		get_dev_data(dev)->user_data = user_data;
	}

	if (dev_config == 0) {
		return 0;
	}

	if (I2C_ADDR_10_BITS & dev_config) {
		return -EINVAL;
	}

	switch (I2C_SPEED_GET(dev_config)) {
	case I2C_SPEED_STANDARD:
		nrf_twim_frequency_set(inst->p_twim, NRF_TWIM_FREQ_100K);
		break;
	case I2C_SPEED_FAST:
		nrf_twim_frequency_set(inst->p_twim, NRF_TWIM_FREQ_400K);
		break;
	default:
		LOG_ERR("unsupported speed");
		return -EINVAL;
	}

	get_dev_data(dev)->dev_config = dev_config;

	return 0;
}

int transfer(struct device *dev, struct i2c_ll_msg *msg, u16_t addr)
{
	int ret = 0;
	nrfx_err_t res;
	nrfx_twim_t const *inst = &(get_dev_config(dev)->twim);
	nrfx_twim_xfer_desc_t xfer = {
		.p_primary_buf  = msg->buf,
		.primary_length = msg->len,
		.address	= addr,
		.type		= (msg->flags & I2C_MSG_READ) ?
					NRFX_TWIM_XFER_RX : NRFX_TWIM_XFER_TX
	};

	nrfx_twim_enable(inst);
	res = nrfx_twim_xfer(inst, &xfer,  (msg->flags & I2C_MSG_STOP) ?
						0 : NRFX_TWIM_FLAG_TX_NO_STOP);
	if (res != NRFX_SUCCESS) {
		nrfx_twim_disable(inst);
		ret = (res == NRFX_ERROR_BUSY) ? -EBUSY : -EIO;
	}

	return ret;
}

static const struct i2c_ll_driver_api i2c_ll_nrfx_twim_api = {
	.configure = configure,
	.transfer  = transfer,
};

static void event_handler(nrfx_twim_evt_t const *p_event, void *p_context)
{
	struct device *dev = p_context;
	int res = (p_event->type == NRFX_TWIM_EVT_DONE) ? 0 : -EIO;

	get_dev_data(dev)->callback(dev, res, get_dev_data(dev)->user_data);
}

static int init_twim(struct device *dev)
{
	nrfx_err_t result = nrfx_twim_init(&get_dev_config(dev)->twim,
					   &get_dev_config(dev)->config,
					   event_handler,
					   dev);
	if (result != NRFX_SUCCESS) {
		LOG_ERR("Failed to initialize device: %s",
			dev->config->name);
		return -EBUSY;
	}

#ifdef CONFIG_DEVICE_POWER_MANAGEMENT
	get_dev_data(dev)->pm_state = DEVICE_PM_ACTIVE_STATE;
#endif

	i2c_mngr_init(&get_dev_data(dev)->mngr, dev, 0);

	return 0;
}

#define I2C_NRFX_TWIM_INVALID_FREQUENCY  ((nrf_twim_frequency_t)-1)
#define I2C_NRFX_TWIM_FREQUENCY(bitrate)				       \
	 (bitrate == I2C_BITRATE_STANDARD ? NRF_TWIM_FREQ_100K		       \
	: bitrate == 250000               ? NRF_TWIM_FREQ_250K		       \
	: bitrate == I2C_BITRATE_FAST     ? NRF_TWIM_FREQ_400K		       \
					  : I2C_NRFX_TWIM_INVALID_FREQUENCY)

#define I2C_LL_NRFX_TWIM_DEVICE(idx)					       \
	BUILD_ASSERT_MSG(						       \
		I2C_NRFX_TWIM_FREQUENCY(				       \
			DT_NORDIC_NRF_TWIM_I2C_##idx##_CLOCK_FREQUENCY)	       \
		!= I2C_NRFX_TWIM_INVALID_FREQUENCY,			       \
		"Wrong I2C " #idx " frequency setting in dts");		       \
	static int twim_##idx##_init(struct device *dev)		       \
	{								       \
		IRQ_CONNECT(DT_NORDIC_NRF_TWIM_I2C_##idx##_IRQ_0,	       \
			    DT_NORDIC_NRF_TWIM_I2C_##idx##_IRQ_0_PRIORITY,     \
			    nrfx_isr, nrfx_twim_##idx##_irq_handler, 0);       \
		return init_twim(dev);					       \
	}								       \
	static struct i2c_ll_nrfx_twim_data twim_##idx##_data;		       \
	static const struct i2c_ll_nrfx_twim_config twim_##idx##z_config = {   \
		.twim = NRFX_TWIM_INSTANCE(idx),			       \
		.config = {						       \
			.scl       = DT_NORDIC_NRF_TWIM_I2C_##idx##_SCL_PIN,   \
			.sda       = DT_NORDIC_NRF_TWIM_I2C_##idx##_SDA_PIN,   \
			.frequency = I2C_NRFX_TWIM_FREQUENCY(		       \
				DT_NORDIC_NRF_TWIM_I2C_##idx##_CLOCK_FREQUENCY)\
		}							       \
	};								       \
	DEVICE_DEFINE(twim_##idx,					       \
		      DT_NORDIC_NRF_TWIM_I2C_##idx##_LABEL,		       \
		      twim_##idx##_init,				       \
		      twim_nrfx_pm_control,				       \
		      &twim_##idx##_data,				       \
		      &twim_##idx##z_config,				       \
		      POST_KERNEL,					       \
		      CONFIG_I2C_INIT_PRIORITY,				       \
		      &i2c_ll_nrfx_twim_api)

#ifdef CONFIG_I2C_0_NRF_TWIM
I2C_LL_NRFX_TWIM_DEVICE(0);
#endif

#ifdef CONFIG_I2C_1_NRF_TWIM
I2C_LL_NRFX_TWIM_DEVICE(1);
#endif

#ifdef CONFIG_I2C_2_NRF_TWIM
I2C_LL_NRFX_TWIM_DEVICE(2);
#endif

#ifdef CONFIG_I2C_3_NRF_TWIM
I2C_LL_NRFX_TWIM_DEVICE(3);
#endif

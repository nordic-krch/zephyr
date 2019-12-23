/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I2C_LL_H__
#define I2C_LL_H__

#include <zephyr.h>
#include <device.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * I2C_MSG_* are I2C Message flags.
 */

/** Write message to I2C bus. */
#define I2C_LL_MSG_WRITE		(0U << 0U)

/** Read message from I2C bus. */
#define I2C_LL_MSG_READ			BIT(0)

/** @cond INTERNAL_HIDDEN */
#define I2C_LL_MSG_RW_MASK		BIT(0)
/** @endcond  */

/** Send STOP after this message. */
#define I2C_LL_MSG_STOP			BIT(1)

/**
 * @brief One I2C Message.
 *
 * This defines one I2C message to transact on the I2C bus.
 *
 * @note Some of the configurations supported by this API may not be
 * supported by specific SoC I2C hardware implementations, in
 * particular features related to bus transactions intended to read or
 * write data from different buffers within a single transaction.
 * Invocations of i2c_transfer() may not indicate an error when an
 * unsupported configuration is encountered.  In some cases drivers
 * will generate separate transactions for each message fragment, with
 * or without presence of @ref I2C_MSG_RESTART in #flags.
 */
struct i2c_ll_msg {
	/** Data buffer in bytes */
	u8_t		*buf;

	/** Length of buffer in bytes */
	u32_t	len;

	/** Flags for this message */
	u8_t		flags;
};

typedef void (*i2c_ll_cb_t)(struct device *dev, int result, void *user_data);

typedef int (*i2c_ll_api_configure_t)(struct device *dev, u32_t dev_config,
					i2c_ll_cb_t cb, void *user_data);

typedef int (*i2c_ll_api_transfer_t)(struct device *dev, struct i2c_ll_msg *msg,
					u16_t addr);

struct i2c_ll_driver_api {
	i2c_ll_api_configure_t configure;
	i2c_ll_api_transfer_t transfer;
};

static inline int i2c_ll_configure(struct device *dev, u32_t dev_config,
				   i2c_ll_cb_t cb, void *user_data)
{
	const struct i2c_ll_driver_api *api =
		(const struct i2c_ll_driver_api *)dev->driver_api;

	return api->configure(dev, dev_config, cb, user_data);
}

static inline int i2c_ll_transfer(struct device *dev, struct i2c_ll_msg *msg,
					u16_t addr)
{
	const struct i2c_ll_driver_api *api =
		(const struct i2c_ll_driver_api *)dev->driver_api;

	return api->transfer(dev, msg, addr);
}

#ifdef __cplusplus
}
#endif

#endif /* I2C_LL_H__ */

/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I2C_MNGR_H__
#define I2C_MNGR_H__

#include <zephyr.h>
#include <drivers/i2c_ll.h>
#include <sys/slist.h>

#ifdef __cplusplus
extern "C" {
#endif

struct i2c_mngr;

typedef void (*i2c_mngr_callback_t)(struct i2c_mngr *mngr, int res,
				    void *user_data);

struct i2c_mngr_transaction {
	sys_snode_t node;
	i2c_mngr_callback_t callback;
	void *user_data;
	u16_t address;
	u8_t num_msgs;
	struct i2c_ll_msg *msgs;
};

struct i2c_mngr {
	sys_slist_t list;
	struct device *dev;
	struct k_spinlock lock;
	struct i2c_mngr_transaction *current;
	u8_t current_idx;
};

int i2c_mngr_init(struct i2c_mngr *mngr, struct device *dev, u32_t dev_config);

int i2c_mngr_configure(struct i2c_mngr *mngr, u32_t dev_config);

int i2c_mngr_schedule(struct i2c_mngr *mngr,
			struct i2c_mngr_transaction *transaction);

#ifdef __cplusplus
}
#endif

#endif /* I2C_MNGR_H__ */

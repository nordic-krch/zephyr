/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <drivers/i2c_mngr.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(i2c_mngr, CONFIG_I2C_LOG_LEVEL);

static int do_next_transfer(struct i2c_mngr *mngr)
{
	return i2c_ll_transfer(mngr->dev,
				&mngr->current->msgs[mngr->current_idx],
				mngr->current->address);

}

/* returns true if there was next transaction pending */
static bool complete_current_get_next(struct i2c_mngr *mngr, int result)
{
	k_spinlock_key_t key;
	sys_snode_t *node;

	mngr->current_idx = 0;
	/* complete */
	mngr->current->callback(mngr, result, mngr->current->user_data);
	key = k_spin_lock(&mngr->lock);
	node = sys_slist_get(&mngr->list);
	mngr->current = CONTAINER_OF(node, struct i2c_mngr_transaction, node);
	k_spin_unlock(&mngr->lock, key);
	if (mngr->current) {
		LOG_DBG("Starting pending transaction %p", mngr->current);
	}

	return (mngr->current != NULL);
}

static void i2c_ll_callback(struct device *dev, int result, void *user_data)
{
	int err;
	struct i2c_mngr *mngr = user_data;

	/* error handle */
	if (result != 0) {
		LOG_WRN("i2c callback err:%d", result);
		if (complete_current_get_next(mngr, result)) {
			goto next;
		}

		return;
	}

	mngr->current_idx++;
	if (mngr->current_idx == mngr->current->num_msgs) {
		LOG_DBG("end of transaction");
		if (complete_current_get_next(mngr, result)) {
			goto next;
		}

		return;
	}

next:
	do {
		err = do_next_transfer(mngr);
		if (err != 0) {
			if (complete_current_get_next(mngr, err) == false) {
				break;
			}
		}
	} while (err != 0);
}

int i2c_mngr_init(struct i2c_mngr *mngr, struct device *dev, u32_t dev_config)
{
	mngr->dev = dev;
	sys_slist_init(&mngr->list);
	i2c_ll_configure(dev, dev_config, i2c_ll_callback, mngr);

	return 0;
}

int i2c_mngr_configure(struct i2c_mngr *mngr, u32_t dev_config)
{
	return i2c_ll_configure(mngr->dev, dev_config, NULL, NULL);
}

int i2c_mngr_schedule(struct i2c_mngr *mngr,
			struct i2c_mngr_transaction *transaction)
{
	bool trigger = false;
	k_spinlock_key_t key = k_spin_lock(&mngr->lock);
	int err = 0;

	if (mngr->current == NULL) {
		mngr->current = transaction;
		trigger = true;
	} else {
		sys_slist_append(&mngr->list, &transaction->node);
	}
	k_spin_unlock(&mngr->lock, key);

	if (trigger) {
		mngr->current_idx = 0;
		err = do_next_transfer(mngr);
	}

	LOG_DBG("transaction scheduled %s(err:%d)",
		trigger ? "and started" : "", err);

	return err;
}

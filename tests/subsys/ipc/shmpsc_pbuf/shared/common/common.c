/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "common.h"
#include <zephyr/kernel.h>
#include <zephyr/cache.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/mbox.h>
LOG_MODULE_REGISTER(common, LOG_LEVEL_INF);

#define DT_DRV_COMPAT zephyr_ipc_icbmsg

struct shmpsc_pbuf *buffer_init(struct k_spinlock *lock, uint32_t *buf,
			       uint32_t size, bool tx)
{
	if (tx) {
		memset(buf, 0, size);
		sys_cache_data_flush_range((void *)buf, size);
	}

	struct shmpsc_pbuf *pb = (struct shmpsc_pbuf *)buf;
	uint32_t space_size = size - sizeof(*pb);
	uint32_t *space = (uint32_t *)((uintptr_t)buf + sizeof(*pb));
	struct shmpsc_pbuf_config mpsc_buf_cfg = {
		.buf = space,
		.size = space_size,
		.lock = lock
	};

	if (tx) {
		shmpsc_pbuf_init(pb, &mpsc_buf_cfg);
	} else {
		bool cont;
		int wdog_cnt = 100;

		do {
			sys_cache_data_invd_range((void *)pb, sizeof(*pb));
			cont = (pb->buf != space);
			if (cont) {
				k_msleep(1);
			}
			wdog_cnt--;
		} while (cont && wdog_cnt);

		if (!wdog_cnt) {
			return NULL;
		}
	}

	return pb;
}

static void mbox_callback(const struct device *instance, uint32_t channel,
			  void *user_data, struct mbox_msg *msg_data)
{
	struct k_sem *sem = user_data;

	LOG_INF("mbox cb:%d", channel);
	k_sem_give(sem);
}

int sync(void)
{
	struct mbox_dt_spec mbox_tx = MBOX_DT_SPEC_INST_GET(0, tx);
	struct mbox_dt_spec mbox_rx = MBOX_DT_SPEC_INST_GET(0, rx);
	struct k_sem sem;
	int ret;

	k_sem_init(&sem, 0, 1);

	LOG_INF("mbox tx: %d", mbox_tx.channel_id);
	LOG_INF("mbox rx: %d", mbox_rx.channel_id);
	ret = mbox_send_dt(&mbox_tx, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to send mbox signal");
		return ret;
	}

	ret = mbox_register_callback_dt(&mbox_rx, mbox_callback, &sem);
	if (ret < 0) {
		LOG_ERR("Failed register callback");
		return ret;
	}

	ret = mbox_set_enabled_dt(&mbox_rx, true);
	if (ret < 0) {
		LOG_ERR("Failed enable rx signal");
		return ret;
	}

	ret = k_sem_take(&sem, K_MSEC(1000));
	if (ret < 0) {
		LOG_ERR("Failed when waiting for mbox response");
		return ret;
	}

	return 0;
}

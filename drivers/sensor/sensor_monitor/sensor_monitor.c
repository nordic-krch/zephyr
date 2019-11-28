/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sensor_monitor.h>

void z_sensor_monitor_work_handler(struct k_work *work)
{
	struct sensor_monitor_ctrl_blk *ctrl_blk =
		CONTAINER_OF(work, struct sensor_monitor_ctrl_blk, work);
	const struct sensor_monitor *monitor = ctrl_blk->monitor;
	struct sensor_value value;
	int err;

	err = sensor_sample_fetch(monitor->ctrl_blk->sensor);
	__ASSERT_NO_MSG(err == 0);

	err = sensor_channel_get(monitor->ctrl_blk->sensor, monitor->channel,
				 &value);
	__ASSERT_NO_MSG(err == 0);

	if (monitor->test_func(monitor, &value, monitor->ctrl_blk->user_data)) {
		monitor->cb(monitor, SENSOR_MONITOR_REASON_FORCED,
				monitor->ctrl_blk->user_data);
		monitor->ctrl_blk->skip_cnt = 0;
	} else if (monitor->max_skip > 0){
		monitor->ctrl_blk->skip_cnt++;
		if (monitor->ctrl_blk->skip_cnt > monitor->max_skip) {
			monitor->ctrl_blk->skip_cnt = 0;
			monitor->cb(monitor, SENSOR_MONITOR_REASON_EXPIRY,
					monitor->ctrl_blk->user_data);
		}
	}

	err = k_delayed_work_submit(&monitor->ctrl_blk->work,
				    K_MSEC(monitor->period_ms));
	__ASSERT_NO_MSG(err == 0);
}

int sensor_monitor_start(const struct sensor_monitor *monitor)
{
	if (monitor->ctrl_blk->sensor == NULL) {
		monitor->ctrl_blk->sensor =
				device_get_binding(monitor->sensor_name);
	}

	monitor->ctrl_blk->skip_cnt = 0;
	k_work_submit(&monitor->ctrl_blk->work.work);

	return 0;
}

int sensor_monitor_stop(const struct sensor_monitor *monitor)
{
	return k_delayed_work_cancel(&monitor->ctrl_blk->work);
}

/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SENSOR_MONITOR_H_
#define ZEPHYR_INCLUDE_SENSOR_MONITOR_H_

#include <device.h>
#include <kernel.h>
#include <drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Action trigger reason. */
enum sensor_monitor_reason {
	SENSOR_MONITOR_REASON_EXPIRY, /* Trigger due to exceeding skips limit */
	SENSOR_MONITOR_REASON_FORCED  /* Trigger due to test function result. */
};

struct sensor_monitor;

/** @brief Prototype of action callback.
 *
 * @param monitor	Monitor instance.
 * @param reason	Action trigger reason.
 * @param user_data	User data.
 */
typedef void (*sensor_monitor_cb_t)(const struct sensor_monitor *monitor,
				    enum sensor_monitor_reason reason,
				    void *user_data);

/** @brief Prototype of test function.
 *
 * @param monitor	Monitor instance.
 * @param value		Measured value.
 * @param user_data	User data.
 *
 * @return True if action should be triggered, false otherwise.
 */
typedef bool (*sensor_monitor_test_func_t)(const struct sensor_monitor *monitor,
					   struct sensor_value *value,
					   void *user_data);

/** @brief Monitor control block. */
struct sensor_monitor_ctrl_blk {
	struct device *sensor;
	struct k_delayed_work work;
	const struct sensor_monitor *monitor;
	void *user_data;
	u16_t skip_cnt;
};

/** @brief Sensor monitor instance. */
struct sensor_monitor {
	const char *sensor_name;
	enum sensor_channel channel;
	u16_t period_ms;
	u16_t max_skip;
	sensor_monitor_cb_t cb;
	sensor_monitor_test_func_t test_func;
	struct sensor_monitor_ctrl_blk *ctrl_blk;
};

void z_sensor_monitor_timer_func(struct k_timer *timer);
void z_sensor_monitor_work_handler(struct k_work *work);

/** @brief Create sensor monitor instance.
 *
 * Macro is used internally. Use @ref SENSOR_MONITOR_STATIC_DEFINE or
 * @ref SENSOR_MONITOR_DEFINE.
 *
 * @param _name		Instance name.
 * @param _sensor_dev	Name of monitored sensor.
 * @param _channel	Monitored channel.
 * @param _period_ms	Frequency of sensor channel sampling.
 * @param _max_skip	Maximum number of skips which will trigger action
 *			callback. Set to 0 to disable.
 * @param _test_func	Decision function triggered on every sample. See
 *			@ref sensor_monitor_test_func_t.
 * @param _callback	Action callback triggered based on decision or exceeding
 *			skips limit.
 * @param _user_data	User data passed to callbacks.
 */
#define Z_SENSOR_MONITOR_DEFINE(_const, _name, _sensor_name, _channel, \
			       _period_ms, _max_skip, _test_func, _callback, \
			       _user_data) \
	static IF_ENABLED(_const, (const)) struct sensor_monitor _name; \
	static struct sensor_monitor_ctrl_blk _name##_ctrl_blk= { \
		.user_data = _user_data, \
		.work = { \
		  .work = Z_WORK_INITIALIZER(z_sensor_monitor_work_handler), \
		}, \
		.monitor = &_name \
	}; \
	static IF_ENABLED(_const, (const)) struct sensor_monitor _name = {\
		.sensor_name = _sensor_name, \
		.channel = _channel, \
		.period_ms = _period_ms, \
		.max_skip = _max_skip, \
		.cb = _callback, \
		.test_func = _test_func, \
		.ctrl_blk = &_name##_ctrl_blk, \
	}

/** @brief Create static instance of sensor monitor.
 *
 * Static instance has fixed parameters like callbacks or timings since its
 * configuration is const. See @ref Z_SENSOR_MONITOR_DEFINE for details.
 */
#define SENSOR_MONITOR_STATIC_DEFINE(_name, _sensor_dev, _channel, \
				     _period_ms, _max_skip, _test_func, \
				     _callback, _user_data) \
	Z_SENSOR_MONITOR_DEFINE(1, _name, _sensor_dev, _channel, _period_ms, \
				_max_skip, _test_func, _callback, _user_data)

/** @brief Create instance of sensor monitor.
 *
 * See @ref Z_SENSOR_MONITOR_DEFINE for details.
 */
#define SENSOR_MONITOR_DEFINE(_name, _sensor_dev, _channel, \
			      _period_ms, _max_skip, _test_func, _callback, \
			      _user_data) \
	Z_SENSOR_MONITOR_DEFINE(0, _name, _sensor_dev, _channel, _period_ms, \
				_max_skip, _test_func, _callback, _user_data)

/** @brief Start sensor monitoring.
 *
 * When started, first measurement will be triggered immediately followed by
 * periodical measurements. After each measurement test callback is called.
 * Test function returns the decision if action shall be triggered or skipped.
 * When number of skips exceeds configured limit then action is forced.
 *
 * @param monitor	Monitor instance.
 *
 * @return 0 or error code on failure.
 */
int sensor_monitor_start(const struct sensor_monitor *monitor);

/** @brief Stop sensor monitoring.
 *
 * @param monitor	Monitor instance.
 *
 * @retval 0 on success.
 * @retval -EINVAL if stopped before starting.
 */
int sensor_monitor_stop(const struct sensor_monitor *monitor);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SENSOR_MONITOR_H_ */

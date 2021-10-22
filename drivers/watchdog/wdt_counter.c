/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <drivers/watchdog.h>
#include <drivers/counter.h>
#include <logging/log_ctrl.h>

#define WDT_CHANNEL_COUNT DT_PROP(DT_WDT_COUNTER, ch_num)

#define DT_WDT_COUNTER DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_counter_watchdog)

extern void sys_arch_reboot(int type);

struct wdt_counter_data {
	wdt_callback_t callback[WDT_CHANNEL_COUNT];
	uint32_t timeout[WDT_CHANNEL_COUNT];
	uint8_t alloc_mask;
	uint8_t feed_mask;
};

struct wdt_counter_config {
	const struct device *counter;
};

static inline struct wdt_counter_data *get_dev_data(const struct device *dev)
{
	return dev->data;
}

static inline const struct wdt_counter_config *get_dev_config(const struct device *dev)
{
	return dev->config;
}

static int wdt_counter_setup(const struct device *dev, uint8_t options)
{
	const struct device *counter = get_dev_config(dev)->counter;

	if ((options & WDT_OPT_PAUSE_IN_SLEEP) || (options & WDT_OPT_PAUSE_IN_SLEEP)) {
		return -ENOTSUP;
	}

	return counter_start(counter);
}

static int wdt_counter_disable(const struct device *dev)
{
	const struct device *counter = get_dev_config(dev)->counter;

	return counter_stop(counter);
}

static void counter_alarm_callback(const struct device *dev,
				   uint8_t chan_id, uint32_t ticks,
				   void *user_data)
{
	const struct device *wdt_dev = user_data;
	struct wdt_counter_data *data = get_dev_data(wdt_dev);

	counter_stop(dev);
	if (data->callback[chan_id]) {
		data->callback[chan_id](wdt_dev, chan_id);
	}

	LOG_PANIC();
	sys_arch_reboot(0);
}

static int timeout_set(const struct device *dev, int chan_id, bool cancel)
{
	struct wdt_counter_data *data = get_dev_data(dev);
	const struct device *counter = get_dev_config(dev)->counter;
	struct counter_alarm_cfg alarm_cfg = {
		.callback = counter_alarm_callback,
		.ticks = data->timeout[chan_id],
		.user_data = (void *)dev,
		.flags = 0
	};

	if (cancel) {
		int err = counter_cancel_channel_alarm(counter, chan_id);

		if (err < 0) {
			return err;
		}
	}

	return counter_set_channel_alarm(counter, chan_id, &alarm_cfg);
}

static int wdt_counter_install_timeout(const struct device *dev,
				   const struct wdt_timeout_cfg *cfg)
{
	struct wdt_counter_data *data = get_dev_data(dev);
	const struct wdt_counter_config *config = get_dev_config(dev);
	const struct device *counter = config->counter;
	int chan_id;
	uint32_t max_timeout = counter_get_top_value(counter) -
				counter_get_guard_period(counter,
				COUNTER_GUARD_PERIOD_LATE_TO_SET);
	uint32_t timeout_ticks = counter_us_to_ticks(counter, cfg->window.max * 1000);

	if (cfg->flags != WDT_FLAG_RESET_SOC) {
		return -ENOTSUP;
	}

	if (cfg->window.min != 0U) {
		return -EINVAL;
	}

	if (timeout_ticks > max_timeout || timeout_ticks == 0) {
		return -EINVAL;
	}

	if (!data->alloc_mask) {
		return -ENOMEM;
	}

	chan_id = 31 - __builtin_clz(data->alloc_mask);
	data->alloc_mask &= ~BIT(chan_id);
	data->timeout[chan_id] = timeout_ticks;
	data->callback[chan_id] = cfg->callback;

	int err = timeout_set(dev, chan_id, false);
	if (err < 0) {
		return err;
	}

	return chan_id;
}

static int wdt_counter_feed(const struct device *dev, int chan_id)
{
	if (chan_id > counter_get_num_of_channels(get_dev_config(dev)->counter)) {
		return -EINVAL;
	}

	/* Set alarm further in the future. */
	return timeout_set(dev, chan_id, true);
}

static const struct wdt_driver_api wdt_counter_driver_api = {
	.setup = wdt_counter_setup,
	.disable = wdt_counter_disable,
	.install_timeout = wdt_counter_install_timeout,
	.feed = wdt_counter_feed,
};

static const struct wdt_counter_config wdt_counter_config = {
	.counter = DEVICE_DT_GET(DT_PHANDLE(DT_WDT_COUNTER, counter)),
};

static struct wdt_counter_data wdt_counter_data;

static int wdt_counter_init(const struct device *dev)
{
	struct wdt_counter_data *data = get_dev_data(dev);
	uint8_t ch_cnt = counter_get_num_of_channels(get_dev_config(dev)->counter);

	if (ch_cnt < WDT_CHANNEL_COUNT) {
		return -EINVAL;
	}

	data->alloc_mask = BIT_MASK(WDT_CHANNEL_COUNT);

	return 0;
}

DEVICE_DT_DEFINE(DT_WDT_COUNTER, wdt_counter_init, NULL,
		 &wdt_counter_data, &wdt_counter_config,
		 POST_KERNEL,
		 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		 &wdt_counter_driver_api);


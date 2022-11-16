/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/counter.h>

extern int z_nrf_rtc_timer_trigger_overflow(void);
static const struct device *counter_dev = DEVICE_DT_GET(DT_NODELABEL(rtc2));
static struct counter_alarm_cfg alarm_cfg;
static volatile bool flag;

static void test_counter_interrupt_fn(const struct device *counter_dev,
				      uint8_t chan_id, uint32_t ticks,
				      void *user_data)
{
	if (flag == false) {
		printk("Failed\n");
	} else {
		printk("wdg ok\n");
	}
}

void main(void)
{
	printk("Hello World! %s\n", CONFIG_BOARD);
	int init_rpt = 656;
	int rpt = init_rpt;
	int err;

	/* Trigger overflow multiple times (for than 256) to get to the range
	 * beyond 32 bits.
	 */
	do {
		int err = z_nrf_rtc_timer_trigger_overflow();
		if (err < 0) {
			printk("trigger failed\n");
		}
		k_busy_wait(2000);

		if (0) {
			printk("now:%llu ticks:%lld (0x%llx)\n",
					k_uptime_get(),
					sys_clock_tick_get(),
					sys_clock_tick_get());
		}
		k_msleep(10);
		/*printk("now:%llu\n", k_uptime_get());*/
		k_busy_wait(1000);

	} while (rpt--);
	printk("prep done ticks 0x%llx\n", sys_clock_tick_get());

	/* Once we are in time beyond 32 bits setup redundant timer on RTC2
	 * which will validate that system timer expires on time. Then go to
	 * sleep for requested time.
	 */
	counter_start(counter_dev);

#define TIMEOUT_MS 60000
	printk("setting timeout to %d s\n", TIMEOUT_MS/1000);
	alarm_cfg.flags = 0;
	alarm_cfg.ticks = counter_us_to_ticks(counter_dev, (TIMEOUT_MS + 1000)*1000);
	alarm_cfg.callback = test_counter_interrupt_fn;
	alarm_cfg.user_data = &alarm_cfg;

	err = counter_set_channel_alarm(counter_dev, 0, &alarm_cfg);
	if (err < 0) printk("failed\n");

	k_msleep(TIMEOUT_MS);
	flag = true;
	printk("ok\n");
	while(1);
}

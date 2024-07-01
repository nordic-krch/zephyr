/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <hal/nrf_gpio.h>
LOG_MODULE_REGISTER(app);

const struct device *timer1 = DEVICE_DT_GET(DT_NODELABEL(dut));

#define MIN_TIMEOUT_US 200

struct counter_alarm_cfg alarm_cfg1;
struct counter_alarm_cfg alarm_cfg2;

#define DBG_PINS 1

#define PIN1 0
#define PIN2 1
#define PORT 9
#define PORT_REG NRF_P9


static void timeout_handler(struct k_timer *timer)
{
#if DBG_PINS
        PORT_REG->OUTSET = BIT(PIN1);
#endif
        uint32_t r = sys_rand32_get();
        uint32_t wait = r & 0x1F;
        uint32_t timeout = MIN_TIMEOUT_US + (r >> 24);

        k_busy_wait(wait);
        k_timer_start(timer, K_USEC(timeout),  K_NO_WAIT);
#if DBG_PINS
        PORT_REG->OUTCLR = BIT(PIN1);
#endif
}

K_TIMER_DEFINE(timer, timeout_handler, NULL);

static void set_next_alarm(const struct device *dev, struct counter_alarm_cfg *cfg)
{
#if DBG_PINS
        PORT_REG->OUTSET = BIT(PIN2);
#endif
        uint32_t r = sys_rand8_get();

	cfg->ticks = counter_us_to_ticks(dev, MIN_TIMEOUT_US + r);

	int err = counter_set_channel_alarm(dev, 0, cfg);
        (void)err;

#if DBG_PINS
        PORT_REG->OUTCLR = BIT(PIN2);
#endif
}


static void test_counter_interrupt_fn(const struct device *counter_dev,
				      uint8_t chan_id, uint32_t ticks,
				      void *user_data)
{
        set_next_alarm(counter_dev, (struct counter_alarm_cfg *)user_data);
}

int main(void)
{
#if DBG_PINS
        nrf_gpio_cfg_output(32*PORT+PIN1);
        nrf_gpio_cfg_output(32*PORT+PIN2);
#endif
	counter_start(timer1);

	alarm_cfg1.flags = 0;
	alarm_cfg1.callback = test_counter_interrupt_fn;
	alarm_cfg1.user_data = &alarm_cfg1;

        set_next_alarm(timer1, &alarm_cfg1);
        k_timer_start(&timer, K_USEC(500), K_NO_WAIT);

        int cnt = 0;
	while (1) {
                LOG_INF("ping %d", cnt++);
		k_msleep(1000);
	}

	return 0;
}

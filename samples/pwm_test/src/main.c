/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <nrfx_pwm.h>
#include <hal/nrf_pwm.h>
#include <nrfx_timer.h>
#include <zephyr/irq.h>
#include <helpers/nrfx_gppi.h>
#include <haly/nrfy_gpio.h>

#define OUTPUT_PIN DT_GPIO_PIN(DT_ALIAS(led2), gpios)

#ifdef CONFIG_SOC_NRF54H20_CPUAPP
#define PWM_INSTANCE 130
#define TIMER_INSTANCE 130
#define PWM_LABEL pwm130
#else
#define PWM_INSTANCE 20
#define TIMER_INSTANCE 20
#define PWM_LABEL pwm20
#endif
static const nrfx_pwm_t pwm_instance = NRFX_PWM_INSTANCE(PWM_INSTANCE);
static nrf_pwm_values_common_t pwm_val[] = {0x500, 0x500, 0x500, 0x500, 0x500, 0x500, 0x500, 0x500, 0x500, 0x500};
static nrf_pwm_sequence_t seq = {
	.values = {pwm_val},
	.length = ARRAY_SIZE(pwm_val),
	.repeats = 0,
	.end_delay = 0
};


static void pwm_isr(void)
{
	static uint32_t evt_counter;

	// pwm_instance.p_reg->EVENTS_COMPAREMATCH[0] = 0;
	printk("PWM compare match count: %d\n", ++evt_counter);
}

static uint32_t pwm_init(void)
{
	nrfx_err_t err;

	nrfx_pwm_config_t pwm_config =
		NRFX_PWM_DEFAULT_CONFIG(OUTPUT_PIN, NRF_PWM_PIN_NOT_CONNECTED,
					NRF_PWM_PIN_NOT_CONNECTED, NRF_PWM_PIN_NOT_CONNECTED);

	pwm_config.top_value = 0x1000;
	/* Connect pwm130 IRQ to nrfx_pwm_irq_handler */
	IRQ_CONNECT(DT_IRQN(DT_NODELABEL(PWM_LABEL)), DT_IRQ(DT_NODELABEL(PWM_LABEL), priority), nrfx_isr,
		    pwm_isr, 0);

	err = nrfx_pwm_init(&pwm_instance, &pwm_config, NULL, NULL);
	if (err != NRFX_SUCCESS) {
		printk("nrfx_pwm_init() failed. (err 0x%x)\n", err);
		return -1;
	}

	// nrfy_pwm_int_set(pwm_instance.p_reg, PWM_INTENSET_COMPAREMATCH0_Msk);

	return (uint32_t)&pwm_instance.p_reg->EVENTS_COMPAREMATCH[0];
}

static uint32_t timer_init(void)
{
	nrfx_err_t err;
	static const nrfx_timer_t timer_instance = NRFX_TIMER_INSTANCE(TIMER_INSTANCE);

	uint32_t base_frequency = NRF_TIMER_BASE_FREQUENCY_GET(timer_instance.p_reg);
    nrfx_timer_config_t timer_config = NRFX_TIMER_DEFAULT_CONFIG(base_frequency);
	timer_config.bit_width = NRF_TIMER_BIT_WIDTH_16;
    timer_config.mode = NRF_TIMER_MODE_COUNTER;

	printk("Timer base frequency: %d Hz\n", base_frequency);

	err = nrfx_timer_init(&timer_instance, &timer_config, NULL);
	if (err != NRFX_SUCCESS) {
		printk("nrfx_timer_init() failed. (err 0x%x)\n", err);
		return -1;
	}

	nrfx_timer_enable(&timer_instance);

	return nrfx_timer_task_address_get(&timer_instance, NRF_TIMER_TASK_COUNT);
}

int main(void)
{
	nrfx_err_t nrfx_err;
	uint32_t timer_task;
	uint32_t pwm_event;
	uint8_t ppi_channel;

    nrfy_gpio_cfg_output(OUTPUT_PIN);
    nrfy_gpio_pin_set(OUTPUT_PIN);

	pwm_event = pwm_init();
	timer_task = timer_init();

	nrfx_err = nrfx_gppi_channel_alloc(&ppi_channel);
	if (nrfx_err != NRFX_SUCCESS) {
		printk("nrfx_gppi_channel_alloc error: 0x%08X", nrfx_err);
		return 0;
	}

	printk("Using PPI channel %d\n", ppi_channel);

	nrfx_gppi_channel_endpoints_setup(ppi_channel, pwm_event, timer_task);
	nrfx_gppi_channels_enable(BIT(ppi_channel));


	static const nrfx_timer_t timer_instance = NRFX_TIMER_INSTANCE(TIMER_INSTANCE);

    while (1) {
        nrfx_timer_capture(&timer_instance, 0);
        uint32_t before = nrfx_timer_capture_get(&timer_instance, 0);

	    nrfx_pwm_simple_playback(&pwm_instance, &seq, 1, NRFX_PWM_FLAG_STOP);

        k_msleep(10);

        nrfx_timer_capture(&timer_instance, 0);
        uint32_t after = nrfx_timer_capture_get(&timer_instance, 0);

        printk("before: %u after %u delta %u\n", before, after, after - before);

        k_msleep(1000);

    }

	return 0;
}

/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

#include <zephyr/irq.h>
#include <nrfx_gpiote.h>
#include <nrfx_timer.h>
#include <helpers/nrfx_gppi.h>
LOG_MODULE_REGISTER(app);

static const nrfx_gpiote_t gpiote = NRFX_GPIOTE_INSTANCE(130);
static const nrfx_timer_t timer = NRFX_TIMER_INSTANCE(131);

#define TEST_PIN (9 * 32)

/* Setup output pin to use task for toggling. Function returns address of the
 * task. 0 return value means error.
 */
static uint32_t gpiote_setup(void)
{
	uint8_t gpiote_ch;
	nrfx_err_t err;

	err = nrfx_gpiote_channel_alloc(&gpiote, &gpiote_ch);
	if (err != NRFX_SUCCESS) {
		printf("failed to allocate gpiote\n");
		return 0;
	}

	nrfx_gpiote_task_config_t task_config = {
		.task_ch = gpiote_ch,
		.polarity = NRF_GPIOTE_POLARITY_TOGGLE,
		.init_val = NRF_GPIOTE_INITIAL_VALUE_LOW
	};
	nrfx_gpiote_output_config_t out_config = {
		.drive = NRF_GPIO_PIN_S0S1,
		.input_connect = NRF_GPIO_PIN_INPUT_DISCONNECT,
		.pull = NRF_GPIO_PIN_NOPULL
	};

	err = nrfx_gpiote_output_configure(&gpiote, TEST_PIN, &out_config, &task_config);
	if (err != NRFX_SUCCESS) {
		printf("failed to configure pin\n");
		return 0;
	}

	nrfx_gpiote_out_task_enable(&gpiote, TEST_PIN);

	uint32_t addr = nrfx_gpiote_out_task_address_get(&gpiote, TEST_PIN);

	return addr;
}

static void nrfx_timer_event_handler(nrf_timer_event_t event_type, void * p_context)
{
}

/* Setup timer to generate periodic events. Return address of the EVENT register
 * to be used by PPI. 0 return value means error.
 *
 * @param period_us Period in microseconds.
 */
uint32_t timer_setup(uint32_t period_us)
{
	nrfx_err_t err;
	nrfx_timer_config_t config = NRFX_TIMER_DEFAULT_CONFIG(1000000);

	config.bit_width = NRF_TIMER_BIT_WIDTH_32;

	err = nrfx_timer_init(&timer, &config, nrfx_timer_event_handler);
	if (err != NRFX_SUCCESS) {
		return 0;
	}

	uint32_t ticks = nrfx_timer_us_to_ticks(&timer, period_us);

	nrfx_timer_extended_compare(&timer, NRF_TIMER_CC_CHANNEL5, ticks,
			NRF_TIMER_SHORT_COMPARE5_CLEAR_MASK, false);


	return nrfx_timer_compare_event_address_get(&timer, 5);
}

void timer_start(void)
{
	nrfx_timer_enable(&timer);
}

int main(void)
{

	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	uint32_t addr = gpiote_setup();

	if (addr == 0) {
		printf("gpiote setup failed\n");
		return 0;
	}

	uint32_t evt_addr = timer_setup(100000);

	if (evt_addr == 0) {
		printf("timer setup failed\n");
		return 0;
	}

	uint8_t ch;
	nrfx_err_t err = nrfx_gppi_channel_alloc(&ch);
	if (err != NRFX_SUCCESS) {
		printf("failed to allocate gppi\n");
		return 0;
	}

	nrfx_gppi_channel_endpoints_setup(ch, evt_addr, addr);

	/* Start timer to generate periodic compare events. */
	timer_start();

	/* enable connect. TODO Note that currently it is not needed as connection is
	 * enabled immediately after endpoints setup but this is not an expected behavior.
	 */
	nrfx_gppi_channels_enable(BIT(ch));

	printf("PPI connection configured and running");

	k_sleep(K_FOREVER);

	return 0;
}

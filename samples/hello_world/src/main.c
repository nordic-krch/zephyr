/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/nrf_gpio.h>
#include <soc/nrfx_coredep.h>
#include <nrfx_uart.h>

nrfx_uart_t uart = NRFX_UART_INSTANCE(0);
static uint8_t buf[] = "test data";

void uart_event_handler(nrfx_uart_event_t const *p_event, void *p_context)
{

}

static void uart_demo(void)
{
	nrfx_err_t err;
	nrfx_uart_config_t config = NRFX_UART_DEFAULT_CONFIG(6, 8);


	err = nrfx_uart_init(&uart, &config, uart_event_handler);
	if (err != NRFX_SUCCESS) {
		return;
	}

	err = nrfx_uart_tx(&uart, buf, sizeof(buf));
	if (err != NRFX_SUCCESS) {
		return;
	}
}

void main(void)
{
	uart_demo();

	nrf_gpio_cfg_output(17);

	while(1) {
		nrfx_coredep_delay_us(100000);
		nrf_gpio_pin_toggle(17);
	}
}

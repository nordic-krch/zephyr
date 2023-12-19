/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/uart.h>

static const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	switch (evt->type) {
	case UART_TX_DONE:
		log_frontend_dict_tx_from_cb();
		break;
	default:
		break;
	}
}

int log_frontend_dict_init(void)
{
	int err;

	if (!device_is_ready(uart)) {
		return -EAGAIN;
	}

	if (!IS_ENABLED(CONFIG_UART_ASYNC_API)) {
		return uart_callback_set(uart_dev, uart_callback, NULL);
	} else {
		return 0;
	}
}

int log_frontend_dict_tx_blocking(const uint8_t *buf, size_t len, bool panic)
{
	ARG_UNUSED(panic);

	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart, buf[i]);
	}

	return 0;
}

int log_frontend_dict_tx_async(const uint8_t *buf, size_t len)
{
	return uart_tx(uart, buf, len, SYS_FOREVER_US);
}

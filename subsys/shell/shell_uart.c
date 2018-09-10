/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <shell/shell_uart.h>
#include <uart.h>
#include <logging/log.h>

#define LOG_MODULE_NAME shell_uart
LOG_MODULE_REGISTER();

static void timer_handler(struct k_timer *timer)
{
	u8_t c;
	struct shell_uart *sh_uart = k_timer_user_data_get(timer);

	while (uart_poll_in(sh_uart->ctrl_blk->dev, &c) == 0) {
		if (sys_ring_buf_raw_put(sh_uart->rx_ringbuf, &c, 1) == 0) {
			/* ring buffer full. */
			LOG_WRN("RX ring buffer full.");
		}
		sh_uart->ctrl_blk->handler(SHELL_TRANSPORT_EVT_RX_RDY,
					   sh_uart->ctrl_blk->context);
	}
}


static int init(const struct shell_transport *transport,
		const void *config,
		shell_transport_handler_t evt_handler,
		void *context)
{
	struct shell_uart *sh_uart = (struct shell_uart *)transport->ctx;

	sh_uart->ctrl_blk->dev = (struct device *)config;
	sh_uart->ctrl_blk->handler = evt_handler;
	sh_uart->ctrl_blk->context = context;

	k_timer_init(sh_uart->timer, timer_handler, NULL);
	k_timer_user_data_set(sh_uart->timer, sh_uart);

	k_timer_start(sh_uart->timer, 20, 20);

	return 0;
}

static int uninit(const struct shell_transport *transport)
{
	return 0;
}

static int enable(const struct shell_transport *transport, bool blocking)
{
	return 0;
}

static int write(const struct shell_transport *transport,
		 const void *data, size_t length, size_t *cnt)
{
	struct shell_uart *sh_uart = (struct shell_uart *)transport->ctx;
	const u8_t *data8 = (const u8_t *)data;

	for (size_t i = 0; i < length; i++) {
		uart_poll_out(sh_uart->ctrl_blk->dev, data8[i]);
	}

	*cnt = length;

	sh_uart->ctrl_blk->handler(SHELL_TRANSPORT_EVT_TX_RDY,
				   sh_uart->ctrl_blk->context);

	return 0;
}

static int read(const struct shell_transport *transport,
		void *data, size_t length, size_t *cnt)
{
	struct shell_uart *sh_uart = (struct shell_uart *)transport->ctx;

	*cnt = sys_ring_buf_raw_get(sh_uart->rx_ringbuf, data, length);

	return 0;
}

const struct shell_transport_api shell_uart_transport_api = {
	.init = init,
	.uninit = uninit,
	.enable = enable,
	.write = write,
	.read = read
};

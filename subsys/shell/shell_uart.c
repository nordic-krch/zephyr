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

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
static void uart_rx_handle(const struct shell_uart *sh_uart)
{
	u8_t *data;
	u32_t len;
	bool new_data = false;

	do {
		len = sys_ring_buf_bytes_alloc(sh_uart->rx_ringbuf, &data,
					       sh_uart->rx_ringbuf->size);
		len = uart_fifo_read(sh_uart->ctrl_blk->dev, data, len);
		new_data = (len != 0);
		sys_ring_buf_bytes_put(sh_uart->rx_ringbuf, len);
	} while (len);

	if (new_data) {
		sh_uart->ctrl_blk->handler(SHELL_TRANSPORT_EVT_RX_RDY,
					   sh_uart->ctrl_blk->context);
	}
}

static void uart_tx_handle(const struct shell_uart *sh_uart)
{
	u32_t len;
	u8_t *data;
	int err;
	struct device *dev = sh_uart->ctrl_blk->dev;

	len = sys_ring_buf_bytes_get(sh_uart->tx_ringbuf, &data,
				     sh_uart->tx_ringbuf->size);
	if (len) {
		len = uart_fifo_fill(dev, data, len);
		err = sys_ring_buf_bytes_free(sh_uart->tx_ringbuf, len);
		assert(err == 0);
	} else {
		uart_irq_tx_disable(dev);
		sh_uart->ctrl_blk->tx_busy = 0;
	}

	sh_uart->ctrl_blk->handler(SHELL_TRANSPORT_EVT_TX_RDY,
				   sh_uart->ctrl_blk->context);
}

static void uart_callback(void *user_data)
{
	const struct shell_uart *sh_uart = (struct shell_uart *)user_data;
	struct device *dev = sh_uart->ctrl_blk->dev;

	uart_irq_update(dev);

	if (uart_irq_rx_ready(dev)) {
		uart_rx_handle(sh_uart);
	}

	if (uart_irq_tx_ready(dev)) {
		uart_tx_handle(sh_uart);
	}
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static void uart_irq_init(const struct shell_uart *sh_uart)
{
	struct device *dev = sh_uart->ctrl_blk->dev;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart_irq_callback_user_data_set(dev, uart_callback, (void *)sh_uart);
	uart_irq_rx_enable(dev);
#endif
}

static void timer_handler(struct k_timer *timer)
{
	u8_t c;
	const struct shell_uart *sh_uart = k_timer_user_data_get(timer);

	while (uart_poll_in(sh_uart->ctrl_blk->dev, &c) == 0) {
		if (sys_ring_buf_bytes_cpy_put(sh_uart->rx_ringbuf, &c, 1) ==
				0) {
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
	const struct shell_uart *sh_uart = (struct shell_uart *)transport->ctx;

	sh_uart->ctrl_blk->dev = (struct device *)config;
	sh_uart->ctrl_blk->handler = evt_handler;
	sh_uart->ctrl_blk->context = context;

	if (IS_ENABLED(CONFIG_UART_INTERRUPT_DRIVEN)) {
		uart_irq_init(sh_uart);
	} else {
		k_timer_init(sh_uart->timer, timer_handler, NULL);
		k_timer_user_data_set(sh_uart->timer, (void *)sh_uart);
		k_timer_start(sh_uart->timer, 20, 20);
	}

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

static void irq_write(const struct shell_uart *sh_uart, const void *data,
		     size_t length, size_t *cnt)
{
	*cnt = sys_ring_buf_bytes_cpy_put(sh_uart->tx_ringbuf, data, length);

	if (atomic_set(&sh_uart->ctrl_blk->tx_busy, 1) == 0) {
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
		uart_irq_tx_enable(sh_uart->ctrl_blk->dev);
#endif
	}
}

static int write(const struct shell_transport *transport,
		 const void *data, size_t length, size_t *cnt)
{
	const struct shell_uart *sh_uart = (struct shell_uart *)transport->ctx;
	const u8_t *data8 = (const u8_t *)data;

	if (IS_ENABLED(CONFIG_UART_INTERRUPT_DRIVEN)) {
		irq_write(sh_uart, data, length, cnt);
	} else {
		for (size_t i = 0; i < length; i++) {
			uart_poll_out(sh_uart->ctrl_blk->dev, data8[i]);
		}

		*cnt = length;

		sh_uart->ctrl_blk->handler(SHELL_TRANSPORT_EVT_TX_RDY,
					   sh_uart->ctrl_blk->context);
	}

	return 0;
}

static int read(const struct shell_transport *transport,
		void *data, size_t length, size_t *cnt)
{
	struct shell_uart *sh_uart = (struct shell_uart *)transport->ctx;

	*cnt = sys_ring_buf_bytes_cpy_get(sh_uart->rx_ringbuf, data, length);

	return 0;
}

const struct shell_transport_api shell_uart_transport_api = {
	.init = init,
	.uninit = uninit,
	.enable = enable,
	.write = write,
	.read = read
};

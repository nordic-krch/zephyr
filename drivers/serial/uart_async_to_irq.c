/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "uart_async_to_irq.h"
#include <string.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(UART_ASYNC_TO_IRQ_LOG_NAME, 4);

#include <hal/nrf_gpio.h>
static struct uart_async_to_irq_data *get_data(const struct device *dev)
{
	struct uart_async_to_irq_data **data = dev->data;

	return *data;
}

/* Function calculates timeout based on baudrate. */
static uint32_t get_rx_timeout(const struct device *dev)
{
	struct uart_config cfg;
	int err;

	err = uart_config_get(dev, &cfg);
	if (err != 0) {
		/* Arbitrary timeout value. */
		return 100;
	}

	/* Get time needed for transferring of 40 bits. */
	uint32_t us = (40 * 1000000) / cfg.baudrate;

	return ceiling_fraction(us, 1000);
}

static uint16_t diff(uint16_t x, uint16_t y, uint16_t size)
{
	return (x - y) & (size - 1);
}

static uint16_t sum(uint16_t x, uint16_t y, uint16_t size)
{
	return (x + y) & (size - 1);
}

static int rx_enable(const struct device *dev, struct uart_async_to_irq_data *data)
{
	data->rx.commit_idx = 0;
	data->rx.claim_idx = 0;
	data->rx.alloc_idx = data->rx.size / 2;
	data->rx.starting = true;
	data->rx.empty = true;

	return data->api->rx_enable(dev, data->rx.buf, data->rx.size / 2, get_rx_timeout(dev));
}

static void uart_async_to_irq_callback(const struct device *dev,
					struct uart_event *evt,
					void *user_data)
{
	struct uart_async_to_irq_data *data = (struct uart_async_to_irq_data *)user_data;
	bool call_handler = false;

	switch (evt->type) {
	case UART_TX_DONE:
		data->tx.req_len = 0;
		call_handler = true;
		break;
	case UART_RX_RDY:
	{
		LOG_INST_DBG(data->log, "%dRX, commit_idx: %d, new len:%d, available: %d",
			data->rx.enabled, data->rx.commit_idx, evt->data.rx.len,
			diff(data->rx.commit_idx, data->rx.claim_idx, data->rx.size));
		k_spinlock_key_t key = k_spin_lock(&data->lock);

		data->rx.commit_idx = sum(data->rx.commit_idx, evt->data.rx.len, data->rx.size);
		data->rx.empty = false;
		k_spin_unlock(&data->lock, key);
		call_handler = data->rx.enabled;
		break;
	}
	case UART_RX_BUF_REQUEST:
	{
		/* Set buffer on response only after starting. Later on new buffer will
		 * be provided on response to buffer release event.
		 */
		if (data->rx.starting) {
			data->rx.starting = false;

			size_t hlen = data->rx.size / 2;
			int err = data->api->rx_buf_rsp(dev, &data->rx.buf[hlen], hlen);

			__ASSERT_NO_MSG(err == 0);
		} else {
			LOG_INST_DBG(data->log, "Unhandled buf request");
		}

		break;
	}
	case UART_RX_STOPPED:
		call_handler = data->err_enabled;
		break;
	case UART_RX_DISABLED:
	{
		bool start_rx;
		k_spinlock_key_t key = k_spin_lock(&data->lock);
		if (data->rx.empty) {
			start_rx = true;
		} else {
			start_rx = false;
			data->rx.dev_enabled = false;
		}
		k_spin_unlock(&data->lock, key);

		if (start_rx) {
			LOG_INST_DBG(data->log, "Reenabling RX from RX_DISABLED");
			int err = rx_enable(dev, data);

			(void)err;
			__ASSERT_NO_MSG(err >= 0);
		}

		break;
	}
	default:
		break;
	}

	if (data->callback) {
		atomic_inc(&data->irq_req);
		data->trampoline(data);
	}
}

int z_uart_async_to_irq_fifo_fill(const struct device *dev, const uint8_t *buf, int len)
{
	struct uart_async_to_irq_data *data = get_data(dev);
	int err;

	len = MIN(len, data->tx.size);
	if (!atomic_cas((atomic_t *)&data->tx.req_len, 0, len)) {
		return 0;
	}

	memcpy(data->tx.buf, buf, len);

	err = data->api->tx(dev, data->tx.buf, data->tx.req_len, SYS_FOREVER_MS);
	if (err < 0) {
		data->tx.req_len = 0;

		return 0;
	}

	return len;
}

/** Interrupt driven FIFO read function */
int z_uart_async_to_irq_fifo_read(const struct device *dev,
				uint8_t *buf,
				const int len)
{
	struct uart_async_to_irq_data *data = get_data(dev);

	if (data->rx.empty) {
		return 0;
	}

	k_spinlock_key_t key = k_spin_lock(&data->lock);
	uint32_t claim_idx = data->rx.claim_idx;
	uint32_t commit_idx = data->rx.commit_idx;
	uint16_t available;
	uint32_t cpy_len;

	if (commit_idx >= claim_idx) {
		available = commit_idx - claim_idx;
	} else {
		available = data->rx.size - claim_idx;
	}

	__ASSERT_NO_MSG(available);
	cpy_len = MIN(available, len);

	data->rx.claim_idx = sum(claim_idx, cpy_len, data->rx.size);
	if (data->rx.claim_idx == data->rx.commit_idx) {
		data->rx.empty = true;
	}

	uint32_t hmask = data->rx.size / 2;
	bool feed_buf = (data->rx.claim_idx & hmask) != (claim_idx & hmask);
	bool start_rx;

	if (!data->rx.dev_enabled && feed_buf) {
		feed_buf = false;
		start_rx = true;
		data->rx.dev_enabled = true;
	} else {
		start_rx = false;
	}

	k_spin_unlock(&data->lock, key);

	LOG_INST_DBG(data->log, "prev claim_idx:%d, new claim_idx:%d", claim_idx, data->rx.claim_idx);
	/* When we passed buffer boundary we can feed the driver with the buffer.
	 * It may happen that buffer is feed too late. In that case RX will be
	 * disabled and reenabled. Which is ok when hwfc is used but lead to bytes
	 * being lost if hwfc is off.
	 */
	if (start_rx) {
		LOG_INST_DBG(data->log, "Reenabling RX from fifo read");
		int err = data->api->rx_enable(dev,
					       &data->rx.buf[claim_idx & hmask],
					       hmask,
					       get_rx_timeout(dev));

		(void)err;
		__ASSERT_NO_MSG(err >= 0);
	} else if (feed_buf) {
		LOG_INST_DBG(data->log, "Feeding buffer.");
		int err = data->api->rx_buf_rsp(dev,
						&data->rx.buf[claim_idx & hmask],
						hmask);
		__ASSERT(err == 0 || err == -EACCES, "err %d", err);
	}
	memcpy(buf, &data->rx.buf[claim_idx], cpy_len);

	return cpy_len;
}


/** Interrupt driven transfer enabling function */
void z_uart_async_to_irq_irq_tx_enable(const struct device *dev)
{
	struct uart_async_to_irq_data *data = get_data(dev);

	data->tx.enabled = true;

	atomic_inc(&data->irq_req);
	data->trampoline(data);

}

/** Interrupt driven transfer disabling function */
void z_uart_async_to_irq_irq_tx_disable(const struct device *dev)
{
	struct uart_async_to_irq_data *data = get_data(dev);

	data->tx.enabled = false;
}

/** Interrupt driven transfer ready function */
int z_uart_async_to_irq_irq_tx_ready(const struct device *dev)
{
	struct uart_async_to_irq_data *data = get_data(dev);

	return data->tx.enabled && (data->tx.req_len == 0);
}

/** Interrupt driven receiver enabling function */
void z_uart_async_to_irq_irq_rx_enable(const struct device *dev)
{
	struct uart_async_to_irq_data *data = get_data(dev);

	data->rx.enabled = true;
	atomic_inc(&data->irq_req);
	data->trampoline(data);
}

/** Interrupt driven receiver disabling function */
void z_uart_async_to_irq_irq_rx_disable(const struct device *dev)
{
	struct uart_async_to_irq_data *data = get_data(dev);

	data->rx.enabled = false;
}

/** Interrupt driven transfer complete function */
int z_uart_async_to_irq_irq_tx_complete(const struct device *dev)
{
	return z_uart_async_to_irq_irq_tx_ready(dev);
}

/** Interrupt driven receiver ready function */
int z_uart_async_to_irq_irq_rx_ready(const struct device *dev)
{
	struct uart_async_to_irq_data *data = get_data(dev);
	uint16_t available;

	available = diff(data->rx.commit_idx, data->rx.claim_idx, data->rx.size);

	return data->rx.enabled && available;
}

/** Interrupt driven error enabling function */
void z_uart_async_to_irq_irq_err_enable(const struct device *dev)
{
	struct uart_async_to_irq_data *data = get_data(dev);

	data->err_enabled = true;
}

/** Interrupt driven error disabling function */
void z_uart_async_to_irq_irq_err_disable(const struct device *dev)
{
	struct uart_async_to_irq_data *data = get_data(dev);

	data->err_enabled = false;
}

/** Interrupt driven pending status function */
int z_uart_async_to_irq_irq_is_pending(const struct device *dev)
{
	return z_uart_async_to_irq_irq_tx_ready(dev) || z_uart_async_to_irq_irq_rx_ready(dev);
}

/** Interrupt driven interrupt update function */
int z_uart_async_to_irq_irq_update(const struct device *dev)
{
	return 1;
}

/** Set the irq callback function */
void z_uart_async_to_irq_irq_callback_set(const struct device *dev,
			 uart_irq_callback_user_data_t cb,
			 void *user_data)
{
	struct uart_async_to_irq_data *data = get_data(dev);

	data->callback = cb;
	data->user_data = user_data;
}

int uart_async_to_irq_rx_enable(const struct device *dev)
{
	struct uart_async_to_irq_data *data = get_data(dev);
	int err;

	if (!is_power_of_two(data->rx.size) || !is_power_of_two(data->tx.size)) {
		return -EINVAL;
	}

	err = data->api->callback_set(dev, uart_async_to_irq_callback, data);
	if (err < 0) {
		return err;
	}

	data->dev = dev;
	data->rx.dev_enabled = true;

	return rx_enable(dev, data);
}

void uart_async_to_irq_trampoline_cb(struct uart_async_to_irq_data *data)
{
	do {
		data->callback(data->dev, data->user_data);
	} while (atomic_dec(&data->irq_req) > 1);
}


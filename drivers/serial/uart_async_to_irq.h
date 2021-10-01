/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_UART_ASYNC_TO_IRQ_H_
#define ZEPHYR_DRIVERS_UART_ASYNC_TO_IRQ_H_

#include <drivers/uart.h>
#include <logging/log.h>
#include <sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration. */
struct uart_async_to_irq_data;

/* Function that triggers trampoline to higher priority context from which
 * uart interrupt is called. It is to fullfil requirement that uart interrupt
 * driven API shall be called from UART interrupt. Trampoline context shall
 * have high priority (ideally uart interrupt priority) but it is not a hard
 * requirement because there are protections against trampoline being interrupted
 * by the actual uart interrupt. If priority of trampoline is too low and it is
 * handled too late uart driver may not setup next buffer on time and loose
 * data.
 *
 * One option may be to use k_timer configured to expire as soon as possible.
 */
typedef void (*uart_async_to_irq_trampoline)(struct uart_async_to_irq_data *data);

/** @brief Callback to be called from trampoline context.
 *
 * @param data Data.
 */
void uart_async_to_irq_trampoline_cb(struct uart_async_to_irq_data *data);

/** @brief Interrupt driven API initializer */
#define UART_ASYNC_TO_IRQ_API_INIT() \
	.fifo_fill	= z_uart_async_to_irq_fifo_fill, \
	.fifo_read	= z_uart_async_to_irq_fifo_read, \
	.irq_tx_enable	= z_uart_async_to_irq_irq_tx_enable, \
	.irq_tx_disable	= z_uart_async_to_irq_irq_tx_disable, \
	.irq_tx_ready	= z_uart_async_to_irq_irq_tx_ready, \
	.irq_rx_enable	= z_uart_async_to_irq_irq_rx_enable, \
	.irq_rx_disable	= z_uart_async_to_irq_irq_rx_disable, \
	.irq_tx_complete= z_uart_async_to_irq_irq_tx_complete, \
	.irq_rx_ready	= z_uart_async_to_irq_irq_rx_ready, \
	.irq_err_enable	= z_uart_async_to_irq_irq_err_enable, \
	.irq_err_disable= z_uart_async_to_irq_irq_err_disable, \
	.irq_is_pending	= z_uart_async_to_irq_irq_is_pending, \
	.irq_update	= z_uart_async_to_irq_irq_update, \
	.irq_callback_set = z_uart_async_to_irq_irq_callback_set

/** @brief Data structure initializer.
 *
 * @param _api Structure with UART asynchronous API.
 * @param _rxbuf RX buffer. Size must be power of two.
 * @param _txbuf TX buffer. Size must be power of two.
 * @param _trampoline Function that is trampolines to high priority context.
 * @param _log Logging instance, if not provided (empty) then default is used.
 */
#define UART_ASYNC_TO_IRQ_API_DATA_INITIALIZE(_api, _rxbuf, _txbuf, _trampoline, _log) \
	{ \
		.api = _api, \
		.trampoline = _trampoline, \
		.rx = { \
			.buf = _rxbuf, \
			.size = ARRAY_SIZE(_rxbuf)\
		}, \
		.tx = { \
			.buf = _txbuf, \
			.size = ARRAY_SIZE(_txbuf)\
		}, \
		LOG_OBJECT_PTR_INIT(log, \
				COND_CODE_1(IS_EMPTY(_log), \
					    (LOG_OBJECT_PTR(UART_ASYNC_TO_IRQ_LOG_NAME)), \
					    (_log) \
					   ) \
				   ) \
	}

/* Starting from here API is internal only. */

struct uart_async_to_irq_async_api {
	int (*callback_set)(const struct device *dev,
			    uart_callback_t callback,
			    void *user_data);

	int (*tx)(const struct device *dev, const uint8_t *buf, size_t len,
		  int32_t timeout);
	int (*tx_abort)(const struct device *dev);

	int (*rx_enable)(const struct device *dev, uint8_t *buf, size_t len,
			 int32_t timeout);
	int (*rx_buf_rsp)(const struct device *dev, uint8_t *buf, size_t len);
	int (*rx_disable)(const struct device *dev);
};

/** @internal @brief Structure holding receiver data. */
struct uart_async_to_irq_rx_data {
	uint8_t *buf;
	size_t size;
	uint16_t alloc_idx;
	uint16_t commit_idx;
	uint16_t claim_idx;
	bool enabled;
	bool starting;
	bool empty;
	bool dev_enabled;
};

/** @internal @brief Structure holding transmitter data. */
struct uart_async_to_irq_tx_data {
	uint8_t *buf;
	size_t size;
	volatile size_t req_len;
	volatile bool enabled;
};

/** @internal @brief Structure used by the adaptation layer. */
struct uart_async_to_irq_data {
	const struct uart_async_to_irq_async_api *api;
	uart_irq_callback_user_data_t callback;
	void *user_data;
	const struct device *dev;
	uart_async_to_irq_trampoline trampoline;
	struct uart_async_to_irq_rx_data rx;
	struct uart_async_to_irq_tx_data tx;
	bool err_enabled;
	struct k_spinlock lock;
	atomic_t irq_cnt;
	atomic_t irq_req;
	LOG_INSTANCE_PTR_DECLARE(log);
};

/** @internal Interrupt driven FIFO fill function */
int z_uart_async_to_irq_fifo_fill(const struct device *dev,
				  const uint8_t *buf,
				  int len);

/** @internal Interrupt driven FIFO read function */
int z_uart_async_to_irq_fifo_read(const struct device *dev,
				  uint8_t *buf,
				  const int len);

/** @internal Interrupt driven transfer enabling function */
void z_uart_async_to_irq_irq_tx_enable(const struct device *dev);

/** @internal Interrupt driven transfer disabling function */
void z_uart_async_to_irq_irq_tx_disable(const struct device *dev);

/** @internal Interrupt driven transfer ready function */
int z_uart_async_to_irq_irq_tx_ready(const struct device *dev);

/** @internal Interrupt driven receiver enabling function */
void z_uart_async_to_irq_irq_rx_enable(const struct device *dev);

/** @internal Interrupt driven receiver disabling function */
void z_uart_async_to_irq_irq_rx_disable(const struct device *dev);

/** @internal Interrupt driven transfer complete function */
int z_uart_async_to_irq_irq_tx_complete(const struct device *dev);

/** @internal Interrupt driven receiver ready function */
int z_uart_async_to_irq_irq_rx_ready(const struct device *dev);

/** @internal Interrupt driven error enabling function */
void z_uart_async_to_irq_irq_err_enable(const struct device *dev);

/** @internal Interrupt driven error disabling function */
void z_uart_async_to_irq_irq_err_disable(const struct device *dev);

/** @internal Interrupt driven pending status function */
int z_uart_async_to_irq_irq_is_pending(const struct device *dev);

/** @internal Interrupt driven interrupt update function */
int z_uart_async_to_irq_irq_update(const struct device *dev);

/** @internal Set the irq callback function */
void z_uart_async_to_irq_irq_callback_set(const struct device *dev,
			 uart_irq_callback_user_data_t cb,
			 void *user_data);

/* @brief Enable RX for interrupt driven API.
 *
 * @param dev UART device. Device must support asynchronous API.
 *
 * @retval 0 on successful operation.
 * @retval -EINVAL if adaption layer has wrong configuration.
 * @retval negative value Error reported by the UART API.
 */
int uart_async_to_irq_rx_enable(const struct device *dev);


#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_UART_ASYNC_TO_IRQ_H_ */

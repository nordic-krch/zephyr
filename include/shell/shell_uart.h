/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SHELL_UART_H__
#define SHELL_UART_H__

#include <shell/shell.h>
#include <ring_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const struct shell_transport_api shell_uart_transport_api;

/** @brief Shell UART transport instance control block (RW data). */
struct shell_uart_ctrl_blk {
	struct device *dev;
	shell_transport_handler_t handler;
	void *context;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	u8_t tx_buf[8];
#endif
};

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
#define UART_SHELL_TX_RINGBUF_DECLARE(_name, _size) \
	SYS_RING_BUF_RAW_DECLARE_SIZE(_name##_tx_ringbuf, _tx_ringbuf_size)

#define UART_SHELL_TX_RINGBUF_PTR(_name) &_name##_tx_ringbuf
#else /* CONFIG_UART_INTERRUPT_DRIVEN */
#define UART_SHELL_TX_RINGBUF_DECLARE(_name, _size) /* Empty */
#define UART_SHELL_TX_RINGBUF_PTR(_name) NULL
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

/** @brief Shell UART transport instance structure. */
struct shell_uart {
	struct shell_uart_ctrl_blk *ctrl_blk;
	struct k_timer *timer;
	struct ring_buf * tx_ringbuf;
	struct ring_buf * rx_ringbuf;
};

/** @brief Macro for creating shell UART transport instance. */
#define SHELL_UART_DEFINE(_name, _tx_ringbuf_size, _rx_ringbuf_size)	     \
	static struct shell_uart_ctrl_blk _name##_ctrl_blk;		     \
	static struct k_timer _name##_timer;				     \
	UART_SHELL_TX_RINGBUF_DECLARE(_name, _tx_ringbuf_size);		     \
	SYS_RING_BUF_RAW_DECLARE_SIZE(_name##_rx_ringbuf, _rx_ringbuf_size); \
	static const struct shell_uart _name##_shell_uart = {		     \
		.ctrl_blk = &_name##_ctrl_blk,				     \
		.timer = &_name##_timer,				     \
		.tx_ringbuf = UART_SHELL_TX_RINGBUF_PTR(_name),		     \
		.rx_ringbuf = &_name##_rx_ringbuf,			     \
	};								     \
	struct shell_transport _name = {				     \
		.api = &shell_uart_transport_api,			     \
		.ctx = (struct shell_uart *)&_name##_shell_uart		     \
	}

#ifdef __cplusplus
}
#endif

#endif /* SHELL_UART_H__ */

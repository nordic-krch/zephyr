/* h4.c - H:4 UART based Bluetooth driver */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>

#include <zephyr.h>
#include <arch/cpu.h>

#include <init.h>
#include <drivers/uart.h>
#include <sys/util.h>
#include <sys/byteorder.h>
#include <string.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <drivers/bluetooth/hci_driver.h>

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_HCI_DRIVER)
#define LOG_MODULE_NAME bt_driver
#include "common/log.h"

#include "../util.h"

#define H4_NONE 0x00
#define H4_CMD  0x01
#define H4_ACL  0x02
#define H4_SCO  0x03
#define H4_EVT  0x04
#define H4_INV  0xff

#define UART_RX_BUF_SIZE 16
#define UART_RX_BUF_COUNT 4

#define TX_TIMEOUT_MS 1000
static K_THREAD_STACK_DEFINE(rx_thread_stack, CONFIG_BT_RX_STACK_SIZE);
static struct k_thread rx_thread_data;

/* Used when UART ASYNC api is used. */
static u8_t *rx_pool_buf[UART_RX_BUF_SIZE*UART_RX_BUF_COUNT];
static struct k_mem_slab rx_pool;


static struct {
	struct net_buf *buf;
	struct k_fifo   fifo;

	u16_t    remaining;
	u16_t    discard;

	bool     have_hdr;
	bool     discardable;

	u8_t     hdr_len;

	u8_t     type;
	union {
		struct bt_hci_evt_hdr evt;
		struct bt_hci_acl_hdr acl;
		u8_t hdr[4];
	};
} rx = {
	.fifo = Z_FIFO_INITIALIZER(rx.fifo),
};

static struct {
	u8_t type;
	struct net_buf *buf;
	struct k_fifo   fifo;
} tx = {
	.fifo = Z_FIFO_INITIALIZER(tx.fifo),
};

static struct device *h4_dev;

static inline int rx_read(void *context, u8_t *dst, int req_len)
{
#ifdef CONFIG_BT_H4_UART_INTERRUPT_DRIVEN
	return uart_fifo_read((struct device *)context, dst, req_len);
#else
	struct uart_event_rx *rx_buf = context;
	size_t len = MIN(req_len, rx_buf->len);

	if (dst) {
		memcpy(dst, &rx_buf->buf[rx_buf->offset], len);
	}
	rx_buf->len -= len;
	rx_buf->offset += len;

	return len;
#endif
}

static inline void h4_get_type(void *context)
{
	/* Get packet type */
	if (rx_read(context, &rx.type, 1) != 1) {
		BT_WARN("Unable to read H:4 packet type");
		rx.type = H4_NONE;
		return;
	}

	switch (rx.type) {
	case H4_EVT:
		rx.remaining = sizeof(rx.evt);
		rx.hdr_len = rx.remaining;
		break;
	case H4_ACL:
		rx.remaining = sizeof(rx.acl);
		rx.hdr_len = rx.remaining;
		break;
	default:
		BT_ERR("Unknown H:4 type 0x%02x", rx.type);
		rx.type = H4_NONE;
	}
}

static inline void get_acl_hdr(void *context)
{
	struct bt_hci_acl_hdr *hdr = &rx.acl;
	int to_read = sizeof(*hdr) - rx.remaining;

	rx.remaining -= rx_read(context, (u8_t *)hdr + to_read, rx.remaining);
	if (!rx.remaining) {
		rx.remaining = sys_le16_to_cpu(hdr->len);
		BT_DBG("Got ACL header. Payload %u bytes", rx.remaining);
		rx.have_hdr = true;
	}
}

static inline void get_evt_hdr(void *context)
{
	struct bt_hci_evt_hdr *hdr = &rx.evt;
	int to_read = rx.hdr_len - rx.remaining;

	rx.remaining -= rx_read(context, (u8_t *)hdr + to_read, rx.remaining);
	if (rx.hdr_len == sizeof(*hdr) && rx.remaining < sizeof(*hdr)) {
		switch (rx.evt.evt) {
		case BT_HCI_EVT_LE_META_EVENT:
			rx.remaining++;
			rx.hdr_len++;
			break;
#if defined(CONFIG_BT_BREDR)
		case BT_HCI_EVT_INQUIRY_RESULT_WITH_RSSI:
		case BT_HCI_EVT_EXTENDED_INQUIRY_RESULT:
			rx.discardable = true;
			break;
#endif
		}
	}

	if (!rx.remaining) {
		if (rx.evt.evt == BT_HCI_EVT_LE_META_EVENT &&
		    rx.hdr[sizeof(*hdr)] == BT_HCI_EVT_LE_ADVERTISING_REPORT) {
			BT_DBG("Marking adv report as discardable");
			rx.discardable = true;
		}

		rx.remaining = hdr->len - (rx.hdr_len - sizeof(*hdr));
		BT_DBG("Got event header. Payload %u bytes", hdr->len);
		rx.have_hdr = true;
	}
}


static inline void copy_hdr(struct net_buf *buf)
{
	net_buf_add_mem(buf, rx.hdr, rx.hdr_len);
}

static void reset_rx(void)
{
	rx.type = H4_NONE;
	rx.remaining = 0U;
	rx.have_hdr = false;
	rx.hdr_len = 0U;
	rx.discardable = false;
}

static struct net_buf *get_rx(k_timeout_t timeout)
{
	BT_DBG("type 0x%02x, evt 0x%02x", rx.type, rx.evt.evt);

	if (rx.type == H4_EVT) {
		return bt_buf_get_evt(rx.evt.evt, rx.discardable, timeout);
	}

	return bt_buf_get_rx(BT_BUF_ACL_IN, timeout);
}

static void rx_thread(void *p1, void *p2, void *p3)
{
	struct net_buf *buf;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	BT_DBG("started");

	while (1) {
		BT_DBG("rx.buf %p", rx.buf);

		/* We can only do the allocation if we know the initial
		 * header, since Command Complete/Status events must use the
		 * original command buffer (if available).
		 */
		if (rx.have_hdr && !rx.buf) {
			rx.buf = get_rx(K_FOREVER);
			BT_DBG("Got rx.buf %p", rx.buf);
			if (rx.remaining > net_buf_tailroom(rx.buf)) {
				BT_ERR("Not enough space in buffer");
				rx.discard = rx.remaining;
				reset_rx();
			} else {
				copy_hdr(rx.buf);
			}
		}

		/* Let the ISR continue receiving new packets */
#ifdef CONFIG_BT_H4_UART_INTERRUPT_DRIVEN
			uart_irq_rx_enable(h4_dev);
#else /* CONFIG_BT_H4_UART_INTERRUPT_DRIVEN*/
			//BT_ERR("Not supported.");
#endif
		buf = net_buf_get(&rx.fifo, K_FOREVER);
		do {
#ifdef CONFIG_BT_H4_UART_INTERRUPT_DRIVEN
			uart_irq_rx_enable(h4_dev);
#endif
			BT_DBG("Calling bt_recv(%p)", buf);
			bt_recv(buf);

			/* Give other threads a chance to run if the ISR
			 * is receiving data so fast that rx.fifo never
			 * or very rarely goes empty.
			 */
			k_yield();

#ifdef CONFIG_BT_H4_UART_INTERRUPT_DRIVEN
			uart_irq_rx_disable(h4_dev);
#endif
			buf = net_buf_get(&rx.fifo, K_NO_WAIT);
		} while (buf);
	}
}

static size_t h4_discard(struct device *uart, size_t len)
{
	u8_t buf[33];

	return rx_read(uart, buf, MIN(len, sizeof(buf)));
}

static inline void read_payload(void *context)
{
	struct net_buf *buf;
	bool prio;
	int read;

	if (!rx.buf) {
		rx.buf = get_rx(K_NO_WAIT);
		if (!rx.buf) {
			if (rx.discardable) {
				BT_WARN("Discarding event 0x%02x", rx.evt.evt);
				rx.discard = rx.remaining;
				reset_rx();
				return;
			}

			BT_WARN("Failed to allocate, deferring to rx_thread");
#ifdef CONFIG_BT_H4_UART_INTERRUPT_DRIVEN
			uart_irq_rx_disable(context);
#else
			BT_ERR("Not supported.");
#endif
			return;
		}

		BT_DBG("Allocated rx.buf %p", rx.buf);

		if (rx.remaining > net_buf_tailroom(rx.buf)) {
			BT_ERR("Not enough space in buffer");
			rx.discard = rx.remaining;
			reset_rx();
			return;
		}

		copy_hdr(rx.buf);
	}

	read = rx_read(context, net_buf_tail(rx.buf), rx.remaining);
	net_buf_add(rx.buf, read);
	rx.remaining -= read;

	BT_DBG("got %d bytes, remaining %u", read, rx.remaining);
	BT_DBG("Payload (len %u): %s", rx.buf->len,
	       bt_hex(rx.buf->data, rx.buf->len));

	if (rx.remaining) {
		return;
	}

	prio = (rx.type == H4_EVT && bt_hci_evt_is_prio(rx.evt.evt));

	buf = rx.buf;
	rx.buf = NULL;

	if (rx.type == H4_EVT) {
		bt_buf_set_type(buf, BT_BUF_EVT);
	} else {
		bt_buf_set_type(buf, BT_BUF_ACL_IN);
	}

	reset_rx();

	if (prio) {
		BT_DBG("Calling bt_recv_prio(%p)", buf);
		bt_recv_prio(buf);
	} else {
		BT_DBG("Putting buf %p to rx fifo", buf);
		net_buf_put(&rx.fifo, buf);
	}
}

static inline void read_header(void *context)
{
	switch (rx.type) {
	case H4_NONE:
		h4_get_type(context);
		return;
	case H4_EVT:
		get_evt_hdr(context);
		break;
	case H4_ACL:
		get_acl_hdr(context);
		break;
	default:
		CODE_UNREACHABLE;
		return;
	}

	if (rx.have_hdr && rx.buf) {
		if (rx.remaining > net_buf_tailroom(rx.buf)) {
			BT_ERR("Not enough space in buffer");
			rx.discard = rx.remaining;
			reset_rx();
		} else {
			copy_hdr(rx.buf);
		}
	}
}

static inline void process_rx(void *context)
{
	BT_DBG("remaining %u discard %u have_hdr %u rx.buf %p len %u",
	       rx.remaining, rx.discard, rx.have_hdr, rx.buf,
	       rx.buf ? rx.buf->len : 0);

	if (rx.discard) {
		LOG_WRN("discard: %d bytes", rx.discard);
		rx.discard -= h4_discard(context, rx.discard);
		return;
	}

	if (rx.have_hdr) {
		read_payload(context);
	} else {
		read_header(context);
	}
}

#ifdef CONFIG_BT_H4_UART_INTERRUPT_DRIVEN

static inline void process_tx(void)
{
	int bytes;

	if (!tx.buf) {
		tx.buf = net_buf_get(&tx.fifo, K_NO_WAIT);
		if (!tx.buf) {
			BT_ERR("TX interrupt but no pending buffer!");
			uart_irq_tx_disable(h4_dev);
			return;
		}
	}

	if (!tx.type) {
		switch (bt_buf_get_type(tx.buf)) {
		case BT_BUF_ACL_OUT:
			tx.type = H4_ACL;
			break;
		case BT_BUF_CMD:
			tx.type = H4_CMD;
			break;
		default:
			BT_ERR("Unknown buffer type");
			goto done;
		}

		bytes = uart_fifo_fill(h4_dev, &tx.type, 1);
		if (bytes != 1) {
			BT_WARN("Unable to send H:4 type");
			tx.type = H4_NONE;
			return;
		}
	}

	bytes = uart_fifo_fill(h4_dev, tx.buf->data, tx.buf->len);
	net_buf_pull(tx.buf, bytes);

	if (tx.buf->len) {
		return;
	}
done:
	tx.type = H4_NONE;
	net_buf_unref(tx.buf);
	tx.buf = net_buf_get(&tx.fifo, K_NO_WAIT);
	if (!tx.buf) {
		uart_irq_tx_disable(h4_dev);
	}
}

static void bt_uart_isr(struct device *unused)
{
	ARG_UNUSED(unused);

	while (uart_irq_update(h4_dev) && uart_irq_is_pending(h4_dev)) {
		if (uart_irq_tx_ready(h4_dev)) {
			process_tx();
		}

		if (uart_irq_rx_ready(h4_dev)) {
			process_rx(h4_dev);
		}
	}
}

#else /* CONFIG_BT_H4_UART_ASYNC */
/* returns true if packet completed. */
static int process_tx(void)
{
	int err;

	if (!tx.type) {
		switch (bt_buf_get_type(tx.buf)) {
		case BT_BUF_ACL_OUT:
			tx.type = H4_ACL;
			break;
		case BT_BUF_CMD:
			tx.type = H4_CMD;
			break;
		default:
			BT_ERR("Unknown buffer type");
			return -EINVAL;
		}
		err = uart_tx(h4_dev, &tx.type, 1, TX_TIMEOUT_MS);
		if ((err < 0) && (err != -EBUSY)) {
			BT_WARN("Unable to send (err: %d)", err);
			return -EINVAL;
		} else {
			return 0;
		}
	}

	err = uart_tx(h4_dev, tx.buf->data, tx.buf->len, TX_TIMEOUT_MS);
	if ((err < 0) && (err != -EBUSY)) {
		BT_WARN("Unable to send (err: %d)", err);
	}

	return err;
}

static void tx_complete(size_t len)
{
	if (tx.type != H4_INV) {
		tx.type = H4_INV;
		return;
	}

	net_buf_pull(tx.buf, len);
	if (tx.buf->len == 0) {
		tx.type = H4_NONE;
		net_buf_unref(tx.buf);
		tx.buf = net_buf_get(&tx.fifo, K_NO_WAIT);
	}
}

static int rx_enable(void)
{
	int err;
	u8_t *buf;

	k_mem_slab_init(&rx_pool, rx_pool_buf,
			UART_RX_BUF_SIZE, UART_RX_BUF_COUNT);

	err = k_mem_slab_alloc(&rx_pool, (void **)&buf, K_NO_WAIT);
	__ASSERT_NO_MSG(err == 0);
	err = uart_rx_enable(h4_dev, buf, UART_RX_BUF_SIZE, 1);
	if (err < 0) {
		return -EIO;
	}

	return 0;
}

static void uart_async_callback(struct uart_event *evt, void *user_data)
{
	int err;

	switch (evt->type) {
		case UART_TX_DONE:
			tx_complete(evt->data.tx.len);
			if (tx.buf) {
				process_tx();
			}
			break;
		case UART_RX_RDY:
			LOG_HEXDUMP_DBG(
				&evt->data.rx.buf[evt->data.rx.offset],
				evt->data.rx.len, "rx:");
			do {
				process_rx(&evt->data.rx);
			} while (evt->data.rx.len);
			break;
		case UART_RX_BUF_REQUEST:
		{
			u8_t *buf;
			err = k_mem_slab_alloc(&rx_pool, (void **)&buf,
						K_NO_WAIT);
			if (err < 0) {
				BT_ERR("Failed to allocate new RX buffer");
			} else {
				err = uart_rx_buf_rsp(h4_dev, buf,
						UART_RX_BUF_SIZE);
				__ASSERT_NO_MSG(err == 0);
			}
			break;
		}
		case UART_RX_BUF_RELEASED:
		{
			k_mem_slab_free(&rx_pool,
					(void **)&evt->data.rx_buf.buf);
			break;
		}
		case UART_RX_STOPPED:
			BT_DBG("RX error occured, reason: %d",
				evt->data.rx_stop.reason);
			break;
		case UART_RX_DISABLED:
			BT_WARN("Unexpected disable (rx error?). Reenabling");
			err = rx_enable();
			__ASSERT(!err, "Failed to enable RX (err: %d)", err);
			break;
		default:
			BT_ERR("Unexpected UART event: %d", evt->type);
			break;
	}
}
#endif /* CONFIG_BT_H4_UART_ASYNC */

static int h4_send(struct net_buf *buf)
{
	BT_DBG("buf %p type %u len %u", buf, bt_buf_get_type(buf), buf->len);

	net_buf_put(&tx.fifo, buf);
#ifdef CONFIG_BT_H4_UART_INTERRUPT_DRIVEN
	uart_irq_tx_enable(h4_dev);
#else
	if (atomic_ptr_cas((atomic_ptr_t *)&tx.buf, NULL, buf)) {
		tx.buf = net_buf_get(&tx.fifo, K_NO_WAIT);
		process_tx();
	}
#endif

	return 0;
}

/** Setup the HCI transport, which usually means to reset the Bluetooth IC
  *
  * @param dev The device structure for the bus connecting to the IC
  *
  * @return 0 on success, negative error value on failure
  */
int __weak bt_hci_transport_setup(struct device *dev)
{
#ifdef CONFIG_BT_H4_UART_INTERRUPT_DRIVEN
	h4_discard(h4_dev, 32);
#endif
	return 0;
}

static int h4_open(void)
{
	int ret;

	BT_DBG("");

#ifdef CONFIG_BT_H4_UART_INTERRUPT_DRIVEN
	uart_irq_rx_disable(h4_dev);
	uart_irq_tx_disable(h4_dev);
	uart_irq_callback_set(h4_dev, bt_uart_isr);
#else
	uart_callback_set(h4_dev, uart_async_callback, NULL);
#endif
	ret = bt_hci_transport_setup(h4_dev);
	if (ret < 0) {
		return -EIO;
	}

	/* wait until it stabilizes */
	k_msleep(80);

#ifndef CONFIG_BT_H4_UART_INTERRUPT_DRIVEN
	ret = rx_enable();
	if (ret < 0) {
		return -EIO;
	}
#endif


	k_thread_create(&rx_thread_data, rx_thread_stack,
			K_THREAD_STACK_SIZEOF(rx_thread_stack),
			rx_thread, NULL, NULL, NULL,
			K_PRIO_COOP(CONFIG_BT_RX_PRIO),
			0, K_NO_WAIT);

	return 0;
}

static const struct bt_hci_driver drv = {
	.name		= "H:4",
	.bus		= BT_HCI_DRIVER_BUS_UART,
	.open		= h4_open,
	.send		= h4_send,
};

static int bt_uart_init(struct device *unused)
{
	ARG_UNUSED(unused);

	h4_dev = device_get_binding(CONFIG_BT_UART_ON_DEV_NAME);
	if (!h4_dev) {
		return -EINVAL;
	}

	bt_hci_driver_register(&drv);

	return 0;
}

SYS_INIT(bt_uart_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

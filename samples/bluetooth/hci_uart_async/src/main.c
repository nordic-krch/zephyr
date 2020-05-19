/*
 * Copyright (c) 2016 Nordic Semiconductor ASA
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <zephyr.h>
#include <arch/cpu.h>
#include <sys/byteorder.h>
#include <logging/log.h>
#include <sys/util.h>

#include <device.h>
#include <init.h>
#include <drivers/uart.h>

#include <net/buf.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <bluetooth/buf.h>
#include <bluetooth/hci_raw.h>

#define LOG_MODULE_NAME hci_uart
LOG_MODULE_REGISTER(LOG_MODULE_NAME, LOG_LEVEL_INF);

static struct device *hci_uart_dev;
static K_THREAD_STACK_DEFINE(tx_thread_stack, CONFIG_BT_HCI_TX_STACK_SIZE);
static struct k_thread tx_thread_data;
static K_FIFO_DEFINE(tx_queue);

#define H4_NONE 0x0
#define H4_CMD 0x01
#define H4_ACL 0x02
#define H4_SCO 0x03
#define H4_EVT 0x04
#define H4_INV 0xff

/* Length of a discard/flush buffer.
 * This is sized to align with a BLE HCI packet:
 * 1 byte H:4 header + 32 bytes ACL/event data
 * Bigger values might overflow the stack since this is declared as a local
 * variable, smaller ones will force the caller to call into discard more
 * often.
 */
#define H4_DISCARD_LEN 33

#define UART_RX_BUF_SIZE 16
#define UART_RX_BUF_COUNT 4

static struct {
	struct net_buf *buf;

	u16_t    remaining;
	u16_t    discard;

	bool     have_hdr;
	u8_t     hdr_len;
	u8_t     type;

	union {
		struct bt_hci_cmd_hdr cmd;
		struct bt_hci_acl_hdr acl;
		u8_t hdr[4];
	};
} rx;

K_MEM_SLAB_DEFINE(rx_pool, UART_RX_BUF_SIZE, UART_RX_BUF_COUNT, 4);

static struct k_sem tx_sem;
static struct net_buf *tx_buf;

static inline int rx_read(void *context, u8_t *dst, int req_len)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
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

static size_t h4_discard(struct device *uart, size_t len)
{
	u8_t buf[H4_DISCARD_LEN];

	return rx_read(uart, buf, MIN(len, sizeof(buf)));
}

static void h4_get_type(void *context)
{
	/* Get packet type */
	if (rx_read(context, &rx.type, 1) != 1) {
		LOG_WRN("Unable to read H:4 packet type");
		rx.type = H4_NONE;
		return;
	}

	switch (rx.type) {
	case H4_CMD:
		rx.remaining = sizeof(rx.cmd);
		rx.hdr_len = rx.remaining;
		break;
	case H4_ACL:
		rx.remaining = sizeof(rx.acl);
		rx.hdr_len = rx.remaining;
		break;
	default:
		LOG_ERR("Unknown H:4 type 0x%02x", rx.type);
		rx.type = H4_NONE;
	}
}

static void get_acl_hdr(void *context)
{
	struct bt_hci_acl_hdr *hdr = &rx.acl;
	int to_read = sizeof(*hdr) - rx.remaining;

	rx.remaining -= rx_read(context, (u8_t *)hdr + to_read, rx.remaining);
	if (!rx.remaining) {
		rx.remaining = sys_le16_to_cpu(hdr->len);
		LOG_DBG("Got ACL header. Payload %u bytes", rx.remaining);
		rx.have_hdr = true;
	}
}

static void get_cmd_hdr(void *context)
{
	struct bt_hci_cmd_hdr *hdr = &rx.cmd;
	int to_read = sizeof(*hdr) - rx.remaining;

	rx.remaining -= rx_read(context, (u8_t *)hdr + to_read, rx.remaining);
	if (!rx.remaining) {
		rx.remaining = sys_le16_to_cpu(hdr->param_len);
		LOG_DBG("Got ACL header. Payload %u bytes", rx.remaining);
		rx.have_hdr = true;
	}
}

static void reset_rx(void)
{
	rx.type = H4_NONE;
	rx.remaining = 0U;
	rx.have_hdr = false;
	rx.hdr_len = 0U;
}

static inline void read_header(void *context)
{
	LOG_DBG("read header, type: %d", rx.type);
	switch (rx.type) {
	case H4_NONE:
		h4_get_type(context);
		return;
	case H4_CMD:
		get_cmd_hdr(context);
		break;
	case H4_ACL:
		get_acl_hdr(context);
		break;
	default:
		CODE_UNREACHABLE;
		return;
	}

	if (rx.have_hdr) {
		rx.buf = bt_buf_get_tx(BT_BUF_H4, K_NO_WAIT, &rx.type,
					    sizeof(rx.type));
		if (rx.remaining > net_buf_tailroom(rx.buf)) {
			LOG_ERR("Not enough space in buffer");
			rx.discard = rx.remaining;
			reset_rx();
			net_buf_unref(rx.buf);
		} else {
			net_buf_add_mem(rx.buf, rx.hdr, rx.hdr_len);
		}
	}
}

static void read_payload(void *context)
{
	int read = rx_read(context, net_buf_tail(rx.buf), rx.remaining);

	rx.buf->len += read;
	rx.remaining -= read;
}

static inline void process_rx(void *context)
{
	LOG_DBG("remaining %u discard %u have_hdr %u rx.buf %p len %u",
	       rx.remaining, rx.discard, rx.have_hdr, rx.buf,
	       rx.buf ? rx.buf->len : 0);

	if (rx.discard) {
		LOG_WRN("discard: %d bytes", rx.discard);
		rx.discard -= h4_discard(context, rx.discard);
		return;
	}

	if (rx.have_hdr) {
		read_payload(context);
		if (!rx.remaining) {
			/* Put buffer into TX queue, thread will dequeue */
			net_buf_put(&tx_queue, rx.buf);
			rx.buf = NULL;
			reset_rx();			
		}
	} else {
		read_header(context);
		if (rx.have_hdr && !rx.remaining) {
			/* Put buffer into TX queue, thread will dequeue */
			net_buf_put(&tx_queue, rx.buf);
			rx.buf = NULL;
			reset_rx();
		}			
	}
}

static void tx_complete(void)
{
	net_buf_unref(tx_buf);
	tx_buf = NULL;
	k_sem_give(&tx_sem);
}

static void start_rx(void)
{
	int err;
	u8_t *buf;

	err = k_mem_slab_alloc(&rx_pool, (void **)&buf, K_NO_WAIT);
	__ASSERT_NO_MSG(err == 0);
	err = uart_rx_enable(hci_uart_dev, buf, UART_RX_BUF_SIZE, 1);
	__ASSERT_NO_MSG(err == 0);
}
#include <hal/nrf_gpio.h>
static void uart_async_callback(struct uart_event *evt, void *user_data)
{
	
	int err;

	switch (evt->type) {
		case UART_TX_DONE:
			tx_complete();
			break;
	case UART_RX_RDY:
		LOG_HEXDUMP_DBG(
			&evt->data.rx.buf[evt->data.rx.offset],
			evt->data.rx.len,
			"rx:");
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
			LOG_ERR("Failed to allocate new RX buffer");
		} else {
			err = uart_rx_buf_rsp(hci_uart_dev, buf,
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
		/*LOG_ERR("RX error occured, reason: %d",
			evt->data.rx_stop.reason);*/
		break;
	case UART_RX_DISABLED:
		start_rx();
		break;
	default:
		LOG_ERR("Unexpected UART event: %d", evt->type);
		break;
	}
}

#if 0
static void bt_uart_isr(struct device *unused)
{
	static struct net_buf *buf;
	static int remaining;

	ARG_UNUSED(unused);

	while (uart_irq_update(hci_uart_dev) &&
	       uart_irq_is_pending(hci_uart_dev)) {
		int read;

		if (!uart_irq_rx_ready(hci_uart_dev)) {
			if (uart_irq_tx_ready(hci_uart_dev)) {
				LOG_DBG("transmit ready");
			} else {
				LOG_DBG("spurious interrupt");
			}
			/* Only the UART RX path is interrupt-enabled */
			break;
		}

		/* Beginning of a new packet */
		if (!remaining) {
			u8_t type;

			/* Get packet type */
			read = h4_read(hci_uart_dev, &type, sizeof(type), 0);
			if (read != sizeof(type)) {
				LOG_WRN("Unable to read H4 packet type");
				continue;
			}

			buf = bt_buf_get_tx(BT_BUF_H4, K_NO_WAIT, &type,
					    sizeof(type));
			if (!buf) {
				return;
			}

			switch (bt_buf_get_type(buf)) {
			case BT_BUF_CMD:
				h4_cmd_recv(buf, &remaining);
				break;
			case BT_BUF_ACL_OUT:
				h4_acl_recv(buf, &remaining);
				break;
			default:
				LOG_ERR("Unknown H4 type %u", type);
				return;
			}

			LOG_DBG("need to get %u bytes", remaining);

			if (remaining > net_buf_tailroom(buf)) {
				LOG_ERR("Not enough space in buffer");
				net_buf_unref(buf);
				buf = NULL;
			}
		}

		if (!buf) {
			read = h4_discard(hci_uart_dev, remaining);
			LOG_WRN("Discarded %d bytes", read);
			remaining -= read;
			continue;
		}

		read = h4_read(hci_uart_dev, net_buf_tail(buf), remaining, 0);

		buf->len += read;
		remaining -= read;

		LOG_DBG("received %d bytes", read);

		if (!remaining) {
			LOG_DBG("full packet received");

			/* Put buffer into TX queue, thread will dequeue */
			net_buf_put(&tx_queue, buf);
			buf = NULL;
		}
	}
}
#endif

static void tx_thread(void *p1, void *p2, void *p3)
{
	while (1) {
		struct net_buf *buf;
		int err;

		/* Wait until a buffer is available */
		buf = net_buf_get(&tx_queue, K_FOREVER);
		/* Pass buffer to the stack */
		err = bt_send(buf);
		if (err) {
			LOG_ERR("Unable to send (err %d)", err);
			net_buf_unref(buf);
		}

		/* Give other threads a chance to run if tx_queue keeps getting
		 * new data all the time.
		 */
		k_yield();
	}
}

#if defined(CONFIG_BT_CTLR_ASSERT_HANDLER)
void bt_ctlr_assert_handle(char *file, u32_t line)
{
	u32_t len = 0U, pos = 0U;


	if (file) {
		while (file[len] != '\0') {
			if (file[len] == '/') {
				pos = len + 1;
			}
			len++;
		}
		file += pos;
		len -= pos;
	}

	uart_poll_out(hci_uart_dev, H4_EVT);
	/* Vendor-Specific debug event */
	uart_poll_out(hci_uart_dev, 0xff);
	/* 0xAA + strlen + \0 + 32-bit line number */
	uart_poll_out(hci_uart_dev, 1 + len + 1 + 4);
	uart_poll_out(hci_uart_dev, 0xAA);

	if (len) {
		while (*file != '\0') {
			uart_poll_out(hci_uart_dev, *file);
			file++;
		}
		uart_poll_out(hci_uart_dev, 0x00);
	}

	uart_poll_out(hci_uart_dev, line >> 0 & 0xff);
	uart_poll_out(hci_uart_dev, line >> 8 & 0xff);
	uart_poll_out(hci_uart_dev, line >> 16 & 0xff);
	uart_poll_out(hci_uart_dev, line >> 24 & 0xff);

	/* Disable interrupts, this is unrecoverable */
	(void)irq_lock();
	while (1) {
	}
}
#endif /* CONFIG_BT_CTLR_ASSERT_HANDLER */

static int hci_uart_init(struct device *unused)
{
	LOG_DBG("");

	/* Derived from DT's bt-c2h-uart chosen node */
	hci_uart_dev = device_get_binding(CONFIG_BT_CTLR_TO_HOST_UART_DEV_NAME);
	if (!hci_uart_dev) {
		return -EINVAL;
	}

	uart_callback_set(hci_uart_dev, uart_async_callback, NULL);
	start_rx();

	return 0;
}

DEVICE_INIT(hci_uart, "hci_uart", &hci_uart_init, NULL, NULL,
	    APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

void main(void)
{
	/* incoming events and data from the controller */
	static K_FIFO_DEFINE(rx_queue);
	int err;

	LOG_DBG("Start");
	__ASSERT(hci_uart_dev, "UART device is NULL");

	k_sem_init(&tx_sem, 0, 1);

	/* Enable the raw interface, this will in turn open the HCI driver */
	bt_enable_raw(&rx_queue);

	if (IS_ENABLED(CONFIG_BT_WAIT_NOP)) {
		/* Issue a Command Complete with NOP */
		int i;

		const struct {
			const u8_t h4;
			const struct bt_hci_evt_hdr hdr;
			const struct bt_hci_evt_cmd_complete cc;
		} __packed cc_evt = {
			.h4 = H4_EVT,
			.hdr = {
				.evt = BT_HCI_EVT_CMD_COMPLETE,
				.len = sizeof(struct bt_hci_evt_cmd_complete),
			},
			.cc = {
				.ncmd = 1,
				.opcode = sys_cpu_to_le16(BT_OP_NOP),
			},
		};

		for (i = 0; i < sizeof(cc_evt); i++) {
			uart_poll_out(hci_uart_dev,
				      *(((const u8_t *)&cc_evt)+i));
		}
	}


	/* Spawn the TX thread and start feeding commands and data to the
	 * controller
	 */
	k_thread_create(&tx_thread_data, tx_thread_stack,
			K_THREAD_STACK_SIZEOF(tx_thread_stack), tx_thread,
			NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);

	while (1) {
		if (tx_buf == NULL) {
			tx_buf = net_buf_get(&rx_queue, K_FOREVER);
			LOG_DBG("buf %p type %u len %u",
				tx_buf, bt_buf_get_type(tx_buf), tx_buf->len);
			err = uart_tx(hci_uart_dev, tx_buf->data, tx_buf->len,
					1000);
			if (err < 0) {
				LOG_ERR("Failed to send (err: %d)", err);
			}
		} else {
			k_sem_take(&tx_sem, K_FOREVER);
		}
	}
}

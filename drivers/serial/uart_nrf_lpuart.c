/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <drivers/uart.h>
#include <drivers/gpio.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_gpiote.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(lpuart, CONFIG_NRF_LPUART_LOG_LEVEL);

struct bidir_gpio {
	struct gpio_callback callback;
	struct device *port;
	int pin;
	u8_t nrf_pin;
	u8_t ch;
	bool req;
};

enum rx_state {
	RX_OFF,
	RX_IDLE,
	RX_ACTIVE,
	RX_TO_IDLE,
	RX_TO_OFF,
};

struct lpuart_data {
	struct device *uart;
	struct bidir_gpio req_pin;
	struct bidir_gpio rsp_pin;

	struct k_timer tx_timer;
	const u8_t *tx_buf;
	size_t tx_len;
	bool tx_active;

	u8_t *rx_buf;
	size_t rx_len;
	s32_t rx_timeout;

	uart_callback_t user_callback;
	void *user_data;

	enum rx_state rx_state;
	bool rx_req;
};

struct lpuart_config {
	const char *uart_name;
	const char *req_port_name;
	const char *rsp_port_name;
	u8_t req_pin;
	u8_t rsp_pin;
};

static inline struct lpuart_data *get_dev_data(struct device *dev)
{
	return dev->driver_data;
}

static inline const struct lpuart_config *get_dev_config(struct device *dev)
{
	return dev->config_info;
}

static void ctrl_pin_set(struct bidir_gpio *io)
{
	int key = irq_lock();

	/* note that there is still a very small chance that if ZLI is used
	 * then it may be interrupted by ZLI and if during that time receiver
	 * clears the pin and sets it high again we may miss it.
	 * This might be solved in the next iteration but it will required
	 * extension to the UART driver. */
	nrf_gpiote_te_default(NRF_GPIOTE, io->ch);
	nrf_gpiote_event_configure(NRF_GPIOTE, io->ch, io->nrf_pin,
					NRF_GPIOTE_POLARITY_HITOLO);

	nrf_gpio_cfg_input(io->nrf_pin, NRF_GPIO_PIN_PULLUP);
	nrf_gpiote_event_enable(NRF_GPIOTE, io->ch);

	irq_unlock(key);
}

static void ctrl_pin_clear(struct bidir_gpio *io)
{
	nrf_gpio_pin_clear(io->nrf_pin);
	nrf_gpiote_te_default(NRF_GPIOTE, io->ch);
	nrf_gpio_cfg_output(io->nrf_pin);
	return;
}

static void ctrl_pin_idle(struct bidir_gpio *io)
{
	if (io->req) {
		ctrl_pin_clear(io);
		return;
	}

	nrf_gpiote_te_default(NRF_GPIOTE, io->ch);
	nrf_gpiote_event_configure(NRF_GPIOTE, io->ch, io->nrf_pin,
					NRF_GPIOTE_POLARITY_LOTOHI);

	nrf_gpio_cfg_input(io->nrf_pin, NRF_GPIO_PIN_NOPULL);
	nrf_gpiote_event_enable(NRF_GPIOTE, io->ch);
}

static void activate_rx(struct lpuart_data *data)
{
	int err;

	if (data->rx_buf == NULL)
	{
		/* TODO overrun error */
	}

	err = uart_rx_enable(data->uart, data->rx_buf,
				data->rx_len, data->rx_timeout);
	if (err < 0) {
		LOG_ERR("Enabling RX failed (err:%d)", err);
		data->rx_buf = NULL;
		/* TODO notify error */
	} else {
		/* Ready. Confirm by toggling the pin. */
		ctrl_pin_clear(&data->rsp_pin);
		ctrl_pin_set(&data->rsp_pin);
		LOG_DBG("Receiver ready");
		data->rx_req = false;
		data->rx_state = RX_ACTIVE;
	}
}

static void deactivate_rx(struct lpuart_data *data)
{
	int err;

	ctrl_pin_idle(&data->rsp_pin);
	if (nrf_gpio_pin_read(data->rsp_pin.nrf_pin)) {
		LOG_DBG("Request pending while deactivating");
		/* pin is set high, another request pending. */
		nrf_gpiote_event_clear(NRF_GPIOTE,
				     nrf_gpiote_in_event_get(data->rsp_pin.ch));
		data->rx_req = true;
	}
	/* abort rx */
	data->rx_state = RX_TO_IDLE;
	//data->rx_state = RX_IDLE;
	err = uart_rx_disable(data->uart);
	if (err < 0) {
		/* todo notify error, which */
		LOG_ERR("Failed to disable uart (err: %d)", err);
	}
}

static void tx_complete(struct lpuart_data *data)
{
	ctrl_pin_idle(&data->req_pin);
	data->tx_buf = NULL;
	data->tx_active = false;
}

static void gpio_handler(struct device *port,
			struct gpio_callback *cb,
			gpio_port_pins_t pins)
{
	struct bidir_gpio *io = CONTAINER_OF(cb, struct bidir_gpio, callback);
	struct lpuart_data *data;
	int err;

	if (io->req) {
		int key;
		const u8_t *buf;
		size_t len;

		data = CONTAINER_OF(io, struct lpuart_data, req_pin);

		if (data->tx_buf == NULL) {
			LOG_WRN("request confirmed but no data to send");
			tx_complete(data);
			/* aborted */
			return;
		}

		LOG_DBG("%d: RX confirmed. TX Can start", io->nrf_pin);
		k_timer_stop(&data->tx_timer);

		key = irq_lock();
		data->tx_active = true;
		buf = data->tx_buf;
		len = data->tx_len;
		irq_unlock(key);
		err = uart_tx(data->uart, buf, len, 0);
		if (err < 0) {
			LOG_ERR("TX not started (error: %d)", err);
			tx_complete(data);
		}
		return;
	}

	data = CONTAINER_OF(io, struct lpuart_data, rsp_pin);

	if (nrf_gpiote_event_polarity_get(NRF_GPIOTE, io->ch)
		== NRF_GPIOTE_POLARITY_LOTOHI) {
		__ASSERT_NO_MSG(data->rx_state != RX_ACTIVE);

		LOG_DBG("Transfer request.");
		data->rx_req = true;
		if (data->rx_state == RX_IDLE) {
			activate_rx(data);
		}
	} else {
		__ASSERT_NO_MSG(data->rx_state == RX_ACTIVE);

		LOG_DBG("TX end, RX to idle");
		deactivate_rx(data);
	}
}

static int ctrl_pin_configure(struct bidir_gpio *io, struct device *port,
				int pin, bool req)
{
	int err;

	io->callback.handler = gpio_handler;
	io->callback.pin_mask = BIT(pin);
	io->pin = pin;
	io->port = port;
	io->req = req;
	io->nrf_pin = pin +
		((io->port == device_get_binding("GPIO_0")) ? 0 : 32);

	err = gpio_pin_configure(io->port, io->pin, GPIO_INPUT);
	if (err < 0) {
		return err;
	}

	err = gpio_add_callback(io->port, &io->callback);
	if (err < 0) {
		return err;
	}

	err = gpio_enable_callback(io->port, io->pin);
	if (err < 0) {
		return err;
	}

	err = gpio_pin_interrupt_configure(io->port, io->pin, req ?
				GPIO_INT_EDGE_FALLING : GPIO_INT_EDGE_RISING);
	if (err < 0) {
		return err;
	}

	for (size_t i = 0; i < GPIOTE_CH_NUM; i++) {
		if (nrf_gpiote_event_pin_get(NRF_GPIOTE, i) == io->nrf_pin) {
			io->ch = i;
			break;
		}
	}

	ctrl_pin_idle(io);

	LOG_DBG("Pin %d configured, gpiote ch:%d, mode:%s",
		io->nrf_pin, io->ch, req ? "req" : "rsp");
	return 0;
}

static int api_callback_set(struct device *dev, uart_callback_t callback,
			    void *user_data)
{
	struct lpuart_data *data = get_dev_data(dev);

	data->user_callback = callback;
	data->user_data = user_data;

	return 0;
}

static void user_callback(struct lpuart_data *data, struct uart_event *evt)
{
	if (data->user_callback) {
		data->user_callback(evt, data->user_data);
	}
}

static void uart_callback(struct uart_event *evt, void *user_data)
{
	struct device *dev = user_data;
	struct lpuart_data *data = get_dev_data(dev);

	switch (evt->type) {
	case UART_TX_DONE:
		tx_complete(data);
		user_callback(data, evt);
		break;

	case UART_TX_ABORTED:
		LOG_DBG("tx aborted");
		user_callback(data, evt);
		break;

	case UART_RX_RDY:
		LOG_DBG("RXRDY buf:%p, offset: %d,len: %d",
		     evt->data.rx.buf, evt->data.rx.offset, evt->data.rx.len);
		user_callback(data, evt);
		break;

	case UART_RX_BUF_REQUEST:
		/* If packet will fit in the provided buffer do not request
		 * additional buffer.
		 */
		if (data->rx_len < CONFIG_NRF_LPUART_MAX_PACKET_SIZE) {
			user_callback(data, evt);
		}
		break;

	case UART_RX_BUF_RELEASED:
		user_callback(data, evt);
		break;

	case UART_RX_DISABLED:
		__ASSERT_NO_MSG((data->rx_state != RX_ACTIVE) &&
			 (data->rx_state != RX_IDLE) &&
			 (data->rx_state != RX_OFF));

		if (data->rx_state == RX_TO_IDLE) {
			/* Need to request new buffer since uart was disabled */
			evt->type = UART_RX_BUF_REQUEST;
		} else if (data->rx_state == RX_TO_OFF){
			data->rx_state = RX_OFF;
		}
		user_callback(data, evt);
		break;
	case UART_RX_STOPPED:
		user_callback(data, evt);
		break;
	}
}

static void tx_timeout(struct k_timer *timer)
{
	struct device *dev = k_timer_user_data_get(timer);
	struct lpuart_data *data = get_dev_data(dev);
	int err;

	if (data->tx_active) {
		err = uart_tx_abort(data->uart);
		if (err == -EFAULT) {
			LOG_DBG("No active transfer. Already finished?");
		} else if (err < 0) {
			__ASSERT(0, "Unexpected tx_abort error:%d", err);
		}
		return;
	}

	tx_complete(data);
}

static int api_tx(struct device *dev, const u8_t *buf, size_t len, s32_t timeout)
{
	struct lpuart_data *data = get_dev_data(dev);

	if (atomic_ptr_cas((atomic_ptr_t *)&data->tx_buf, NULL, (void *)buf)
		== false) {
		return -EBUSY;
	}
	LOG_DBG("tx len:%d", len);
	data->tx_len = len;
	k_timer_start(&data->tx_timer, SYS_TIMEOUT_MS(timeout), K_NO_WAIT);

	ctrl_pin_set(&data->req_pin);

	return 0;
}

static int api_tx_abort(struct device *dev)
{
	struct lpuart_data *data = get_dev_data(dev);
	int err;
	int key;
	const u8_t *buf = data->tx_buf;

	if (data->tx_buf == NULL) {
		return -EFAULT;
	}

	k_timer_stop(&data->tx_timer);
	key = irq_lock();
	tx_complete(data);
	irq_unlock(key);

	err = uart_tx_abort(data->uart);
	if (err != -EFAULT) {
		/* if successfully aborted or returned error different than
		 * one indicating that there is no transfer, return error code.
		 */
		return err;
	}

	struct uart_event event = {
		.type = UART_TX_ABORTED,
		.data = {
			.tx = {
				.buf = buf,
				.len = 0
			}
		}
	};

	user_callback(data, &event);

	return err;
}

static int api_rx_enable(struct device *dev, u8_t *buf, size_t len, s32_t timeout)
{
	struct lpuart_data *data = get_dev_data(dev);

	__ASSERT_NO_MSG(data->rx_state == RX_OFF);

	if (atomic_ptr_cas((atomic_ptr_t *)&data->rx_buf, NULL, buf) == false) {
		return -EBUSY;
	}

	data->rx_len = len;
	data->rx_timeout = timeout;
	data->rx_state = RX_IDLE;

	LOG_DBG("Enabling RX");

	int key = irq_lock();
	bool pending_rx = nrf_gpio_pin_read(data->rsp_pin.nrf_pin) &&
		(data->rx_state == RX_IDLE);
	irq_unlock(key);

	if (pending_rx) {
		activate_rx(data);
	}

	return 0;
}

static int api_rx_buf_rsp(struct device *dev, u8_t *buf, size_t len)
{
	struct lpuart_data *data = get_dev_data(dev);

	__ASSERT_NO_MSG((data->rx_state != RX_OFF) &&
		 (data->rx_state != RX_TO_OFF));

	if (data->rx_state == RX_TO_IDLE) {
		data->rx_buf = buf;
		data->rx_len = len;

		if (data->rx_req) {
			LOG_DBG("Pending RX request. Activating RX");
			activate_rx(data);
		} else {
			data->rx_state = RX_IDLE;
			LOG_DBG("RX Idle");
		}

		return 0;
	}

	return uart_rx_buf_rsp(data->uart, buf, len);
}

static int api_rx_disable(struct device *dev)
{
	struct lpuart_data *data = get_dev_data(dev);

	data->rx_state = RX_TO_OFF;

	return uart_rx_disable(data->uart);
}

static int lpuart_init(struct device *dev)
{
	struct lpuart_data *data = get_dev_data(dev);
	const struct lpuart_config *cfg = get_dev_config(dev);
	struct device *req_port;
	struct device *rsp_port;
	int err;

	data->uart = device_get_binding(cfg->uart_name);
	if (data->uart == NULL) {
		return -ENODEV;
	}

	req_port = device_get_binding(cfg->req_port_name);
	if (req_port == NULL) {
		return -ENODEV;
	}

	rsp_port = device_get_binding(cfg->rsp_port_name);
	if (rsp_port == NULL) {
		return -ENODEV;
	}

	err = ctrl_pin_configure(&data->req_pin, req_port, cfg->req_pin, true);
	if (err < 0) {
		return -EINVAL;
	}

	err = ctrl_pin_configure(&data->rsp_pin, rsp_port, cfg->rsp_pin, false);
	if (err < 0) {
		return -EINVAL;
	}

	k_timer_init(&data->tx_timer, tx_timeout, NULL);
	k_timer_user_data_set(&data->tx_timer, dev);

	return uart_callback_set(data->uart, uart_callback, (void *)dev);
}

#define UARTE(idx)			DT_NODELABEL(UTIL_CAT(uart,idx))

static const struct lpuart_config lpuart_config = {
	.uart_name = DT_LABEL(UARTE(CONFIG_NRF_LPUART_UARTE_INSTANCE)),
	.req_port_name = (CONFIG_NRF_LPUART_TXCTRL_PIN >= 32) ?
			"GPIO_1" : "GPIO_0",
	.rsp_port_name = (CONFIG_NRF_LPUART_RXCTRL_PIN >= 32) ?
			"GPIO_1" : "GPIO_0",
	.req_pin = (CONFIG_NRF_LPUART_TXCTRL_PIN >= 32) ?
			(CONFIG_NRF_LPUART_TXCTRL_PIN - 32):
			CONFIG_NRF_LPUART_TXCTRL_PIN,
	.rsp_pin = (CONFIG_NRF_LPUART_RXCTRL_PIN >= 32) ?
			(CONFIG_NRF_LPUART_RXCTRL_PIN - 32) :
			CONFIG_NRF_LPUART_RXCTRL_PIN
};

static struct lpuart_data lpuart_data;

static const struct uart_driver_api lpuart_api = {
	.callback_set = api_callback_set,
	.tx = api_tx,
	.tx_abort = api_tx_abort,
	.rx_enable = api_rx_enable,
	.rx_buf_rsp = api_rx_buf_rsp,
	.rx_disable = api_rx_disable,
};

DEVICE_AND_API_INIT(lpuart, "LPUART", lpuart_init, &lpuart_data,
		    &lpuart_config, POST_KERNEL,
		    CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &lpuart_api);

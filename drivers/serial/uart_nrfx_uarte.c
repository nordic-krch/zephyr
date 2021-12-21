/*
 * Copyright (c) 2018-2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Driver for Nordic Semiconductor nRF UARTE
 */

#include <drivers/uart.h>
#include <pm/device.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_uarte.h>
#include <nrfx_timer.h>
#include <nrfx_uarte.h>
#include <sys/util.h>
#include <kernel.h>
#include <logging/log.h>
#include <helpers/nrfx_gppi.h>
#include <nrfx_egu.h>
#include "uart_async_to_irq.h"
#include <drivers/pinctrl.h>

#define LOG_MODULE_NAME uarte

LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_UART_LOG_LEVEL);

/* Generalize PPI or DPPI channel management */
#if defined(CONFIG_HAS_HW_NRF_PPI)
#include <nrfx_ppi.h>
#define gppi_channel_t nrf_ppi_channel_t
#define gppi_channel_alloc nrfx_ppi_channel_alloc
#define gppi_channel_enable nrfx_ppi_channel_enable
#elif defined(CONFIG_HAS_HW_NRF_DPPIC)
#include <nrfx_dppi.h>
#define gppi_channel_t uint8_t
#define gppi_channel_alloc nrfx_dppi_channel_alloc
#define gppi_channel_enable nrfx_dppi_channel_enable
#else
#error "No PPI or DPPI"
#endif


#if	(defined(CONFIG_UART_0_NRF_UARTE) &&	     \
	 defined(CONFIG_UART_0_INTERRUPT_DRIVEN)) || \
	(defined(CONFIG_UART_1_NRF_UARTE) &&	     \
	 defined(CONFIG_UART_1_INTERRUPT_DRIVEN)) || \
	(defined(CONFIG_UART_2_NRF_UARTE) &&	     \
	 defined(CONFIG_UART_2_INTERRUPT_DRIVEN)) || \
	(defined(CONFIG_UART_3_NRF_UARTE) &&	     \
	 defined(CONFIG_UART_3_INTERRUPT_DRIVEN))
	#define UARTE_INTERRUPT_DRIVEN	1
#else
	#define UARTE_INTERRUPT_DRIVEN	0
#endif

#if defined(UARTE_CONFIG_PARITYTYPE_Msk)
#define UARTE_ODD_PARITY_ALLOWED 1
#else
#define UARTE_ODD_PARITY_ALLOWED 0
#endif


#define INT_DRIVEN_USE_EGU(idx, _) \
	IS_ENABLED(CONFIG_UART_##idx##_INT_DRIVEN_USE_EGU) +

/* Determine if any instance is using egu. */
#define USE_EGU_TRAMPOLINE (UTIL_LISTIFY(UARTE_COUNT, INT_DRIVEN_USE_EGU) 0)

/* Macro for converting numerical baudrate to register value. It is convenient
 * to use this approach because for constant input it can calculate nrf setting
 * at compile time.
 */
#define NRF_BAUDRATE(baudrate) ((baudrate) == 300 ? 0x00014000 : \
			((baudrate) == 600    ? 0x00027000 : \
			((baudrate) == 1200   ? NRF_UARTE_BAUDRATE_1200 : \
			((baudrate) == 2400   ? NRF_UARTE_BAUDRATE_2400 : \
			((baudrate) == 4800   ? NRF_UARTE_BAUDRATE_4800 : \
			((baudrate) == 9600   ? NRF_UARTE_BAUDRATE_9600 : \
			((baudrate) == 14400  ? NRF_UARTE_BAUDRATE_14400 : \
			((baudrate) == 19200  ? NRF_UARTE_BAUDRATE_19200 : \
			((baudrate) == 28800  ? NRF_UARTE_BAUDRATE_28800 : \
			((baudrate) == 31250  ? NRF_UARTE_BAUDRATE_31250 : \
			((baudrate) == 38400  ? NRF_UARTE_BAUDRATE_38400 : \
			((baudrate) == 56000  ? NRF_UARTE_BAUDRATE_56000 : \
			((baudrate) == 57600  ? NRF_UARTE_BAUDRATE_57600 : \
			((baudrate) == 76800  ? NRF_UARTE_BAUDRATE_76800 : \
			((baudrate) == 115200 ? NRF_UARTE_BAUDRATE_115200 : \
			((baudrate) == 230400 ? NRF_UARTE_BAUDRATE_230400 : \
			((baudrate) == 250000 ? NRF_UARTE_BAUDRATE_250000 : \
			((baudrate) == 460800 ? NRF_UARTE_BAUDRATE_460800 : \
			((baudrate) == 921600 ? NRF_UARTE_BAUDRATE_921600 : \
			((baudrate) == 1000000) ? NRF_UARTE_BAUDRATE_1000000 : \
			 0)))))))))))))))))))
/*
 * RX timeout is divided into time slabs, this define tells how many divisions
 * should be made. More divisions - higher timeout accuracy and processor usage.
 */
#define RX_TIMEOUT_DIV 5

/* Size of hardware fifo in RX path. */
#define UARTE_HW_RX_FIFO_SIZE 5

struct uarte_rx_data {
	struct k_timer timer;
	uint8_t t_countdown;
	k_timeout_t timeout;

	uint8_t flush_cnt;

	uint16_t len;
	uint8_t *buf;
	uint16_t offset;
	uint16_t next_len;
	uint8_t *next_buf;
	uint8_t *bbb; /* Byte by byte buf */

	uint32_t last_report_cnt;
	uint32_t last_cnt;
	uint32_t curr_cnt;

	uint16_t buf_cnt_down;
};

struct uarte_tx_data {
	struct k_timer timer;
	const uint8_t *buf;
	size_t len;
	size_t cache_offset;
	uint8_t cache_buf[8];
};

#define UARTE_DATA_FLAG_OFF BIT(0)
#define UARTE_DATA_FLAG_HW_RX_COUNT BIT(1)
#define UARTE_DATA_FLAG_RX_RDY_REPORTING BIT(2)
#define UARTE_DATA_FLAG_RX_TIMEOUT_SETUP BIT(3)
#define UARTE_DATA_FLAG_IN_RX_DONE_IRQ BIT(4)
#define UARTE_DATA_FLAG_RX_ACTIVE BIT(5)

#define UARTE_DATA_FLAG_ERROR_SHIFT 8
#define UARTE_DATA_FLAG_ERROR_BITS 8
#define UARTE_DATA_FLAG_GET_ERROR(flags) \
	((flags >> UARTE_DATA_FLAG_ERROR_SHIFT) & BIT_MASK(UARTE_DATA_FLAG_ERROR_BITS))

struct uarte_async_data {
	uart_callback_t user_callback;
	void *user_data;

	struct uarte_rx_data rx;
	struct uarte_tx_data tx;
};

struct uart_nrfx_a2i {
	struct uart_async_to_irq_data data;
	struct k_timer *timer;
	const nrfx_egu_t *egu;
};

/* Device data structure */
struct uarte_nrfx_data {
	struct uart_nrfx_a2i *a2i_data;
	struct uarte_async_data *async;
	atomic_t flags;
	struct uart_config config;
	uint8_t rx_byte;
};

#define UARTE_LOW_POWER_TX BIT(0)
#define UARTE_LOW_POWER_RX BIT(1)

/* If enabled, pins are managed when going to low power mode. */
#define UARTE_CFG_FLAG_GPIO_MGMT   BIT(0)

/* If enabled then ENDTX is PPI'ed to TXSTOP */
#define UARTE_CFG_FLAG_PPI_ENDTX   BIT(1)

/* If enabled then TIMER is used for RX byte counting. */
#define UARTE_CFG_FLAG_HW_RX_COUNT BIT(2)

/* If set receiver is not used. */
#define UARTE_CFG_FLAG_NO_RX BIT(3)

/* If set receiver is not used. */
#define UARTE_CFG_FLAG_INTERRUPT_DRIVEN_API BIT(4)

struct uarte_nrfx_psel_config {
	uint32_t tx_pin;
	uint32_t rx_pin;
	uint32_t cts_pin;
	uint32_t rts_pin;
	bool rx_pull_up;
	bool cts_pull_up;
};

union uarte_nrfx_pin_config {
	const struct uarte_nrfx_psel_config *psel;
	const struct pinctrl_dev_config *pinctrl;
};

/**
 * @brief Structure for UARTE configuration.
 */
struct uarte_nrfx_config {
	nrfx_uarte_t instance; /* nrfx driver instance */
	nrfx_uarte_config_t nrfx_config;
	union uarte_nrfx_pin_config pin_config;
	struct uart_config config;
	uint32_t flags;
	nrf_gpio_pin_pull_t rxd_pull; /* RXD pin pull configuration */
	nrf_gpio_pin_pull_t cts_pull; /* CTS pin pull configuration */
	nrfx_timer_t timer;
	LOG_INSTANCE_PTR_DECLARE(log);
};

#define GET_LOG(dev) get_dev_config(dev)->log

static inline struct uarte_nrfx_data *get_dev_data(const struct device *dev)
{
	return dev->data;
}

static inline const struct uarte_nrfx_config *get_dev_config(const struct device *dev)
{
	return dev->config;
}

static inline bool is_sync_api(const struct device *dev)
{
	struct uarte_nrfx_data *data = get_dev_data(dev);

	return data->async == NULL;
}

static inline bool is_int_driven_api(const struct device *dev)
{
	if (IS_ENABLED(CONFIG_UART_ASYNC_TO_INT_DRIVEN_API) &&
	    get_dev_config(dev)->flags & UARTE_CFG_FLAG_INTERRUPT_DRIVEN_API) {
		return true;
	}

	return false;
}

static inline bool is_async_api(const struct device *dev)
{
	struct uarte_nrfx_data *data = get_dev_data(dev);

	if (data->async &&
	    (!IS_ENABLED(CONFIG_UART_ASYNC_TO_INT_DRIVEN_API) ||
	    !(get_dev_config(dev)->flags & UARTE_CFG_FLAG_INTERRUPT_DRIVEN_API))) {
		return true;
	}

	return false;
}

#define NRF_CONFIG_HWFC(config) \
	(config == UART_CFG_FLOW_CTRL_RTS_CTS ? \
		NRF_UARTE_HWFC_ENABLED :\
		((config == UART_CFG_FLOW_CTRL_DTR_DSR) ? \
		(nrf_uarte_hwfc_t)-1 : NRF_UARTE_HWFC_DISABLED))

#define NRF_CONFIG_PARITY(config, odd_allowed) \
	(config == UART_CFG_PARITY_NONE ? NRF_UARTE_PARITY_EXCLUDED : \
		(config == UART_CFG_PARITY_EVEN ? NRF_UARTE_PARITY_INCLUDED : \
			(odd_allowed ? NRF_UARTE_PARITY_INCLUDED : (nrf_uarte_parity_t)-1)))

#define NRF_CONFIG_STOP(config) \
	(config == UART_CFG_STOP_BITS_1 ? NRF_UARTE_STOP_ONE : \
	 (config == UART_CFG_STOP_BITS_2 ? NRF_UARTE_STOP_TWO : (nrf_uarte_stop_t)-1))

#define NRF_CONFIG_PARITYTYPE(config) \
	((config == UART_CFG_PARITY_NONE || config == UART_CFG_PARITY_EVEN) ? \
	 	NRF_UARTE_PARITYTYPE_EVEN : \
		(config == UART_CFG_PARITY_ODD ? NRF_UARTE_PARITYTYPE_ODD : \
		 		(nrf_uarte_paritytype_t)-1))

/* Macro for initializing nrf_uarte configuration structure based on API configuration. */
#define NRF_UARTE_CONFIG(config, odd_allowed) { \
	.hwfc = NRF_CONFIG_HWFC((config).flow_ctrl), \
	.parity = NRF_CONFIG_PARITY((config).parity, odd_allowed), \
	.stop = NRF_CONFIG_STOP((config).stop_bits), \
	IF_ENABLED(UARTE_ODD_PARITY_ALLOWED, \
		(.paritytype = NRF_CONFIG_PARITYTYPE((config).parity),)) \
}

#define NRF_UARTE_CONFIG_VALIDATE_CAN_OPT(config) \
	(sizeof(config.hwfc) == 1 && sizeof(config.parity) == 1 && \
	 sizeof(config.stop) == 1 \
	 IF_ENABLED(UARTE_ODD_PARITY_ALLOWED, (&& sizeof(config.paritytype) == 1)))

/* Optimized check is based on assumption that enums are byte size and no
 * valid value is setting highest bit. So when bit is set it means that field
 * was set to -1. Validity check can then be done with single "and" operation.
 */
#define NRF_UARTE_CONFIG_VALIDATE_OPT(config) \
		(*(uint32_t *)&config & 0x80808080 ? -ENOTSUP : 0)

#define NRF_UARTE_CONFIG_VALIDATE_NO_OPT(config) \
	((config.hwfc == (nrf_uarte_hwfc_t)-1 || \
	 config.parity == (nrf_uarte_parity_t)-1 || \
	 config.stop == (nrf_uarte_stop_t)-1 \
	 IF_ENABLED(UARTE_ODD_PARITY_ALLOWED, \
		 (|| config.paritytype == (nrf_uarte_paritytype_t)-1))) ? -ENOTSUP : 0)

/* Macro for checking if new configuration is valid. Any invalid field is set
 * to -1. There is an optimized version of that macro which is applied when
 * possible.
 */
#define NRF_UARTE_CONFIG_VALIDATE(config) \
	NRF_UARTE_CONFIG_VALIDATE_CAN_OPT(config) ? \
		NRF_UARTE_CONFIG_VALIDATE_OPT(config) : \
		NRF_UARTE_CONFIG_VALIDATE_NO_OPT(config)

static int uarte_nrfx_configure(const struct device *dev,
				const struct uart_config *cfg)
{
	NRF_UARTE_Type *reg = get_dev_config(dev)->instance.p_reg;
	nrf_uarte_config_t uarte_cfg = NRF_UARTE_CONFIG(*cfg, UARTE_ODD_PARITY_ALLOWED);
	nrf_uarte_baudrate_t baudrate = NRF_BAUDRATE(cfg->baudrate);
	int err;

	if (baudrate == 0) {
		return -ENOTSUP;
	}

	err = NRF_UARTE_CONFIG_VALIDATE(uarte_cfg);
	if (err < 0) {
		return err;
	}

	/* TODO need to disable? */
	nrf_uarte_baudrate_set(reg, baudrate);
	nrf_uarte_configure(reg, &uarte_cfg);
	get_dev_data(dev)->config = *cfg;

	return 0;
}

static int uarte_nrfx_config_get(const struct device *dev,
				 struct uart_config *cfg)
{
	/* Use dynamic config if set, else static one. */
	const struct uart_config *c = get_dev_data(dev)->config.baudrate ?
		&get_dev_data(dev)->config : &get_dev_config(dev)->config;

	*cfg = *c;
	return 0;
}

static int uarte_nrfx_err_check(const struct device *dev)
{
	return UARTE_DATA_FLAG_GET_ERROR(get_dev_data(dev)->flags);
}

static int ppi_setup(uint32_t evt, uint32_t tsk)
{
	uint8_t ch;
	nrfx_err_t err = nrfx_gppi_channel_alloc(&ch);

	if (err != NRFX_SUCCESS) {
		return -ENOMEM;
	}

	nrfx_gppi_channel_endpoints_setup(ch, evt, tsk);
	nrfx_gppi_channels_enable(BIT(ch));

	return ch;
}

static void uarte_nrfx_poll_out(const struct device *dev, unsigned char c)
{
	nrf_gpio_cfg_output(3);
	nrf_gpio_pin_set(3);
	nrfx_err_t err;
	static const uint32_t flags = NRFX_UARTE_TX_BLOCKING | NRFX_UARTE_TX_EARLY_RETURN;

	while ((err = nrfx_uarte_tx(&get_dev_config(dev)->instance, &c, 1, flags)) ==
			NRFX_ERROR_BUSY) {
	}

	if (err != NRFX_SUCCESS) {
		while(1);
	}
	nrf_gpio_pin_clear(3);
}

static void tx_timeout(struct k_timer *timer)
{
	const struct device *dev = (const struct device *)k_timer_user_data_get(timer);

	(void)nrfx_uarte_tx_abort(&get_dev_config(dev)->instance, false);
}

/* Setup cache buffer (used for sending data from RO memmory).
 * During setup data is copied to cache buffer and transfer length is set.
 *
 * @return True if cache was set, false if no more data to put in cache.
 */
static bool setup_tx_cache(struct uarte_nrfx_data *data, size_t *len)
{
	size_t remaining = data->async->tx.len - data->async->tx.cache_offset;

	if (!remaining) {
		data->async->tx.cache_offset = 0;

		return false;
	}

	*len = MIN(remaining, sizeof(data->async->tx.cache_buf));
	memcpy(data->async->tx.cache_buf, &data->async->tx.buf[data->async->tx.cache_offset], *len);

	return true;
}

static int uarte_nrfx_tx(const struct device *dev, const uint8_t *buf,
			 size_t len,
			 int32_t timeout)
{
	nrfx_err_t err;
	struct uarte_nrfx_data *data = get_dev_data(dev);
	const uint8_t *xfer_buf;
	size_t xfer_len;

	/* If powered down drop any transfer request. */
	if (data->flags & UARTE_DATA_FLAG_OFF) {
		return -ENOTSUP;
	}

	if (!atomic_ptr_cas((atomic_ptr_t *)&data->async->tx.buf, NULL, (void *)buf)) {
		return -EBUSY;
	}

	if (nrfx_is_in_ram(buf)) {
		xfer_buf = buf;
		xfer_len = len;
	} else {
		data->async->tx.len = len;
		xfer_buf = data->async->tx.cache_buf;
		(void)setup_tx_cache(data, &xfer_len);
	}

	err = nrfx_uarte_tx(&get_dev_config(dev)->instance, xfer_buf, xfer_len, 0);
	LOG_INST_DBG(GET_LOG(dev), "uart tx:%d", err);
	if (err != NRFX_SUCCESS)
	{
		__ASSERT(0, "err:%08x %s %d", err, buf, len);
		LOG_ERR("unexp:%08x", err);
		return -ENOTSUP;
	}

	if (data->config.flow_ctrl == UART_CFG_FLOW_CTRL_RTS_CTS
	    && timeout != SYS_FOREVER_US) {
		k_timer_start(&data->async->tx.timer, K_USEC(timeout), K_NO_WAIT);
	}

	return 0;
}

static int uarte_nrfx_tx_abort(const struct device *dev)
{
	struct uarte_nrfx_data *data = get_dev_data(dev);
	nrfx_err_t err;

	k_timer_stop(&data->async->tx.timer);
	err = nrfx_uarte_tx_abort(&get_dev_config(dev)->instance, false);

	return (err != NRFX_SUCCESS) ? -EFAULT : 0;
}

static int uarte_nrfx_callback_set(const struct device *dev,
				   uart_callback_t callback,
				   void *user_data)
{
	struct uarte_nrfx_data *data = get_dev_data(dev);

	data->async->user_callback = callback;
	data->async->user_data = user_data;

	return 0;
}

#ifdef CONFIG_UART_ASYNC_API
static int uarte_nrfx_api_callback_set(const struct device *dev,
				       uart_callback_t callback,
				       void *user_data)
{
	if (!is_async_api(dev)) {
		return -ENOTSUP;
	}

	return uarte_nrfx_callback_set(dev, callback, user_data);
}
#endif

#define HW_RX_COUNTING_ENABLED(config) \
	(IS_ENABLED(CONFIG_UARTE_NRF_HW_ASYNC) ? \
		(config->flags & UARTE_CFG_FLAG_HW_RX_COUNT) : false)

static void timer_handler(nrf_timer_event_t event_type, void *p_context) { }

static int hw_rx_counter_init(const struct device *dev, struct uarte_nrfx_data *data)
{
	const nrfx_timer_t *timer = &get_dev_config(dev)->timer;
	nrfx_timer_config_t tmr_config = NRFX_TIMER_DEFAULT_CONFIG;
	const nrfx_uarte_t *instance = &get_dev_config(dev)->instance;
	uint32_t evt = nrfx_uarte_event_address_get(instance, NRF_UARTE_EVENT_RXDRDY);
	uint32_t tsk = nrfx_timer_task_address_get(timer, NRF_TIMER_TASK_COUNT);
	int err;
	nrfx_err_t ret;

	err = ppi_setup(evt, tsk);
	if (err < 0) {
		return err;
	}

	tmr_config.mode = NRF_TIMER_MODE_COUNTER;
	tmr_config.bit_width = NRF_TIMER_BIT_WIDTH_32;
	ret = nrfx_timer_init(timer, &tmr_config, timer_handler);

	return (ret == NRFX_SUCCESS) ? 0 : -EIO;
}

static void hw_rx_counter_enable(const struct device *dev, struct uarte_nrfx_data *data)
{
	const nrfx_timer_t *timer = &get_dev_config(dev)->timer;

	nrfx_timer_enable(timer);

	for (int i = 0; i < data->async->rx.flush_cnt; i++) {
		nrfx_timer_increment(timer);
	}
}

static void hw_rx_counter_disable(const struct device *dev, struct uarte_nrfx_data *data)
{
	const nrfx_timer_t *timer = &get_dev_config(dev)->timer;

	nrfx_timer_disable(timer);
}

/* Function can be called from two contexts: UART interrupt and timeout handler.
 * Driver has no control over priority of those contexts thus it assumes that one
 * may preempt another. Function is not reentrant. Before enterring from timeout
 * context uart interrupt is disabled to ensure that it will not preempt. When
 * calling from uarte context a flag is set to indicate that interrupt context is
 * active, if timeout handler preempts it checks this flag and if set reporting
 * is skipped.
 */
static void report_rx_rdy(const struct device *dev,
			  struct uarte_nrfx_data *data)
{
	uint32_t last_report_cnt = data->async->rx.last_report_cnt;
	uint32_t curr_cnt = data->async->rx.curr_cnt;
	int32_t bytes = curr_cnt - last_report_cnt;
	struct uart_event event;

	while (bytes > 0 && data->async->rx.buf) {
		uint32_t buf_remainder = data->async->rx.len - data->async->rx.offset;

		if (buf_remainder == 0) {
			LOG_INST_ERR(GET_LOG(dev),
				     "0 rem, len:%d, offset:%d, last_report:%d, curr: %d",
				     data->async->rx.len, data->async->rx.offset,
				     last_report_cnt, curr_cnt);
			__ASSERT_NO_MSG(0);
		}

		uint32_t len = MIN(bytes, buf_remainder);
		bool buf_rel;

		event.type = UART_RX_RDY;
		event.data.rx.buf = data->async->rx.buf;
		event.data.rx.offset = data->async->rx.offset;
		event.data.rx.len = len;
		data->async->rx.last_report_cnt += len;
		data->async->rx.offset += len;

		/* Buffer boundary is reached. */
		if (data->async->rx.offset == data->async->rx.len) {
			buf_rel = true;
			/* Switch to new buffer. */
			data->async->rx.buf = data->async->rx.next_buf;
			data->async->rx.len = data->async->rx.next_len;
			data->async->rx.next_buf = NULL;
			data->async->rx.offset = 0;
		} else {
			buf_rel = false;
		}

		data->async->user_callback(dev, &event, data->async->user_data);

		if (buf_rel) {
			/* Release the buffer. */
			event.type = UART_RX_BUF_RELEASED;
			event.data.rx_buf.buf = event.data.rx.buf;
			data->async->user_callback(dev, &event, data->async->user_data);
		}

		bytes -= len;
	}
}

static void restart_rx_timeout(struct uarte_nrfx_data *data)
{
	k_timer_start(&data->async->rx.timer,
		      data->async->rx.timeout,
		      K_NO_WAIT);
}

static void rx_timeout_bbb(struct k_timer *timer)
{
	const struct device *dev = (const struct device *)k_timer_user_data_get(timer);
	struct uarte_nrfx_data *data = get_dev_data(dev);
	const nrfx_uarte_t *instance = &get_dev_config(dev)->instance;

	/* Timeout may arrive after RX is disabled. */
	if (!(data->flags & UARTE_DATA_FLAG_RX_ACTIVE)) {
		return;
	}

	if (data->flags & UARTE_DATA_FLAG_IN_RX_DONE_IRQ) {
		return;
	}

	nrfx_uarte_rx_int_disable(instance);
	LOG_INST_DBG(GET_LOG(dev), "Report from timeout");
	report_rx_rdy(dev, data);
	nrfx_uarte_rx_int_enable(instance);
}

static void rx_timeout(struct k_timer *timer)
{
	const struct device *dev = (const struct device *)k_timer_user_data_get(timer);
	struct uarte_nrfx_data *data = get_dev_data(dev);
	const struct uarte_nrfx_config *cfg = get_dev_config(dev);
	const nrfx_uarte_t *instance = &cfg->instance;
	uint32_t new_bytes, last_report_cnt, curr_cnt, last_cnt;

	/* Timeout may arrive after RX is disabled. */
	if (!(data->flags & UARTE_DATA_FLAG_RX_ACTIVE)) {
		return;
	}

	if (data->flags & UARTE_DATA_FLAG_IN_RX_DONE_IRQ) {
		return;
	}

	nrfx_uarte_rx_int_disable(instance);

	nrf_gpio_cfg_output(31);
	nrf_gpio_pin_set(31);
	data->async->rx.curr_cnt = nrfx_timer_capture(&cfg->timer, 0);
	last_report_cnt = data->async->rx.last_report_cnt;
	curr_cnt = data->async->rx.curr_cnt;

	new_bytes = curr_cnt - last_report_cnt;
	last_cnt = data->async->rx.last_cnt;
	data->async->rx.last_cnt = curr_cnt;

	/* If there are new bytes coming reset timeout and return. */
	if (curr_cnt != last_cnt || !new_bytes) {
		data->async->rx.t_countdown = RX_TIMEOUT_DIV;
	} else {
		/* If no new bytes, continue countdown. */
		data->async->rx.t_countdown--;
		if (!data->async->rx.t_countdown) {
			/* If we got here it means that for number of consecutive timeouts (which
			 * sums to the user rx timeout) there were no new bytes. Attempt to report
			 * data, if we interrupted another place of report then skip.
			 */

			report_rx_rdy(dev, data);
			data->async->rx.t_countdown = RX_TIMEOUT_DIV;
		}
	}

	/*restart_rx_timeout(data);*/
	nrf_gpio_pin_clear(31);

	nrfx_uarte_rx_int_enable(instance);
}

static int schedule_bbb(const struct device *dev, struct uarte_nrfx_data *data, bool timeout)
{
	const nrfx_uarte_t *instance = &get_dev_config(dev)->instance;
	nrfx_err_t err;

	if (data->async->rx.bbb) {
		err = nrfx_uarte_rx_buffer_set(instance, data->async->rx.bbb, 1);
		if (err == NRFX_SUCCESS) {
			data->async->rx.bbb++;
			if (timeout) {
				restart_rx_timeout(data);
			}
		} else {
			LOG_INST_ERR(GET_LOG(dev), "rx buffer set failed (err:%08x)", err);
		}
	} else {
    nrf_gpio_cfg_output(31);
    nrf_gpio_pin_set(31);
		err = nrfx_uarte_rx_abort(instance, false);
    nrf_gpio_pin_clear(31);
	}

	return (err == NRFX_SUCCESS) ? 0 : -EIO;
}

static void rx_done_handler_bbb(const struct device *dev, struct uarte_nrfx_data *data)
{
	data->async->rx.curr_cnt++;
	data->async->rx.buf_cnt_down--;

	if (!data->async->rx.buf_cnt_down) {
		/* Exceeded buffer boundary. */
		report_rx_rdy(dev, data);
		data->async->rx.bbb = data->async->rx.buf;
		data->async->rx.buf_cnt_down = data->async->rx.len;
	}

	if (data->flags & UARTE_DATA_FLAG_RX_ACTIVE) {
		int err = schedule_bbb(dev, data, true);

		(void)err;
		__ASSERT_NO_MSG(err == 0);
	}
}

static void rx_done_handler(const struct device *dev, struct uarte_nrfx_data *data)
{
	atomic_or(&data->flags, UARTE_DATA_FLAG_IN_RX_DONE_IRQ);

	if (HW_RX_COUNTING_ENABLED(get_dev_config(dev))) {
		data->async->rx.curr_cnt =
			nrfx_timer_capture(&get_dev_config(dev)->timer, 0);
		report_rx_rdy(dev, data);
		data->async->rx.t_countdown = RX_TIMEOUT_DIV;
		/*restart_rx_timeout(data);*/
	} else {
		rx_done_handler_bbb(dev, data);
	}

	atomic_and(&data->flags, ~UARTE_DATA_FLAG_IN_RX_DONE_IRQ);
}

static void rx_buf_req_handler(const struct device *dev, struct uarte_nrfx_data *data)
{
	struct uart_event event = {
		.type = UART_RX_BUF_REQUEST
	};
	bool call_handler;

	if (HW_RX_COUNTING_ENABLED(get_dev_config(dev))) {
		call_handler = true;
	} else {
		call_handler = data->async->rx.bbb == &data->async->rx.buf[1];
	}

	if (call_handler) {
		data->async->user_callback(dev, &event, data->async->user_data);
	}
}

static void stop_rx_framework(const struct device *dev,
				struct uarte_nrfx_data *data,
				size_t flush_cnt)
{
	atomic_and(&data->flags, ~UARTE_DATA_FLAG_RX_ACTIVE);

	if (data->async) {
		k_timer_stop(&data->async->rx.timer);
		if (HW_RX_COUNTING_ENABLED(get_dev_config(dev))) {
			hw_rx_counter_disable(dev, data);
		}
		data->async->rx.flush_cnt = flush_cnt;
	}
}

static void rx_disabled_handler(const struct device *dev,
				struct uarte_nrfx_data *data,
				size_t flush_cnt)
{
	nrf_gpio_cfg_output(29);
	nrf_gpio_pin_set(29);
	struct uart_event event;

	stop_rx_framework(dev, data, flush_cnt);
	report_rx_rdy(dev, data);

	if (data->async->rx.buf) {
		event.type = UART_RX_BUF_RELEASED;
		event.data.rx_buf.buf = data->async->rx.buf;
		data->async->user_callback(dev, &event, data->async->user_data);
	}

	if (data->async->rx.next_buf) {
		event.data.rx_buf.buf = data->async->rx.next_buf;
	}

	event.type = UART_RX_DISABLED;
	data->async->user_callback(dev, &event, data->async->user_data);

	nrf_gpio_pin_clear(29);
}

static void rx_error_handler(const struct device *dev, struct uarte_nrfx_data *data)
{
	/* TODO error handling is not settled yet. */
	struct uart_event event = {
		.type = UART_RX_STOPPED
	};

	data->async->user_callback(dev, &event, data->async->user_data);
}

static void user_callback_tx(const struct device *dev, enum uart_event_type type,
			     const uint8_t *buf, size_t len)
{
	struct uarte_nrfx_data *data = get_dev_data(dev);
	struct uart_event event = {
		.type = type,
		.data = {
		       .tx = {
				.buf = buf,
				.len = len
		       }
		}
	};

	data->async->user_callback(dev, &event, data->async->user_data);
}

static inline const char *evt2str(nrfx_uarte_evt_type_t type)
{
	static const char *strs[] = {
		"TX_DONE",
		"RX_DONE",
		"ERROR",
		"TX_ABORTED",
		"RX_BUF_REQ",
		"RX_DISABLED",
		"RX_BUF_TOO_LATE"
	};

	BUILD_ASSERT(NRFX_UARTE_EVT_TX_DONE == 0, "");
	BUILD_ASSERT(NRFX_UARTE_EVT_RX_DONE == 1, "");
	BUILD_ASSERT(NRFX_UARTE_EVT_ERROR == 2, "");
	BUILD_ASSERT(NRFX_UARTE_EVT_TX_ABORTED == 3, "");
	BUILD_ASSERT(NRFX_UARTE_EVT_RX_BUF_REQUEST == 4, "");
	BUILD_ASSERT(NRFX_UARTE_EVT_RX_DISABLED == 5, "");
	BUILD_ASSERT(NRFX_UARTE_EVT_RX_BUF_TOO_LATE == 6, "");
	return strs[type];
}

static void tx_done_handler(const struct device *dev, nrfx_uarte_event_t const *event)
{
	struct uarte_nrfx_data *data = get_dev_data(dev);
	const uint8_t *buf;
	size_t len;

	/* Handle case when read only buffer was scheduled and it is splited into
	 * multiple chunks of cached data.
	 */
	if (event->data.rxtx.p_data == data->async->tx.cache_buf) {
		data->async->tx.cache_offset += event->data.rxtx.bytes;
		if (setup_tx_cache(data, &len)) {
			nrfx_err_t err;

			err = nrfx_uarte_tx(&get_dev_config(dev)->instance,
					    data->async->tx.cache_buf, len, 0);

			(void)err;
			__ASSERT_NO_MSG(err == NRFX_SUCCESS);
			return;
		}
	}

	buf = data->async->tx.buf;
	len = data->async->tx.len;
	data->async->tx.buf = NULL;
	user_callback_tx(dev, UART_TX_DONE, buf, len);
}

static void tx_aborted_handler(const struct device *dev, nrfx_uarte_event_t const *event)
{
	struct uarte_nrfx_data *data = get_dev_data(dev);
	size_t len = data->async->tx.cache_offset + event->data.rxtx.bytes;
	const uint8_t *buf = data->async->tx.buf;

	data->async->tx.cache_offset = 0;
	data->async->tx.buf = NULL;
	user_callback_tx(dev, UART_TX_ABORTED, buf, len);
}

static void event_handler(nrfx_uarte_event_t const *event, void *context)
{
	const struct device *dev = (const struct device *)context;
	struct uarte_nrfx_data *data = get_dev_data(dev);

	nrf_gpio_cfg_output(27);
	nrf_gpio_pin_set(27);
	/*LOG_INST_DBG(GET_LOG(dev), "Event %s", evt2str(event->type));*/
	LOG_INST_DBG(GET_LOG(dev), "Event %d", event->type);

	switch (event->type) {
	case NRFX_UARTE_EVT_TX_DONE:
		tx_done_handler(dev, event);
		break;
	case NRFX_UARTE_EVT_TX_ABORTED:
		tx_aborted_handler(dev, event);
		break;
	case NRFX_UARTE_EVT_RX_BUF_REQUEST:
		rx_buf_req_handler(dev, data);
		break;
	case NRFX_UARTE_EVT_RX_DONE:
		rx_done_handler(dev, data);
		break;
	case NRFX_UARTE_EVT_RX_DISABLED:
		rx_disabled_handler(dev, data, event->data.rx_disabled.flush_cnt);
		break;
	case NRFX_UARTE_EVT_ERROR:
		rx_error_handler(dev, data);
		break;
	case NRFX_UARTE_EVT_RX_BUF_TOO_LATE:
		/* todo */
		break;
	default:
		__ASSERT_NO_MSG(0);
		break;
	}
	nrf_gpio_pin_clear(27);
}

static inline k_timeout_t get_timeout(int32_t timeout_us)
{

	/* Set minimum interval to 3 RTC ticks. 3 is used due to RTC limitation
	 * which cannot set timeout for next tick. Assuming delay in processing
	 * 3 instead of 2 is used. Note that lower value would work in a similar
	 * way but timeouts would always occur later than expected,  most likely
	 * after ~3 ticks.
	 */
	static const uint32_t min_timeout_us =
			ceiling_fraction(3 * 1000000, CONFIG_SYS_CLOCK_TICKS_PER_SEC);

	if (timeout_us == 0 || timeout_us == SYS_FOREVER_US) {
		return K_FOREVER;
	}

	return K_USEC(MAX(timeout_us, min_timeout_us));
}

static int uarte_nrfx_rx_enable(const struct device *dev, uint8_t *buf,
				size_t len,
				int32_t timeout)
{
	struct uarte_nrfx_data *data = get_dev_data(dev);
	const nrfx_uarte_t *instance = &get_dev_config(dev)->instance;
	nrfx_err_t err;
	bool stop_on_end;

	if (get_dev_config(dev)->flags & UARTE_CFG_FLAG_NO_RX) {
		__ASSERT(false, "TX only UARTE instance");
		return -ENOTSUP;
	}

	data->async->rx.buf = buf;
	data->async->rx.len = len;
	data->async->rx.next_buf = NULL;
	data->async->rx.next_len = 0;
	data->async->rx.offset = 0;
	data->async->rx.last_report_cnt = 0;
	data->async->rx.curr_cnt = 0;

	stop_on_end = !is_int_driven_api(dev);

	atomic_or(&data->flags, UARTE_DATA_FLAG_RX_ACTIVE);

	if (!HW_RX_COUNTING_ENABLED(get_dev_config(dev))) {
		int rv;

		data->async->rx.timeout = get_timeout(timeout);
		data->async->rx.bbb = buf;
		data->async->rx.buf_cnt_down = data->async->rx.len;

		rv = schedule_bbb(dev, data, false);
		if (rv < 0) {
			return rv;
		}

		err = nrfx_uarte_rx_enable(instance, 0);
		return (err != NRFX_SUCCESS) ? -EBUSY : 0;
	}

	hw_rx_counter_enable(dev, data);
	data->async->rx.t_countdown = RX_TIMEOUT_DIV;
	data->async->rx.timeout = get_timeout(ceiling_fraction(timeout, RX_TIMEOUT_DIV));
	k_timer_start(&data->async->rx.timer,
		      data->async->rx.timeout,
		      data->async->rx.timeout);

	err = nrfx_uarte_rx_buffer_set(instance, buf, len);
	if (err != NRFX_SUCCESS) {
		return -EIO;
	}

	uint32_t flags = NRFX_UARTE_RX_CONT |
			 (is_int_driven_api(dev) ? 0 : NRFX_UARTE_RX_STOP_ON_END);

	err = nrfx_uarte_rx_enable(instance, flags);
	if (err != NRFX_SUCCESS) {
		return -EBUSY;
	}

	return 0;
}

static int uarte_nrfx_rx_buf_rsp(const struct device *dev, uint8_t *buf,
				 size_t len)
{
	struct uarte_nrfx_data *data = get_dev_data(dev);
	nrfx_err_t err;
	int rv;
	int key;

	if (HW_RX_COUNTING_ENABLED(get_dev_config(dev))) {
		err = nrfx_uarte_rx_buffer_set(&get_dev_config(dev)->instance, buf, len);
	} else {
		err = NRFX_SUCCESS;
	}

	key = irq_lock();
	if (err == NRFX_SUCCESS) {
		if (data->async->rx.buf && !data->async->rx.next_buf) {
			data->async->rx.next_buf = buf;
			data->async->rx.next_len = len;
			rv = 0;
		} else if (data->async->rx.next_buf) {
			rv = -EBUSY;
		} else {
			rv = -EACCES;
		}
	} else if (err == NRFX_ERROR_INVALID_STATE) {
		rv = -EBUSY;
	} else {
		rv = -EACCES;
	}

	irq_unlock(key);

	return rv;
}

static int uarte_nrfx_poll_in(const struct device *dev, unsigned char *c)
{
	struct uarte_nrfx_data *data = get_dev_data(dev);
	const nrfx_uarte_t *instance = &get_dev_config(dev)->instance;
	nrfx_err_t err;

	if (get_dev_data(dev)->flags & UARTE_DATA_FLAG_OFF) {
		return -1;
	}

	if (is_int_driven_api(dev)) {
		return uart_fifo_read(dev, c, 1) == 0 ? -1 : 0;
	}
	if (is_async_api(dev)) {
		return -EBUSY;
	}

	err = nrfx_uarte_rx_ready(instance, NULL);
	if (err == NRFX_SUCCESS) {
		*c = data->rx_byte;
		err = nrfx_uarte_rx_buffer_set(instance, &data->rx_byte, 1);
		__ASSERT_NO_MSG(err == NRFX_SUCCESS);

		return 0;
	}

	return -1;
}

static int uarte_nrfx_rx_disable(const struct device *dev)
{
	atomic_and(&get_dev_data(dev)->flags, ~UARTE_DATA_FLAG_RX_ACTIVE);
	nrfx_err_t err = nrfx_uarte_rx_abort(&get_dev_config(dev)->instance, false);

	return (err == NRFX_SUCCESS) ? 0 : -EFAULT;
}

static int tx_end_stop_init(const nrfx_uarte_t *instance)
{
	return ppi_setup(nrfx_uarte_event_address_get(instance, NRF_UARTE_EVENT_ENDTX),
			 nrfx_uarte_task_address_get(instance, NRF_UARTE_TASK_STOPTX));
}

static void gpio_init(const nrfx_uarte_t *instance, const struct uarte_nrfx_psel_config *config)
{
	nrf_gpio_pin_write(config->tx_pin, 1);
	nrf_gpio_cfg_output(config->tx_pin);

	if (config->rx_pin != NRF_UARTE_PSEL_DISCONNECTED) {
		nrf_gpio_cfg_input(config->rx_pin,
				   config->rx_pull_up ?
				   NRF_GPIO_PIN_PULLUP : NRF_GPIO_PIN_NOPULL);
	}

	if (config->cts_pin != NRF_UARTE_PSEL_DISCONNECTED) {
		nrf_gpio_cfg_input(config->cts_pin,
				   config->cts_pull_up ?
				   NRF_GPIO_PIN_PULLUP : NRF_GPIO_PIN_NOPULL);
	}

	if (config->rts_pin != NRF_UARTE_PSEL_DISCONNECTED) {
		nrf_gpio_pin_write(config->rts_pin, 1);
		nrf_gpio_cfg_output(config->rts_pin);
	}

	nrf_uarte_txrx_pins_set(instance->p_reg, config->tx_pin, config->rx_pin);
	nrf_uarte_hwfc_pins_set(instance->p_reg, config->rts_pin, config->cts_pin);
}

static void gpio_uninit(const struct uarte_nrfx_psel_config *config)
{
	nrf_gpio_cfg_default(config->tx_pin);

	if (config->rx_pin != NRF_UARTE_PSEL_DISCONNECTED) {
		nrf_gpio_cfg_default(config->rx_pin);
	}

	if (config->rts_pin != NRF_UARTE_PSEL_DISCONNECTED) {
		nrf_gpio_cfg_default(config->rts_pin);
	}

	if (config->cts_pin != NRF_UARTE_PSEL_DISCONNECTED) {
		nrf_gpio_cfg_default(config->cts_pin);
	}
}

static int pins_config(const struct uarte_nrfx_config *config, bool init)
{
	if (IS_ENABLED(CONFIG_PINCTRL)) {
		int err;

		err = pinctrl_apply_state(config->pin_config.pinctrl,
					  init ?
					  PINCTRL_STATE_DEFAULT : PINCTRL_STATE_SLEEP);
		if (err < 0) {
			return err;
		}
	}

	if (init) {
		gpio_init(&config->instance, config->pin_config.psel);
	} else {
		gpio_uninit(config->pin_config.psel);
	}

	return 0;
}

static int start_rx(const struct device *dev, bool int_driven_api)
{
	struct uarte_nrfx_data *data = get_dev_data(dev);

	if (is_sync_api(dev)) {
		nrfx_err_t err;
		const nrfx_uarte_t *instance = &get_dev_config(dev)->instance;

		err = nrfx_uarte_rx_buffer_set(instance, &data->rx_byte, 1);
		__ASSERT_NO_MSG(err == NRFX_SUCCESS);

		err = nrfx_uarte_rx_enable(instance, 0);
		__ASSERT_NO_MSG(err == NRFX_SUCCESS || err == NRFX_ERROR_BUSY);

		(void)err;

		return 0;
	} else if (IS_ENABLED(CONFIG_UART_INTERRUPT_DRIVEN) && int_driven_api) {
		return uart_async_to_irq_rx_enable(dev);
	}

	return 0;
}


static void async_to_irq_egu_evt_handler(uint8_t event_idx, void *context)
{
	struct uart_async_to_irq_data *data = context;

	uart_async_to_irq_trampoline_cb(data);
}

static void async_to_irq_trampoline_timeout(struct k_timer *timer)
{
	struct uart_async_to_irq_data *data = k_timer_user_data_get(timer);

	uart_async_to_irq_trampoline_cb(data);
}

static void async_to_irq_trampoline(struct uart_async_to_irq_data *data)
{
	struct uart_nrfx_a2i *ndata = CONTAINER_OF(data, struct uart_nrfx_a2i, data);

	if (ndata->timer) {
		uint32_t key = irq_lock();
		k_timer_start(ndata->timer, K_USEC(10), K_NO_WAIT);
		irq_unlock(key);
		return;
	}

	if (USE_EGU_TRAMPOLINE) {
		nrfx_egu_trigger(ndata->egu, 0);
	}
}

static int async_to_irq_trampoline_init(struct uart_nrfx_a2i *data)
{
	if (data->timer) {
		k_timer_user_data_set(data->timer, data);
	} else if (USE_EGU_TRAMPOLINE && data->egu) {
		nrfx_err_t err;

		err = nrfx_egu_init(data->egu, 1, async_to_irq_egu_evt_handler, data);
		if (err != NRFX_SUCCESS) {
			return -EIO;
		}

		nrfx_egu_int_enable(data->egu, BIT(0));
	} else {
		return -EINVAL;
	}

	return 0;
}

static int uarte_nrfx_init(const struct device *dev)
{
	struct uarte_nrfx_data *data = get_dev_data(dev);
	const struct uarte_nrfx_config *config = get_dev_config(dev);
	nrfx_err_t nrfx_err;
	int err = 0;


	if (config->nrfx_config.tx_stop_on_end) {
		tx_end_stop_init(&config->instance);
	}

	err = pins_config(config, true);
	if (err < 0) {
		return err;
	}

	nrfx_err = nrfx_uarte_init(&config->instance, &config->nrfx_config,
				   data->async ? event_handler : NULL);
	if (nrfx_err != NRFX_SUCCESS) {
		err = -EIO;
		goto bail;
	}

	if (data->a2i_data) {
		err = async_to_irq_trampoline_init(data->a2i_data);
		if (err != 0) {
			goto bail;
		}
	}

	if (!(config->flags & UARTE_CFG_FLAG_NO_RX)) {
		if (data->async) {
			if (HW_RX_COUNTING_ENABLED(config)) {
				err = hw_rx_counter_init(dev, data);
				if (err < 0) {
					err = -ENOMEM;
					goto bail;
				}
			}

			k_timer_init(&data->async->rx.timer,
				     HW_RX_COUNTING_ENABLED(config) ?
				     rx_timeout : rx_timeout_bbb,
				     NULL);
			k_timer_user_data_set(&data->async->rx.timer, (void *)dev);
			k_timer_init(&data->async->tx.timer, tx_timeout, NULL);
			k_timer_user_data_set(&data->async->tx.timer, (void *)dev);
		}
		err = start_rx(dev, data->a2i_data != NULL);
	}
bail:
	if (err < 0) {
		LOG_INST_ERR(GET_LOG(dev), "Init failed (err: %d)", err);
	} else {
		LOG_INST_DBG(GET_LOG(dev), "Init done");
	}

	return err;
}

static const struct uart_async_to_irq_async_api a2i_api = {
	.callback_set		= uarte_nrfx_callback_set,
	.tx			= uarte_nrfx_tx,
	.tx_abort		= uarte_nrfx_tx_abort,
	.rx_enable		= uarte_nrfx_rx_enable,
	.rx_buf_rsp		= uarte_nrfx_rx_buf_rsp,
	.rx_disable		= uarte_nrfx_rx_disable,
};

static const struct uart_driver_api uart_nrfx_uarte_driver_api = {
	.poll_in		= uarte_nrfx_poll_in,
	.poll_out		= uarte_nrfx_poll_out,
	.err_check		= uarte_nrfx_err_check,

	.configure	= IS_ENABLED(CONFIG_UART_USE_RUNTIME_CONFIGURE) ?
						uarte_nrfx_configure : NULL,
	.config_get	= IS_ENABLED(CONFIG_UART_USE_RUNTIME_CONFIGURE) ?
						uarte_nrfx_config_get : NULL,

#ifdef CONFIG_UART_ASYNC_API
	.callback_set		= IS_ENABLED(CONFIG_UART_ASYNC_API) ?
					uarte_nrfx_api_callback_set : NULL,
	.tx			= IS_ENABLED(CONFIG_UART_ASYNC_API) ?
					uarte_nrfx_tx : NULL,
	.tx_abort		= IS_ENABLED(CONFIG_UART_ASYNC_API) ?
					uarte_nrfx_tx_abort : NULL,
	.rx_enable		= IS_ENABLED(CONFIG_UART_ASYNC_API) ?
					uarte_nrfx_rx_enable : NULL,
	.rx_buf_rsp		= IS_ENABLED(CONFIG_UART_ASYNC_API) ?
					uarte_nrfx_rx_buf_rsp : NULL,
	.rx_disable		= IS_ENABLED(CONFIG_UART_ASYNC_API) ?
					uarte_nrfx_rx_disable : NULL,
#endif /* CONFIG_UART_ASYNC_API */
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	UART_ASYNC_TO_IRQ_API_INIT(),
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
};

static int uarte_nrfx_pm_action(const struct device *dev,
				enum pm_device_action action)
{
	const struct uarte_nrfx_config *config = get_dev_config(dev);
	const nrfx_uarte_t *instance = &config->instance;
	int err = 0;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		if (config->flags & UARTE_CFG_FLAG_GPIO_MGMT) {
			err = pins_config(config, true);
			if (err < 0) {
				return err;
			}
		}
		if (!is_async_api(dev)) {
			/* TODO start continues RX */
		}

		nrfx_uarte_tx_unlock(instance);

		if (!(config->flags & UARTE_CFG_FLAG_NO_RX)) {
			err = start_rx(dev, is_int_driven_api(dev));
		}
		atomic_and(&get_dev_data(dev)->flags, ~UARTE_DATA_FLAG_OFF);
		LOG_INST_DBG(GET_LOG(dev), "Resumed");
		break;
	case PM_DEVICE_ACTION_SUSPEND:
	{
		/* Disabling UART requires stopping RX, but stop RX event is
		 * only sent after each RX if async UART API is used.
		 */
		/* Entering inactive state requires device to be no
		 * active asynchronous calls.
		 */
		nrfx_err_t err;

		atomic_or(&get_dev_data(dev)->flags, UARTE_DATA_FLAG_OFF);
		if (!(config->flags & UARTE_CFG_FLAG_NO_RX)) {
			stop_rx_framework(dev, get_dev_data(dev), 0);
			err = nrfx_uarte_rx_abort(instance, true);
			__ASSERT_NO_MSG((err == NRFX_SUCCESS) ||
					(err == NRFX_ERROR_INVALID_STATE));
			/* reset rx data. */
		}

		err = nrfx_uarte_tx_lock(instance);
		__ASSERT_NO_MSG(err == NRFX_SUCCESS);

		err = nrfx_uarte_tx_abort(instance, true);
		__ASSERT_NO_MSG((err == NRFX_SUCCESS) ||
				(err == NRFX_ERROR_INVALID_STATE));

		if (config->flags & UARTE_CFG_FLAG_GPIO_MGMT) {
			err = pins_config(config, false);
			if (err < 0) {
				return err;
			}
		}

		LOG_INST_DBG(GET_LOG(dev), "Suspended");
		break;
	}
	default:
		return -ENOTSUP;
	}

	return err;
}

#define UARTE(idx)			DT_NODELABEL(uart##idx)
#define UARTE_HAS_PROP(idx, prop)	DT_NODE_HAS_PROP(UARTE(idx), prop)
#define UARTE_PROP(idx, prop)		DT_PROP(UARTE(idx), prop)

#define HWFC_AVAILABLE(idx)					       \
	(UARTE_HAS_PROP(idx, rts_pin) || UARTE_HAS_PROP(idx, cts_pin))

#define HWFC_CONFIG_CHECK(idx) \
	BUILD_ASSERT( \
		(UARTE_PROP(idx, hw_flow_control) && HWFC_AVAILABLE(idx)) \
		|| \
		!UARTE_PROP(idx, hw_flow_control) \
	)

#define GET_INIT_LOG_LEVEL(idx) \
	COND_CODE_1(DT_HAS_CHOSEN(zephyr_console), \
		(DT_SAME_NODE(UARTE(idx), \
			      DT_CHOSEN(zephyr_console)) ? \
			 LOG_LEVEL_NONE : CONFIG_UART_LOG_LEVEL), \
			(CONFIG_UART_LOG_LEVEL))

#define EGU_INSTANCE(uart_idx) \
	UTIL_CAT(CONFIG_UART_, UTIL_CAT(uart_idx, _INT_DRIVEN_EGU))
#define EGU_IRQ_HANDLER(uart_idx) \
	UTIL_CAT(nrfx_egu_, UTIL_CAT(EGU_INSTANCE(uart_idx), _irq_handler))
#define EGU_IRQN(uart_idx) \
	NRFX_IRQ_NUMBER_GET(UTIL_CAT(NRF_EGU, EGU_INSTANCE(uart_idx)))

#define UARTE_PSEL_CONFIG_NAME(idx) nrfx_psel_config_##idx

#define UARTE_PSEL_CONFIG(idx) \
	static const struct uarte_nrfx_psel_config  UARTE_PSEL_CONFIG_NAME(idx) = { \
		.tx_pin  = DT_PROP_OR(UARTE(idx), tx_pin, NRF_UARTE_PSEL_DISCONNECTED), \
		.rx_pin  = DT_PROP_OR(UARTE(idx), rx_pin, NRF_UARTE_PSEL_DISCONNECTED), \
		.rts_pin = DT_PROP_OR(UARTE(idx), rts_pin, NRF_UARTE_PSEL_DISCONNECTED), \
		.cts_pin = DT_PROP_OR(UARTE(idx), cts_pin, NRF_UARTE_PSEL_DISCONNECTED), \
		.rx_pull_up  = DT_PROP(UARTE(idx), rx_pull_up), \
		.cts_pull_up = DT_PROP(UARTE(idx), cts_pull_up) \
	}

#define UART_NRF_UARTE_DEVICE(idx) \
	HWFC_CONFIG_CHECK(idx); \
	LOG_INSTANCE_REGISTER(LOG_MODULE_NAME, idx, GET_INIT_LOG_LEVEL(idx)); \
	static const struct uart_config uart##idx##_config = { \
		.baudrate = UARTE_PROP(idx, current_speed), \
		.parity = IS_ENABLED(CONFIG_UART_##idx##_NRF_PARITY_BIT) ? \
			  UART_CFG_PARITY_EVEN : UART_CFG_PARITY_NONE, \
		.stop_bits = UART_CFG_STOP_BITS_1, \
		.data_bits = UART_CFG_DATA_BITS_8, \
		.flow_ctrl = UARTE_PROP(idx, hw_flow_control) ?  \
			     UART_CFG_FLOW_CTRL_RTS_CTS : UART_CFG_FLOW_CTRL_NONE, \
	}; \
	COND_CODE_1(CONFIG_PINCTRL, \
			(PINCTRL_DT_DEFINE(UARTE(idx))), \
			(UARTE_PSEL_CONFIG(idx);)) \
	static const struct uarte_nrfx_config uarte_##idx##_config = { \
		.instance = NRFX_UARTE_INSTANCE(idx), \
		.nrfx_config = { \
			.p_context = (void *)DEVICE_DT_GET(UARTE(idx)), \
			.baudrate = NRF_BAUDRATE(uart##idx##_config.baudrate), \
			.interrupt_priority = DT_IRQ(UARTE(idx), priority), \
			.hal_cfg = NRF_UARTE_CONFIG(uart##idx##_config, \
						UARTE_ODD_PARITY_ALLOWED), \
			.tx_stop_on_end = IS_ENABLED(CONFIG_UART_##idx##_ENHANCED_POLL_OUT), \
			.skip_psel_cfg = true, \
			.skip_gpio_cfg = true, \
		}, \
		.pin_config = { \
			COND_CODE_1(CONFIG_PINCTRL, \
				(.pinctrl = PINCTRL_DT_DEV_CONFIG_GET(UARTE(idx))), \
				(.psel = &UARTE_PSEL_CONFIG_NAME(idx))) \
		}, \
		.config = uart##idx##_config, \
		.flags = (UARTE_HAS_PROP(idx, rx_pin) ?	\
				0 : UARTE_CFG_FLAG_NO_RX) | \
			(IS_ENABLED(CONFIG_UART_##idx##_GPIO_MANAGEMENT) ? \
				UARTE_CFG_FLAG_GPIO_MGMT : 0) | \
			(IS_ENABLED(CONFIG_UART_##idx##_NRF_HW_ASYNC) ? \
			        UARTE_CFG_FLAG_HW_RX_COUNT : 0) | \
			(IS_ENABLED(CONFIG_UART_##idx##_INTERRUPT_DRIVEN) ? \
				UARTE_CFG_FLAG_INTERRUPT_DRIVEN_API : 0), \
		IF_ENABLED(CONFIG_UART_##idx##_NRF_HW_ASYNC, \
			(.timer = NRFX_TIMER_INSTANCE( \
				CONFIG_UART_##idx##_NRF_HW_ASYNC_TIMER),)) \
		LOG_INSTANCE_PTR_INIT(log, LOG_MODULE_NAME, idx) \
	}; \
	K_TIMER_DEFINE(a2i_timer_##idx, async_to_irq_trampoline_timeout, NULL); \
	static nrfx_egu_t uart_egu_##idx = \
		COND_CODE_1(CONFIG_UART_##idx##_INT_DRIVEN_USE_EGU, \
			    (NRFX_EGU_INSTANCE(EGU_INSTANCE(idx))), \
			    ({})); \
	static uint8_t a2i_rxbuf_##idx[32]; \
	static uint8_t a2i_txbuf_##idx[8]; \
	static struct uart_nrfx_a2i a2i_data_##idx = { \
		.data = UART_ASYNC_TO_IRQ_API_DATA_INITIALIZE( &a2i_api,\
					  a2i_rxbuf_##idx, a2i_txbuf_##idx,\
					  async_to_irq_trampoline, \
					  LOG_INSTANCE_PTR(LOG_MODULE_NAME, idx)), \
		.timer = !IS_ENABLED(CONFIG_UART_##idx##_INT_DRIVEN_USE_EGU) ? \
			&a2i_timer_##idx : NULL, \
		.egu = IS_ENABLED(CONFIG_UART_##idx##_INT_DRIVEN_USE_EGU) ? \
			&uart_egu_##idx : NULL, \
	}; \
	static struct uarte_async_data uarte##idx##_async; \
	static struct uarte_nrfx_data uarte_##idx##_data = { \
		.a2i_data = IS_ENABLED(CONFIG_UART_##idx##_INTERRUPT_DRIVEN) ? \
			&a2i_data_##idx : NULL, \
		.async = (IS_ENABLED(CONFIG_UART_##idx##_INTERRUPT_DRIVEN) || \
		          IS_ENABLED(CONFIG_UART_##idx##_ASYNC)) ? \
			&uarte##idx##_async : NULL \
	}; \
	static int uarte_##idx##_init(const struct device *dev) \
	{ \
		(void)uarte_nrfx_pm_action;/* To avoid compilation warnings. */ \
		IRQ_CONNECT(DT_IRQN(UARTE(idx)), \
			    DT_IRQ(UARTE(idx), priority), \
			    nrfx_isr, nrfx_uarte_##idx##_irq_handler, 0); \
		irq_enable(DT_IRQN(UARTE(idx))); \
		COND_CODE_1(CONFIG_UART_##idx##_INT_DRIVEN_USE_EGU, \
			(IRQ_CONNECT(EGU_IRQN(idx), \
				     DT_IRQ(UARTE(idx), priority), \
				     nrfx_isr, EGU_IRQ_HANDLER(idx), 0); \
			 irq_enable(EGU_IRQN(idx)); \
			), \
			()) \
		return uarte_nrfx_init(dev); \
	} \
	PM_DEVICE_DT_DEFINE(UARTE(idx), uarte_nrfx_pm_action); \
	DEVICE_DT_DEFINE(UARTE(idx), \
		      uarte_##idx##_init, \
		      PM_DEVICE_DT_REF(UARTE(idx)), \
		      &uarte_##idx##_data, \
		      &uarte_##idx##_config, \
		      PRE_KERNEL_1, \
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, \
		      &uart_nrfx_uarte_driver_api) \

#ifdef CONFIG_UART_0_NRF_UARTE
UART_NRF_UARTE_DEVICE(0);
#endif

#ifdef CONFIG_UART_1_NRF_UARTE
UART_NRF_UARTE_DEVICE(1);
#endif

#ifdef CONFIG_UART_2_NRF_UARTE
UART_NRF_UARTE_DEVICE(2);
#endif

#ifdef CONFIG_UART_3_NRF_UARTE
UART_NRF_UARTE_DEVICE(3);
#endif

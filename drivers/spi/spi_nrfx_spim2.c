/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/nrf_gpio.h>
#include <hal/nrf_spim.h>
#include <zephyr/cache.h>
#include <zephyr/dt-bindings/memory-attr/memory-attr.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(spi_nrfx_spim, CONFIG_SPI_LOG_LEVEL);

#if CONFIG_SOC_NRF54L15
#define DBG_PORT NRF_P2
#define DBG_PIN 10
#else
#define DBG_PORT NRF_P0
#define DBG_PIN 27
#endif

#define CONFIG_SPI_NRFX_TX_RAM_BUFFER_SIZE 16
#define CONFIG_SPI_NRFX_RX_RAM_BUFFER_SIZE 0

#if (CONFIG_SPI_NRFX_TX_RAM_BUFFER_SIZE > 0)
#define SPI_TX_BUFFER_IN_RAM 1
#endif

#if (CONFIG_SPI_NRFX_RX_RAM_BUFFER_SIZE > 0)
#define SPI_RX_BUFFER_IN_RAM 1
#endif

#define SPIM(idx)			DT_NODELABEL(spi##idx)

#define SPIM_IS_CACHEABLE(idx) DMM_IS_REG_CACHEABLE(DT_PHANDLE(SPIM(idx), memory_regions))

#define SPIM_NRF_MODE(_op) \
	((_op) == 0 ? NRF_SPIM_MODE_0 : \
	 (_op) == SPI_MODE_CPHA ? NRF_SPIM_MODE_1 : \
	 (_op) == SPI_MODE_CPOL ? NRF_SPIM_MODE_2 : NRF_SPIM_MODE_3)

#define SPIM_NRF_BIT_ORDER(_op) \
	(((_op) & SPI_TRANSFER_LSB) ? NRF_SPIM_BIT_ORDER_LSB_FIRST : NRF_SPIM_BIT_ORDER_MSB_FIRST)

#define SPIM_CS_ACTIVE_HIGH(_op) ((_op) & SPI_CS_ACTIVE_HIGH)

#define SPIM_NRF_PRESCALER(_freq, _clock) DIV_ROUND_UP(_clock, _freq)

#define SPIM_NRF_FREQUENCY(_freq, _clock) \
	(_freq >= MHZ(32) ? NRF_SPIM_FREQ_32M : \
	 _freq >= MHZ(16) ? NRF_SPIM_FREQ_16M : \
	 _freq >= MHZ(8) ? NRF_SPIM_FREQ_8M : \
	 _freq >= MHZ(4) ? NRF_SPIM_FREQ_4M : \
	 _freq >= MHZ(2) ? NRF_SPIM_FREQ_2M : \
	 _freq >= MHZ(1) ? NRF_SPIM_FREQ_1M : \
	 _freq >= KHZ(500) ? NRF_SPIM_FREQ_500K : \
	 _freq >= KHZ(250) ? NRF_SPIM_FREQ_250K : NRF_SPIM_FREQ_125K)

#define OP_OK(_op) true

struct spi_cs_config {
	NRF_GPIO_Type *reg;
	uint32_t pin;
	bool active_high;
	uint16_t delay;
};

struct spi_current_cfg {
	const struct spi_config *config;
	struct spi_cs_config cs;
};

struct spi_current {
	const struct spi_buf_set *tx_bufs;
	const struct spi_buf_set *rx_bufs;
	uint8_t *rx_buf;
	uint8_t *tx_buf;
	uint32_t cnt;
	uint32_t max_cnt;
};

struct spi_nrfx_data {
	struct k_sem lock;
	struct k_sem sync;
	struct spi_current_cfg current_cfg;
	struct spi_current current;
	spi_callback_t cb;
	void * user_data;
	atomic_t busy;
	int result;
	bool locked;
};

struct spi_nrfx_config {
	NRF_SPIM_Type *reg;
	uint32_t flags;
	uint32_t freq;
	const struct pinctrl_dev_config *pcfg;
#ifdef SPI_TX_BUFFER_IN_RAM
	uint8_t *tx_buffer;
#endif
#ifdef SPI_RX_BUFFER_IN_RAM
	uint8_t *rx_buffer;
#endif
	const struct spi_nrfx_dt_spec *dt_spec;
	size_t dt_spec_cnt;
	uint32_t mem_attr;
	uint8_t orc;
	uint8_t rx_delay;
	uint8_t prep_delay;
};

NRF_GPIO_Type *nrf_gpio_get_reg(const struct device *dev)
{
	struct gpio_dev_config {
		NRF_GPIO_Type *reg;
	};

	const struct gpio_dev_config *config = dev->config;

	return config->reg;
}

static int configure(const struct device *dev, const struct spi_config *spi_cfg)
{
	const struct spi_nrfx_config *config = dev->config;
	struct spi_nrfx_data *data = dev->data;
	uint32_t op = spi_cfg->operation;
#if NRF_SPIM_HAS_PRESCALER
	uint8_t prescaler = SPIM_NRF_PRESCALER(spi_cfg->frequency, config->freq);
#else
	uint32_t freq = SPIM_NRF_FREQUENCY(spi_cfg->frequency, config->freq);
#endif
	nrf_spim_mode_t mode = SPIM_NRF_MODE(SPI_OP_MODE_GET(op));
	nrf_spim_bit_order_t bit_order = SPIM_NRF_BIT_ORDER(op);

	if (!OP_OK(op)) {
		return -ENOTSUP;
	}

#if NRF_SPIM_HAS_PRESCALER
	nrf_spim_prescaler_set(config->reg, prescaler);
#else
	nrf_spim_frequency_set(config->reg, freq);
#endif
	nrf_spim_configure(config->reg, mode, bit_order);

	data->current_cfg.config = spi_cfg;
	data->current_cfg.cs.reg = spi_cfg->cs.gpio.port ?
		nrf_gpio_get_reg(spi_cfg->cs.gpio.port) : NULL;
	data->current_cfg.cs.pin = spi_cfg->cs.gpio.pin;
	data->current_cfg.cs.active_high = SPIM_CS_ACTIVE_HIGH(op);
	data->current_cfg.cs.delay = 0;

	if (data->current_cfg.cs.reg) {
		nrf_gpio_port_pin_output_set(data->current_cfg.cs.reg, data->current_cfg.cs.pin);
		nrf_gpio_port_pin_write(data->current_cfg.cs.reg, data->current_cfg.cs.pin,
			data->current_cfg.cs.active_high ? 0 : 1);
	}

	return 0;
}

static uint8_t *prepare_tx_buffer(const struct device *dev, const struct spi_buf *buf)
{
	const struct spi_nrfx_config *config = dev->config;

	if (nrf_dma_accessible_check(&config->reg, buf)) {
		return buf->buf;
	}

	if (buf->len <= CONFIG_SPI_NRFX_TX_RAM_BUFFER_SIZE) {
		memcpy(config->tx_buffer, buf->buf, buf->len);
		if (config->mem_attr & DT_MEM_CACHEABLE) {
			sys_cache_data_flush_range(config->tx_buffer, buf->len);
		}
		return config->tx_buffer;
	}
#if CONFIG_HAS_NORDIC_DMM
	int err;
	uint8_t *outbuf;

	err = dmm_buffer_out_prepare(config->mem_reg, buf->buf, buf->len, (void **)&outbuf);
	if (err == 0) {
		return outbuf;
	}
#endif
	/*LOG_ERR("Backup buffer TX too small, has %d bytes but %d required",*/
			/*len, CONFIG_SPI_NRFX_TX_RAM_BUFFER_SIZE);*/
	return NULL;
}

static uint8_t *prepare_rx_buffer(const struct device *dev, const struct spi_buf *buf)
{
#if CONFIG_HAS_NORDIC_DMM
	const struct spi_nrfx_config *config = dev->config;
	uint8_t *outbuf;
	int err;

	if (len <= CONFIG_SPI_NRFX_RX_RAM_BUFFER_SIZE) {
		return config->rx_buffer;
	}

	err = dmm_buffer_in_prepare(config->mem_reg, buf->buf, buf->len, (void **)&outbuf);
	if (err < 0) {
		return NULL;
	}

	return outbuf;
#else
	return buf->buf;
#endif
}

static void finalize_spi_transaction(const struct device *dev, bool deactivate_cs)
{
	const struct spi_nrfx_config *config = dev->config;
	struct spi_nrfx_data *data = dev->data;
	struct spi_cs_config *cs = &data->current_cfg.cs;

	if (cs->reg) {
		nrf_gpio_port_pin_write(cs->reg, cs->pin, cs->active_high ? 0 : 1);
	}

	nrf_spim_disable(config->reg);
}

static void finish_transaction(const struct device *dev, int error)
{
	struct spi_nrfx_data *data = dev->data;

	finalize_spi_transaction(dev, true);

	if (data->cb) {
		spi_callback_t cb = data->cb;
		void *user_data = data->user_data;

		if (data->locked) {
			data->busy = 0;
		}
		cb(dev, error, user_data);
	} else {
		data->result = error;
		k_sem_give(&data->sync);
	}
}

static int transfer_next_chunk(const struct device *dev)
{
	struct spi_nrfx_data *data = dev->data;
	const struct spi_nrfx_config *config = dev->config;
	struct spi_current *curr = &data->current;
	size_t cnt = curr->cnt;
	size_t tx_len, rx_len;

	if (cnt < curr->tx_bufs->count) {
		curr->tx_buf = prepare_tx_buffer(dev, &curr->tx_bufs->buffers[cnt]);
		if (curr->tx_buf == NULL) {
			return -ENOMEM;
		}
		tx_len = curr->tx_bufs->buffers[cnt].len;
	} else {
		curr->tx_buf = NULL;
		tx_len = 0;
	}

	if (cnt < curr->rx_bufs->count) {
		curr->rx_buf = prepare_rx_buffer(dev, &curr->rx_bufs->buffers[cnt]);
		if (curr->rx_buf == NULL) {
			return -ENOMEM;
		}
		rx_len = curr->rx_bufs->buffers[cnt].len;
	} else {
		curr->rx_buf = NULL;
		rx_len = 0;
	}

#ifdef CONFIG_SOC_NRF52832_ALLOW_SPIM_DESPITE_PAN_58
		if (xfer.rx_length == 1 && xfer.tx_length <= 1) {
			if (dev_config->anomaly_58_workaround) {
				anomaly_58_workaround_setup(dev);
			} else {
				LOG_WRN("Transaction aborted since it would trigger "
					"nRF52832 PAN 58");
				error = -EIO;
			}
		}
#endif
	nrf_spim_enable(config->reg);
	nrf_spim_rx_buffer_set(config->reg, curr->rx_buf, rx_len);
	nrf_spim_tx_buffer_set(config->reg, curr->tx_buf, tx_len);
	k_busy_wait(data->current_cfg.cs.delay);
	nrf_spim_task_trigger(config->reg, NRF_SPIM_TASK_START);

	return 0;
}

static int wait_for_completion(const struct device *dev)
{
	struct spi_nrfx_data *data = dev->data;
	int result;

	if (data->cb) {
		return 0;
	}

	k_sem_take(&data->sync, K_FOREVER);
	result = data->result;
	if (data->locked) {
		data->busy = 0;
	} else {
		k_sem_give(&data->lock);
	}

	return result;
}

static int transceive(const struct device *dev,
		      const struct spi_config *spi_cfg,
		      const struct spi_buf_set *tx_bufs,
		      const struct spi_buf_set *rx_bufs,
		      bool asynchronous,
		      spi_callback_t cb,
		      void *userdata)
{
	struct spi_nrfx_data *data = dev->data;
	struct spi_current_cfg *curr_cfg = &data->current_cfg;
	int err;

	if (spi_cfg == curr_cfg->config && (data->locked)) {
		/* Locked and configured. */
		if (!atomic_cas(&data->busy, 0, 1)) {
			return -EBUSY;
		}
	} else {
		err = k_sem_take(&data->lock, K_FOREVER);
		if (err < 0) {
			return err;
		}
	}

	if (spi_cfg != curr_cfg->config) {
		err = configure(dev, spi_cfg);
		if (err < 0) {
			goto finish;
		}
	}

	if (curr_cfg->cs.reg) {
		nrf_gpio_port_pin_write(curr_cfg->cs.reg, curr_cfg->cs.pin,
				curr_cfg->cs.active_high ? 1 : 0);
	}

	data->cb = cb;
	data->user_data = userdata;
	data->current.cnt = 0;
	data->current.tx_bufs = tx_bufs;
	data->current.rx_bufs = rx_bufs;
	data->current.max_cnt = MAX(rx_bufs->count, tx_bufs->count);
	err = transfer_next_chunk(dev);
finish:
	if (err < 0) {
		finish_transaction(dev, err);
	}

	return wait_for_completion(dev);
}

static int spi_nrfx_transceive(const struct device *dev,
			       const struct spi_config *spi_cfg,
			       const struct spi_buf_set *tx_bufs,
			       const struct spi_buf_set *rx_bufs)
{
	return transceive(dev, spi_cfg, tx_bufs, rx_bufs, false, NULL, NULL);
}

#ifdef CONFIG_SPI_ASYNC
static int spi_nrfx_transceive_async(const struct device *dev,
				     const struct spi_config *spi_cfg,
				     const struct spi_buf_set *tx_bufs,
				     const struct spi_buf_set *rx_bufs,
				     spi_callback_t cb,
				     void *userdata)
{
	return transceive(dev, spi_cfg, tx_bufs, rx_bufs, true, cb, userdata);
}

static int complete_rx_buf(const struct device *dev)
{
	const struct spi_nrfx_config *config = dev->config;
	struct spi_nrfx_data *data = dev->data;
	struct spi_current *curr = &data->current;
	uint8_t *usr_buf = curr->rx_bufs->buffers[curr->cnt].buf;

	(void)config;
	if (!IS_ENABLED(CONFIG_HAS_NORDIC_DMM) && (CONFIG_SPI_NRFX_RX_RAM_BUFFER_SIZE == 0)) {
		return 0;
	}

	if (curr->rx_buf == usr_buf) {
		return 0;
	}

#if SPI_RX_BUFFER_IN_RAM

	if (curr->rx_buf == config->rx_buffer) {
		if (dev_config->mem_attr & DT_MEM_CACHEABLE) {
			sys_cache_data_invd_range(curr->rx_buf, curr->rx_len);
		}
		memcpy(usr_buf, curr->rx_buf, curr->rx_len);
		return 0;
	}
#endif

#if CONFIG_HAS_NORDIC_DMM
	return dmm_buffer_in_release(config->mem_reg, usr_buf, data->rx_len, async_rx->buf);
#endif
}

static void spim_isr(const struct device *dev)
{
	DBG_PORT->OUTSET=BIT(DBG_PIN);
	const struct spi_nrfx_config *config = dev->config;
	struct spi_nrfx_data *data = dev->data;
	int err;

	/* One END event is enabled so nothing else can trigger the interrupt. */
	__ASSERT_NO_MSG(nrf_spim_event_check(config->reg, NRF_SPIM_EVENT_END));

	nrf_spim_event_clear(config->reg, NRF_SPIM_EVENT_END);

	err = complete_rx_buf(dev);
	data->current.cnt++;
	if (data->current.cnt == data->current.max_cnt) {
		finish_transaction(dev, err);
	} else {
		err = transfer_next_chunk(dev);
		if (err < 0) {
			finish_transaction(dev, err);
		}
	}
	DBG_PORT->OUTCLR=BIT(DBG_PIN);
}

static int spi_nrfx_release(const struct device *dev,
			    const struct spi_config *spi_cfg)
{
	struct spi_nrfx_data *data = dev->data;

	if (data->busy) {
		return -EBUSY;
	}

	k_sem_give(&data->lock);

	return 0;
}

static int spim_init(const struct device *dev)
{
	const struct spi_nrfx_config *config = dev->config;
	struct spi_nrfx_data *data = dev->data;

	k_sem_init(&data->lock, 0, 1);
	k_sem_init(&data->sync, 0, 1);
	nrf_spim_iftiming_set(config->reg, config->rx_delay);
	nrf_spim_event_clear(config->reg, NRF_SPIM_EVENT_END);
	nrf_spim_int_enable(config->reg, NRF_SPIM_INT_END_MASK);

	(void)pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);

	return 0;
}

#endif /* CONFIG_SPI_ASYNC */
static DEVICE_API(spi, spi_nrfx_driver_api) = {
	.transceive = spi_nrfx_transceive,
#ifdef CONFIG_SPI_ASYNC
	.transceive_async = spi_nrfx_transceive_async,
#endif
#ifdef CONFIG_SPI_RTIO
	/*.iodev_submit = spi_rtio_iodev_default_submit,*/
#endif
	.release = spi_nrfx_release,
};

#define SPIM_CFG_FLAG_CACHEABLE BIT(0)

#define SPIM(idx)			DT_NODELABEL(spi##idx)
#define SPIM_PROP(idx, prop)		DT_PROP(SPIM(idx), prop)

#define SPI_NRFX_SPIM_DEFINE(idx)					       \
	IF_ENABLED(SPI_RX_BUFFER_IN_RAM ,				       \
		 (static uint8_t spim_##idx##_rx_buffer			       \
			[CONFIG_SPI_NRFX_RX_RAM_BUFFER_SIZE]		       \
			DMM_MEMORY_SECTION(SPIM(idx));))		       \
	IF_ENABLED(SPI_TX_BUFFER_IN_RAM,				       \
		(static uint8_t spim_##idx##_tx_buffer			       \
			[CONFIG_SPI_NRFX_TX_RAM_BUFFER_SIZE]		       \
			/*DMM_MEMORY_SECTION(SPIM(idx))*/;))		       \
	PINCTRL_DT_DEFINE(SPIM(idx));					       \
	static const struct spi_nrfx_config spi_##idx##_config = { \
		.reg = (NRF_SPIM_Type *)DT_REG_ADDR(SPIM(idx)), \
		.freq = NRF_PERIPH_GET_FREQUENCY(SPIM(idx)), \
		.flags = (/*SPIM_IS_CACHEABLE(idx) ? SPIM_CFG_FLAG_CACHEABLE :*/ 0), \
		.orc    = SPIM_PROP(idx, overrun_character),	       \
		.pcfg = PINCTRL_DT_DEV_CONFIG_GET(SPIM(idx)),		       \
		IF_ENABLED(SPI_RX_BUFFER_IN_RAM,			       \
			 (.rx_buffer = spim_##idx##_rx_buffer,))	       \
		IF_ENABLED(SPI_TX_BUFFER_IN_RAM,			       \
			 (.tx_buffer = spim_##idx##_tx_buffer,))	       \
		 COND_CODE_1(SPIM_PROP(idx, rx_delay_supported),	\
			     (.rx_delay = SPIM_PROP(idx, rx_delay),),	\
			     ())					\
	};								       \
	static struct spi_nrfx_data spi_##idx##_data;\
	static int nrf_spim_init##idx(const struct device *dev)	       \
	{								       \
		IRQ_CONNECT(DT_IRQN(SPIM(idx)), DT_IRQ(SPIM(idx), priority),   \
			    spim_isr, DEVICE_DT_GET(SPIM(idx)), 0);       \
		irq_enable(DT_IRQN(SPIM(idx)));			       \
		return spim_init(dev); \
	}								       \
	NRF_DT_CHECK_NODE_HAS_PINCTRL_SLEEP(SPIM(idx));			       \
	DEVICE_DT_DEFINE(SPIM(idx),					       \
		      nrf_spim_init##idx,					       \
		      /*PM_DEVICE_DT_GET(SPIM(idx))*/NULL,			       \
		      &spi_##idx##_data,				       \
		      &spi_##idx##_config,				       \
		      POST_KERNEL, 0,	       \
		      &spi_nrfx_driver_api)

#define COND_NRF_SPIM_DEVICE(unused, prefix, i, _) \
	IF_ENABLED(CONFIG_HAS_HW_NRF_SPIM##prefix##i, (SPI_NRFX_SPIM_DEFINE(prefix##i);))

/*SPI_NRFX_SPIM_DEFINE(22)*/
NRFX_FOREACH_PRESENT(SPIM, COND_NRF_SPIM_DEVICE, (), (), _)

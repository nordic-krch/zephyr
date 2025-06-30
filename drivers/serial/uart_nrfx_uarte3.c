/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/pinctrl.h>
#include <hal/nrf_uarte.h>
#include <hal/nrf_timer.h>
#include <helpers/nrfx_gppi.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(poc, 0);

#define BOUNCE_BUF_LEN 512
uint8_t bounce_buf[BOUNCE_BUF_LEN+32] __aligned(4);
uint32_t g_len = BOUNCE_BUF_LEN;
uint8_t *p_canary = &bounce_buf[BOUNCE_BUF_LEN];
uint8_t *p_bounce = bounce_buf;

#define UARTE(idx)                DT_NODELABEL(uart##idx)

#define UARTE_FLAG_RX_BUF_REQ BIT(0)
#define UARTE_FLAG_RXTO BIT(1)
#define UARTE_FLAG_RXEN BIT(2)

#define UARTE_MAGIC_BYTE 0xAA

struct uart_buf {
	uint8_t *buf;
	size_t len;
};

static int rx_evt_cnt;

#define NRF_BAUDRATE(baudrate) ((baudrate) == 300 ? 0x00014000 :\
	(baudrate) == 600    ? 0x00027000 :			\
	(baudrate) == 1200   ? NRF_UARTE_BAUDRATE_1200 :	\
	(baudrate) == 2400   ? NRF_UARTE_BAUDRATE_2400 :	\
	(baudrate) == 4800   ? NRF_UARTE_BAUDRATE_4800 :	\
	(baudrate) == 9600   ? NRF_UARTE_BAUDRATE_9600 :	\
	(baudrate) == 14400  ? NRF_UARTE_BAUDRATE_14400 :	\
	(baudrate) == 19200  ? NRF_UARTE_BAUDRATE_19200 :	\
	(baudrate) == 28800  ? NRF_UARTE_BAUDRATE_28800 :	\
	(baudrate) == 31250  ? NRF_UARTE_BAUDRATE_31250 :	\
	(baudrate) == 38400  ? NRF_UARTE_BAUDRATE_38400 :	\
	(baudrate) == 56000  ? NRF_UARTE_BAUDRATE_56000 :	\
	(baudrate) == 57600  ? NRF_UARTE_BAUDRATE_57600 :	\
	(baudrate) == 76800  ? NRF_UARTE_BAUDRATE_76800 :	\
	(baudrate) == 115200 ? NRF_UARTE_BAUDRATE_115200 :	\
	(baudrate) == 230400 ? NRF_UARTE_BAUDRATE_230400 :	\
	(baudrate) == 250000 ? NRF_UARTE_BAUDRATE_250000 :	\
	(baudrate) == 460800 ? NRF_UARTE_BAUDRATE_460800 :	\
	(baudrate) == 921600 ? NRF_UARTE_BAUDRATE_921600 :	\
	(baudrate) == 1000000 ? NRF_UARTE_BAUDRATE_1000000 : 0)

struct uart_data {
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	struct uart_config uart_config;
#endif
	struct uart_buf usr_buf[2];
	struct uart_buf tx_buf;
	uint8_t *curr_bounce_buf;
	uint32_t last_cnt;
	uint32_t usr_rd_off;
	uint32_t usr_wr_off;
	uint32_t bounce_off;
	uint32_t bounce_limit;
	atomic_t flags;
	int ref_cnt;

	uart_callback_t user_callback;
	void *user_data;

	struct k_timer rx_timer;
	k_timeout_t timeout;

	uint8_t bounce_idx;
	uint8_t idle_cnt;
	bool in_irq;
	uint8_t *anomaly_byte;
};

struct uart_cfg {
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	struct uart_config uart_config;
#endif
	NRF_UARTE_Type *uarte;
	NRF_TIMER_Type *timer;
	uint32_t uarte_irqn;
	uint32_t timer_irqn;
	const struct pinctrl_dev_config *pcfg;
	uint8_t *bounce_buf[2];
	size_t bounce_len;
	uint32_t buf_switch_len;
	nrf_uarte_baudrate_t baudrate;
	bool irq_prio;
	bool hwfc;
};

#define UARTE_TIMER_USR_CNT_CH 2
#define UARTE_TIMER_BUF_SWITCH_CH 1
#define UARTE_TIMER_CAPTURE_CH 0

static void user_callback(const struct device *dev, struct uart_event *evt)
{
	struct uart_data *data = dev->data;

	if (data->user_callback) {
		data->user_callback(dev, evt, data->user_data);
	}
}

static void notify_uart_rx_buf_release(const struct device *dev, uint8_t *buf)
{
	LOG_DBG("rx buf rel: %p", (void *)buf);
	struct uart_event evt = {
		.type = UART_RX_BUF_RELEASED,
		.data.rx_buf.buf = buf,
	};

	user_callback(dev, &evt);
}

static void notify_rx_disable(const struct device *dev)
{
	LOG_INF("rx dis");
	struct uart_event evt = {
		.type = UART_RX_DISABLED,
	};

	user_callback(dev, (struct uart_event *)&evt);
}

static void rx_buf_req(const struct device *dev)
{
	LOG_DBG("rx buf req");
	struct uart_event evt = {
		.type = UART_RX_BUF_REQUEST,
	};

	user_callback(dev, &evt);
}

static int rx_disable(const struct device *dev)
{
	const struct uart_cfg *cfg = dev->config;
	struct uart_data *data = dev->data;

	if (!(data->flags & UARTE_FLAG_RXEN)) {
		return -EALREADY;
	}

	LOG_INF("rx dis req");
	nrf_timer_event_clear(cfg->timer, nrf_timer_compare_event_get(UARTE_TIMER_BUF_SWITCH_CH));
	nrf_timer_event_clear(cfg->timer, nrf_timer_compare_event_get(UARTE_TIMER_USR_CNT_CH));
	nrf_timer_int_disable(cfg->timer, nrf_timer_compare_int_get(UARTE_TIMER_BUF_SWITCH_CH) |
					 nrf_timer_compare_int_get(UARTE_TIMER_USR_CNT_CH));
	k_timer_stop(&data->rx_timer);
	nrf_uarte_shorts_disable(cfg->uarte, NRF_UARTE_SHORT_ENDRX_STARTRX);
	nrf_uarte_task_trigger(cfg->uarte, NRF_UARTE_TASK_STOPRX);

	return 0;
}

static bool notify_rx_rdy(const struct device *dev)
{
	const struct uart_cfg *cfg = dev->config;
	struct uart_data *data = dev->data;
	size_t len = data->usr_wr_off - data->usr_rd_off;

	if (len == 0) {
		return data->usr_buf[0].buf != NULL;
	}
	rx_evt_cnt++;
	struct uart_event evt = {
		.type = UART_RX_RDY,
		.data.rx.buf = data->usr_buf[0].buf,
		.data.rx.len = len,
		.data.rx.offset = data->usr_rd_off
	};
	LOG_INF("rx rdy: %p len:%d off:%d",(void *)data->usr_buf[0].buf,
			len, data->usr_rd_off);
	if (len < 64) {
		LOG_HEXDUMP_DBG(&data->usr_buf[0].buf[data->usr_rd_off], len, "rx");
	}
	user_callback(dev, &evt);
	data->usr_rd_off += len;

	if (data->usr_rd_off == data->usr_buf[0].len) {
		notify_uart_rx_buf_release(dev, data->usr_buf[0].buf);
		data->usr_buf[0] = data->usr_buf[1];
		data->usr_buf[1].buf = NULL;
		data->usr_buf[1].len = 0;
		data->usr_rd_off = 0;
		data->usr_wr_off = 0;

		nrf_timer_event_clear(cfg->timer,
				nrf_timer_compare_event_get(UARTE_TIMER_USR_CNT_CH));
		nrf_timer_cc_set(cfg->timer, UARTE_TIMER_USR_CNT_CH,
				 nrf_timer_cc_get(cfg->timer, UARTE_TIMER_USR_CNT_CH) +
				 data->usr_buf[0].len);

		return data->usr_buf[0].buf != NULL;
	}

	return true;
}

static uint32_t get_byte_cnt(NRF_TIMER_Type *timer)
{
	nrf_timer_task_trigger(timer, nrf_timer_capture_task_get(UARTE_TIMER_CAPTURE_CH));

	return nrf_timer_cc_get(timer, UARTE_TIMER_CAPTURE_CH);
}

/* Fill current user buffer with data.
 * @param dev Device.
 * @param len Amount of new data in the bounce buffer.
 * @return Amount of copied data.
 */
static uint32_t fill_usr_buf(const struct device *dev, uint32_t len, int src)
{
	struct uart_data *data = dev->data;
	const struct uart_cfg *cfg = dev->config;
	uint8_t *buf = cfg->bounce_buf[data->bounce_idx];
	uint32_t usr_rem = data->usr_buf[0].len - data->usr_wr_off;
	uint32_t bounce_rem = data->bounce_limit - data->bounce_off;
	uint32_t cpy_len = MIN(bounce_rem, MIN(usr_rem, len));

	LOG_INF("Fill %d usr buf (len %d) from idx:%d (req len:%d rem:%d), "
		"bounce buf %d, from idx %d (limit %d rem %d)",
		src, data->usr_buf[0].len, data->usr_wr_off, len, usr_rem,
		data->bounce_idx, data->bounce_off, data->bounce_limit, bounce_rem);
	__ASSERT(cpy_len + data->bounce_off <= (sizeof(bounce_buf) /2), "Exceeding the buffer");
	memcpy(&data->usr_buf[0].buf[data->usr_wr_off], &buf[data->bounce_off], cpy_len);
	data->bounce_off += cpy_len;
	data->usr_wr_off += cpy_len;
	data->last_cnt += cpy_len;
	if (data->bounce_off == data->bounce_limit) {
		/* Bounce buffer drained */
		LOG_INF("Drained one bounce buffer");
		data->bounce_idx = data->bounce_idx == 0 ? 1 : 0;
		data->bounce_off = 0;
		data->bounce_limit = cfg->bounce_len;
	}

	return cpy_len;
}

static int anomaly_byte_handle(const struct device *dev)
{
	struct uart_data *data = dev->data;
	const struct uart_cfg *cfg = dev->config;
	int ret;

	if (data->anomaly_byte == NULL) {
		return 0;
	}

	uint32_t diff = cfg->uarte->DMA.RX.PTR - (uint32_t)data->curr_bounce_buf;

	if (diff < 2) {
		LOG_INF("applying too early try curr:%02x ano:%02x",
			data->curr_bounce_buf[0], *data->anomaly_byte);
		return -diff;
	}

	if ((data->curr_bounce_buf[0] == UARTE_MAGIC_BYTE) &&
	    (*data->anomaly_byte != UARTE_MAGIC_BYTE)) {
		LOG_INF("appying anomaly curr:%02x ano:%02x diff:%d",
			data->curr_bounce_buf[0], *data->anomaly_byte, diff);
		data->curr_bounce_buf[0] = *data->anomaly_byte;
		ret = 1;
	} else {
		LOG_INF("appying not done curr:%02x ano:%02x diff:%d",
			data->curr_bounce_buf[0], *data->anomaly_byte, diff);
		ret = 2;
	}

	data->anomaly_byte = NULL;

	return ret;
}
uint32_t lastlen;
/* Return false if RX should be stopped because user buffers are filled. */
static bool update_usr_buf(const struct device *dev, uint32_t len, bool notify_any, int src)
{
	struct uart_data *data = dev->data;
	int anomaly_rv = anomaly_byte_handle(dev);

	lastlen = len;
	LOG_INF("update usr_buf %d len:%d, anomaly:%d", src, len, anomaly_rv);
	while (len > 0) {
		uint32_t cpy_len = len ? fill_usr_buf(dev, len, src) : 0;
		bool usr_buf_full = data->usr_wr_off == data->usr_buf[0].len;

		len -= cpy_len;
		if (((len == 0) && notify_any) || usr_buf_full) {
			if (!notify_rx_rdy(dev)) {
				return false;
			}

			if (usr_buf_full) {
				rx_buf_req(dev);
			}
		}
	}

	return true;
}

uint32_t g_bounce_len[16];
uint32_t g_bounce_cnt[16];
uint32_t g_bounce_cnt2[16];
int g_bounce_last_cnt[16];
uint32_t g_bounce_byte[16];
uint32_t g_bounce_byte1[16];
uint32_t g_bounce_limit[16];
uint32_t g_bounce_new_cnt[16];
uint32_t g_bounce_off[16];
bool g_bounce_anomaly[16];
uint32_t *pg_bounce_len = g_bounce_len;
uint32_t *pg_bounce_cnt = g_bounce_cnt;
uint32_t *pg_bounce_cnt2 = g_bounce_cnt2;
int *pg_bounce_last_cnt = g_bounce_last_cnt;
uint32_t *pg_bounce_byte = g_bounce_byte;
uint32_t *pg_bounce_byte1 = g_bounce_byte1;
uint32_t *pg_bounce_limit = g_bounce_limit;
uint32_t *pg_bounce_new_cnt = g_bounce_new_cnt;
uint32_t *pg_bounce_off = g_bounce_off;
bool *pg_bounce_anomaly = g_bounce_anomaly;
uint32_t g_idx;

uint32_t cnt0_cnt;
uint32_t cnt1p_cnt;
static int bounce_buf_swap(const struct device *dev)
{
	struct uart_data *data = dev->data;
	const struct uart_cfg *cfg = dev->config;
	uint8_t *prev_bounce_buf = data->curr_bounce_buf;
	uint32_t prev_buf_cnt, new_cnt, cnt, cnt2, ptr;
	bool rxdrdy_evt, rxs_evt;
	uint8_t rxd = 0xaa;
	uint32_t prev_buf_inc = 1;
	int mode;
	int key = irq_lock();

	data->curr_bounce_buf = (data->curr_bounce_buf == cfg->bounce_buf[0]) ?
		cfg->bounce_buf[1] : cfg->bounce_buf[0];
	data->curr_bounce_buf[0] = UARTE_MAGIC_BYTE;

	nrf_uarte_event_clear(cfg->uarte, NRF_UARTE_EVENT_RXSTARTED);
	nrf_uarte_event_clear(cfg->uarte, NRF_UARTE_EVENT_RXDRDY);
	cfg->uarte->DMA.RX.PTR = (uint32_t)data->curr_bounce_buf;
	cnt = get_byte_cnt(cfg->timer);

	rxdrdy_evt = nrf_uarte_event_check(cfg->uarte, NRF_UARTE_EVENT_RXDRDY);
	rxs_evt = nrf_uarte_event_check(cfg->uarte, NRF_UARTE_EVENT_RXSTARTED);
	if (!rxdrdy_evt && !rxs_evt) {
		/* RXDRDY did not happen when PTR was set. Safest case. PTR was updated correctly.
		 * Last byte will be received to the previouls buffer.
		 */
		new_cnt = 0;
		mode = 0;
		goto no_collision;
	}
	/* Setting PTR collided with byte boundary. */
	rxd = *(volatile uint8_t *)((uint32_t)cfg->uarte + 0x518);
	cnt2 = get_byte_cnt(cfg->timer);
	while (!nrf_uarte_event_check(cfg->uarte, NRF_UARTE_EVENT_RXSTARTED)) {
	}
	ptr = cfg->uarte->DMA.RX.PTR;

	new_cnt = ptr - (uint32_t)data->curr_bounce_buf;
	cnt = cnt2;
#if 1
	if (new_cnt == 0) {
		/* New PTR is not incremented. It was written after LIST post ENDRX
		 * incrementation.
		 */
		/*int tmp = cnt - data->last_cnt;*/
		/*LOG_ERR("prev_cnt:%d bounce_off:%d limit:%d",*/
				/*tmp, data->bounce_off, data->bounce_limit);*/
		cnt0_cnt++;
		mode = 1;
		goto no_collision;
	}
#endif

#if 1
	if (new_cnt > 1) {
		/* New PTR value is not set. Re-set PTR is needed. Transfer continues to
		 * previous buffer.*/
		/*LOG_ERR("n:%d", new_cnt);*/
		cnt1p_cnt++;
		mode = 2;
		cfg->uarte->DMA.RX.PTR = (uint32_t)data->curr_bounce_buf;
		goto no_collision;
	}
#endif

	/* new_cnt == 1. New PTR incremented. It's possible that data is already
	 * copied to that new location or it is written to the tail of the previous
	 * bounce buffer. We try to detect what happens.
	 */
	mode = 3;
	prev_buf_inc = 0;
	prev_buf_cnt = cnt - data->last_cnt;
	prev_bounce_buf[data->bounce_off + prev_buf_cnt] = UARTE_MAGIC_BYTE;
	data->anomaly_byte = &prev_bounce_buf[data->bounce_off + prev_buf_cnt];
	goto no_collision;

	LOG_ERR("rxd:%02x new_cnt:%d cnt2:%d cnt:%d evt_cnt:%d, cnt0:%d cnt1p:%d",
		rxd, new_cnt, cnt2, cnt, rx_evt_cnt, cnt0_cnt, cnt1p_cnt);
	__ASSERT_NO_MSG(0);

no_collision:

	prev_buf_cnt = cnt - data->last_cnt;
	data->bounce_limit = data->bounce_off + prev_buf_cnt + prev_buf_inc;
	irq_unlock(key);

#if 0
	g_idx++;
	if (g_idx == ARRAY_SIZE(g_bounce_len)) {
		g_idx = 0;
	}
	g_bounce_len[g_idx] = prev_buf_cnt;
	g_bounce_cnt[g_idx] = cnt;
	g_bounce_last_cnt[g_idx] = data->last_cnt;
	g_bounce_byte[g_idx] = prev_bounce_buf[prev_buf_cnt-1];
	g_bounce_byte1[g_idx] = prev_bounce_buf[prev_buf_cnt-2];
	g_bounce_limit[g_idx] = data->bounce_limit;
	g_bounce_off[g_idx] = data->bounce_off;
	g_bounce_new_cnt[g_idx] = new_cnt;
	LOG_INF("buf swap prev:%d mode:%d", prev_buf_cnt, mode);
#endif
	return prev_buf_cnt;
}

static void bounce_buf_switch(const struct device *dev)
{
	struct uart_data *data = dev->data;
	const struct uart_cfg *cfg = dev->config;
	int new_data = nrf_timer_cc_get(cfg->timer, UARTE_TIMER_BUF_SWITCH_CH) -
		data->last_cnt;

	if (!update_usr_buf(dev, new_data < 0 ? 0 : new_data, false, 0)) {
		rx_disable(dev);
		return;
	}

	uint32_t prev_cnt = bounce_buf_swap(dev);

	if (update_usr_buf(dev, prev_cnt, false, 1)) {
		uint32_t next = data->last_cnt + cfg->buf_switch_len;
		uint32_t cnt = get_byte_cnt(cfg->timer);

		__ASSERT(next > cnt, "Setting CC too late next:%d cnt:%d", next, cnt);
		LOG_WRN("setting cc:%d", next);
		nrf_timer_cc_set(cfg->timer, UARTE_TIMER_BUF_SWITCH_CH, next);
	} else {
		/* Stop RX. */
		LOG_WRN("no buf to receive, stopping RX");
		rx_disable(dev);
	}

	return;
}

static void usr_buf_complete(const struct device *dev)
{
	const struct uart_cfg *cfg = dev->config;
	struct uart_data *data = dev->data;
	uint32_t rem = data->usr_buf[0].len - data->usr_wr_off;
	__ASSERT_NO_MSG(rem <= (get_byte_cnt(cfg->timer) - data->last_cnt));

	LOG_INF("user buf completed cnt:%d last:%d new_data:%d",
			get_byte_cnt(cfg->timer), data->last_cnt, rem);
	if (!update_usr_buf(dev, rem, true, 2)) {
		LOG_WRN("no buf to receive, stopping RX");
		/* Stop RX if there is no next buffer. */
		rx_disable(dev);
	}
}

static int rx_enable(const struct device *dev, uint8_t *buf, size_t len, int32_t timeout)
{
	struct uart_data *data = dev->data;
	const struct uart_cfg *cfg = dev->config;
	bool rx_to = (timeout != 0) && (timeout != SYS_FOREVER_US);
	static const uint32_t rx_int_mask = NRF_UARTE_INT_RXTO_MASK;
	uint32_t cnt = get_byte_cnt(cfg->timer);
	uint32_t rem_data = cnt - data->last_cnt;

	LOG_INF("rx en buf:%p len:%d pending data:%d b_off:%d b_limit:%d",
		(void *)buf, len, rem_data, data->bounce_off, data->bounce_limit);
	if (len == 0) {
		return -EINVAL;
	}

	atomic_or(&data->flags, UARTE_FLAG_RXEN);
	data->usr_buf[0].buf = buf;
	data->usr_buf[0].len = len;
	data->usr_rd_off = 0;
	data->usr_wr_off = 0;

#if 1
	if (rem_data < 128) {
		static uint8_t pending_buf[128];

		if (rem_data <= (data->bounce_limit - data->bounce_off)) {
			LOG_INF("pending from buf:%d from %d (limit %d)",
				data->bounce_idx, data->bounce_off, data->bounce_limit);
			memcpy(pending_buf, &cfg->bounce_buf[data->bounce_idx][data->bounce_off],
					rem_data);
		} else {
			LOG_INF("pending from both bufs from %d (limit %d), from new buf:%d",
				data->bounce_off, data->bounce_limit,
				rem_data - (data->bounce_limit - data->bounce_off));
			memcpy(pending_buf, &cfg->bounce_buf[data->bounce_idx][data->bounce_off],
					data->bounce_limit - data->bounce_off);
			int idx = (data->bounce_idx + 1) & 0x1;
			memcpy(&pending_buf[data->bounce_limit - data->bounce_off],
					cfg->bounce_buf[idx],
					rem_data - (data->bounce_limit - data->bounce_off));
		}
		LOG_HEXDUMP_ERR(pending_buf, rem_data, "pending data");
	}
#endif
	if (rem_data >= len) {
		LOG_INF("trigger rxto %d in irq:%d", cfg->uarte_irqn, data->in_irq);
		atomic_or(&data->flags, UARTE_FLAG_RXTO);
		NRFX_IRQ_PENDING_SET(cfg->uarte_irqn);
		return 0;
	} else if (rem_data) {
		(void)update_usr_buf(dev, rem_data, false, 3);
		len -= rem_data;
	}

	if (rx_to) {
		data->timeout = K_USEC(timeout / 5);
		data->timeout.ticks = MAX(data->timeout.ticks, 2);
		data->idle_cnt = 0;
	}

	int k = irq_lock();
	data->ref_cnt++;
	if (data->ref_cnt == 1) {
		(void)pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
		nrf_uarte_enable(cfg->uarte);
	}
	irq_unlock(k);

	data->last_cnt = 0;
	data->bounce_off = 0;
	data->bounce_idx = 0;
	data->curr_bounce_buf = cfg->bounce_buf[0];
	data->bounce_limit = cfg->bounce_len;
	/* Enable ArrayList. */
	nrf_uarte_shorts_enable(cfg->uarte, NRF_UARTE_SHORT_ENDRX_STARTRX);
	nrf_uarte_event_clear(cfg->uarte, NRF_UARTE_EVENT_RXDRDY);
	nrf_uarte_frame_timeout_set(cfg->uarte, 50);
	nrf_uarte_int_enable(cfg->uarte, rx_int_mask | (rx_to ? NRF_UARTE_INT_RXDRDY_MASK : 0));
	cfg->uarte->DMA.RX.PTR = (uint32_t)data->curr_bounce_buf;

	nrf_timer_event_clear(cfg->timer, nrf_timer_compare_event_get(UARTE_TIMER_BUF_SWITCH_CH));
	nrf_timer_event_clear(cfg->timer, nrf_timer_compare_event_get(UARTE_TIMER_USR_CNT_CH));
	nrf_timer_int_enable(cfg->timer, nrf_timer_compare_int_get(UARTE_TIMER_BUF_SWITCH_CH) |
					 nrf_timer_compare_int_get(UARTE_TIMER_USR_CNT_CH));

	nrf_timer_task_trigger(cfg->timer, NRF_TIMER_TASK_CLEAR);
	nrf_timer_task_trigger(cfg->timer, NRF_TIMER_TASK_START);

	nrf_timer_cc_set(cfg->timer, UARTE_TIMER_BUF_SWITCH_CH, cfg->buf_switch_len);
	nrf_timer_cc_set(cfg->timer, UARTE_TIMER_USR_CNT_CH, len);
	LOG_INF("rx en buf switch:%d user:%d", cfg->buf_switch_len, len);

	atomic_or(&data->flags, UARTE_FLAG_RX_BUF_REQ);
	nrf_uarte_task_trigger(cfg->uarte, NRF_UARTE_TASK_STARTRX);
	NRFX_IRQ_PENDING_SET(nrfx_get_irq_number(cfg->uarte));

	return 0;
}

static int callback_set(const struct device *dev, uart_callback_t callback, void *user_data)
{
	struct uart_data *data = dev->data;

	data->user_callback = callback;
	data->user_data = user_data;

	return 0;
}

static int tx_abort(const struct device *dev)
{
	const struct uart_cfg *cfg = dev->config;

	struct uart_data *data = dev->data;

	if (!data->tx_buf.buf) {
		return -EALREADY;
	}

	nrf_uarte_task_trigger(cfg->uarte, NRF_UARTE_TASK_STOPTX);

	return 0;
}

static int tx(const struct device *dev, const uint8_t *buf, size_t len, int32_t timeout)
{
	const struct uart_cfg *cfg = dev->config;
	struct uart_data *data = dev->data;

	if (data->tx_buf.buf) {
		return -EBUSY;
	}
	int k = irq_lock();
	data->ref_cnt++;
	if (data->ref_cnt == 1) {
		nrf_uarte_enable(cfg->uarte);
	}
	irq_unlock(k);

	LOG_INF("tx %p %d", (void*)buf, len);
	data->tx_buf.buf = (uint8_t *)buf;
	data->tx_buf.len = len;
	nrf_uarte_tx_buffer_set(cfg->uarte, buf, len);
	nrf_uarte_task_trigger(cfg->uarte, NRF_UARTE_TASK_STARTTX);

	return 0;
}

static int rx_buf_rsp(const struct device *dev, uint8_t *buf, size_t len)
{
	struct uart_data *data = dev->data;
	unsigned int key = irq_lock();
	int err;

	if (data->usr_buf[0].buf == NULL) {
		LOG_ERR("rx buf rsp too late");
		err = -EACCES;
	} else if (data->usr_buf[1].buf == NULL) {
		LOG_INF("rx buf rsp %p %d", (void *)buf, len);
		data->usr_buf[1].buf = buf;
		data->usr_buf[1].len = len;
		err = 0;
	} else {
		err = -EBUSY;
		LOG_ERR("rx buf rsp %d", err);
	}

	irq_unlock(key);

	return err;
}

static void notify_new_data(const struct device *dev)
{
	struct uart_data *data = dev->data;
	const struct uart_cfg *cfg = dev->config;
	uint32_t cnt = get_byte_cnt(cfg->timer);
	uint32_t new_data = cnt - data->last_cnt;
	LOG_INF("cnt:%d last:%d new:%d", cnt, data->last_cnt, new_data);

	(void)update_usr_buf(dev, new_data, true, 4);
}

static void rxdrdy_isr(const struct device *dev)
{
	const struct uart_cfg *cfg = dev->config;
	struct uart_data *data = dev->data;

	LOG_INF("rxdrdy isr");
	k_timer_start(&data->rx_timer, data->timeout, K_NO_WAIT);
	nrf_uarte_int_disable(cfg->uarte, NRF_UARTE_INT_RXDRDY_MASK);
}

int f_cnt[5];
int *gf_cnt=f_cnt;
static void rx_flush_handle(const struct device *dev)
{
	struct uart_data *data = dev->data;
	const struct uart_cfg *cfg = dev->config;
	uint32_t amount;
	uint8_t flush_buf[5];
	uint32_t cnt = get_byte_cnt(cfg->timer);
	uint32_t rem_data = cnt - data->last_cnt;
	uint8_t *dst;

	nrf_uarte_rx_buffer_set(cfg->uarte, flush_buf, 5);
	nrf_uarte_event_clear(cfg->uarte, NRF_UARTE_EVENT_RXSTARTED);
	nrf_uarte_task_trigger(cfg->uarte, NRF_UARTE_TASK_FLUSHRX);
	while (!nrf_uarte_event_check(cfg->uarte, NRF_UARTE_EVENT_ENDRX)) {
		/* empty */
	}

	nrf_uarte_event_clear(cfg->uarte, NRF_UARTE_EVENT_ENDRX);
	cfg->uarte->DMA.RX.MAXCNT = 1;
	if (!nrf_uarte_event_check(cfg->uarte, NRF_UARTE_EVENT_RXSTARTED)) {
		return;
	}

	nrf_uarte_event_clear(cfg->uarte, NRF_UARTE_EVENT_RXSTARTED);
	amount = nrf_uarte_rx_amount_get(cfg->uarte);

	if (rem_data <= (data->bounce_limit - data->bounce_off)) {
		/* instead of -1 it should be -amount but RXDRDY event is not generated
		 * for bytes following first that goes to FIFO.
		 */
		dst = &cfg->bounce_buf[data->bounce_idx][data->bounce_off + rem_data - 1];
		LOG_HEXDUMP_INF(flush_buf, amount, "flush to current buf");
		if (amount > 1) {
			LOG_ERR("cnt:%d (new cnt:%d) rem_data:%d bounce_off:%d bidx:%d",
				cnt, get_byte_cnt(cfg->timer),
				rem_data, data->bounce_off, data->bounce_idx);
			LOG_HEXDUMP_ERR(cfg->bounce_buf[data->bounce_idx],
					data->bounce_off + rem_data, "bounce");
		}
		LOG_HEXDUMP_ERR((uint8_t *)flush_buf, amount, "flushed");
	} else {
		/* See comment in if clause. */
		uint32_t sec_buf_off = rem_data - (data->bounce_limit - data->bounce_off) - 1;

		LOG_INF("flushing to new buf at index:%d off:%d limit:%d rem_data:%d cnt:%d "
			" last:%d",
			sec_buf_off, data->bounce_off, data->bounce_limit, rem_data,
			cnt, data->last_cnt);
		dst = &data->curr_bounce_buf[sec_buf_off];
		LOG_HEXDUMP_INF(flush_buf, amount, "flush to next buf");
		LOG_HEXDUMP_ERR((uint8_t *)flush_buf, amount, "flushed2");
	}

	f_cnt[amount]++;
	memcpy(dst, flush_buf, amount);

	/*LOG_HEXDUMP_INF((uint8_t *)cfg->uarte->DMA.RX.PTR, amount, "flushed");*/
}

static void rxto_isr(const struct device *dev, bool do_flush)
{
	struct uart_data *data = dev->data;
	const struct uart_cfg *cfg = dev->config;

	if (data->usr_buf[0].buf && (data->usr_buf[0].len == 0)) {
		while(1);
	}
	LOG_INF("rxto isr");
	if (data->usr_buf[0].buf) {
		notify_new_data(dev);
	}

	for (int i = 0; i < ARRAY_SIZE(data->usr_buf); i++) {
		if (data->usr_buf[i].buf) {
			notify_uart_rx_buf_release(dev, data->usr_buf[i].buf);
			data->usr_buf[i].buf = NULL;
		}
	}

	if (do_flush) {
		rx_flush_handle(dev);

		int k = irq_lock();
		data->ref_cnt--;
		if (data->ref_cnt == 0) {
			(void)pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_SLEEP);
			nrf_uarte_disable(cfg->uarte);
		}
		irq_unlock(k);
	}

	nrf_timer_task_trigger(cfg->timer, NRF_TIMER_TASK_STOP);
	atomic_and(&data->flags, ~UARTE_FLAG_RXEN);
	notify_rx_disable(dev);
}

static void txstopped_isr(const struct device *dev)
{
	const struct uart_cfg *cfg = dev->config;
	struct uart_data *data = dev->data;
	uint32_t amount = nrf_uarte_tx_amount_get(cfg->uarte);

	struct uart_event evt = {
		.type = (amount == data->tx_buf.len) ? UART_TX_DONE : UART_TX_ABORTED,
		.data.tx.buf = data->tx_buf.buf,
		.data.tx.len = amount
	};

	int k = irq_lock();
	data->ref_cnt--;
	if (data->ref_cnt == 0) {
		(void)pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_SLEEP);
		nrf_uarte_disable(cfg->uarte);
	}
	irq_unlock(k);

	LOG_INF("tx done: %d", evt.data.tx.len);
	data->tx_buf.buf = NULL;
	user_callback(dev, (struct uart_event *)&evt);
}

static void rx_timeout(struct k_timer *timer)
{
	const struct device *dev = k_timer_user_data_get(timer);
	const struct uart_cfg *cfg = dev->config;
	struct uart_data *data = dev->data;

	if (nrf_uarte_event_check(cfg->uarte, NRF_UARTE_EVENT_RXDRDY)) {
		nrf_uarte_event_clear(cfg->uarte, NRF_UARTE_EVENT_RXDRDY);
		data->idle_cnt = 0;
	} else {
		data->idle_cnt++;
		/* We compare against RX_TIMEOUT_DIV - 1 to get rather earlier timeout
		 * than late. idle_cnt is reset when last RX activity (RXDRDY event) is
		 * detected. It may happen that it happens when RX is inactive for whole
		 * RX timeout period (and it is the case when transmission is short compared
		 * to the timeout, for example timeout is 50 ms and transmission of few bytes
		 * takes less than 1ms). In that case if we compare against RX_TIMEOUT_DIV
		 * then RX notification would come after (RX_TIMEOUT_DIV + 1) * timeout.
		 */
		if (data->idle_cnt >= 4) {
			LOG_INF("rx timeout, report new data");
			if (cfg->irq_prio) {
				if (data->in_irq) {
					/* TIMER or UARTE interrupt preempted. Lets try again
					 * later.
					 */
					k_timer_start(timer, data->timeout, K_NO_WAIT);
					return;
				}
				irq_disable(cfg->uarte_irqn);
				irq_disable(cfg->timer_irqn);
			}

			nrf_uarte_int_enable(cfg->uarte, NRF_UARTE_INT_RXDRDY_MASK);
			notify_new_data(dev);

			if (cfg->irq_prio) {
				irq_enable(cfg->uarte_irqn);
				irq_enable(cfg->timer_irqn);
			}
			return;
		}
	}

	k_timer_start(timer, data->timeout, K_NO_WAIT);
}

static bool event_check_clear(NRF_UARTE_Type *uarte, nrf_uarte_event_t event,
				uint32_t int_mask, uint32_t int_en_mask)
{
	if (nrf_uarte_event_check(uarte, event) && (int_mask & int_en_mask)) {
		nrf_uarte_event_clear(uarte, event);
		return true;
	}

	return false;
}

static void uarte_irq(void *arg)
{
	NRF_P2->OUTSET=BIT(8);
	const struct device *dev = arg;
	struct uart_data *data = dev->data;
	const struct uart_cfg *cfg = dev->config;
	NRF_UARTE_Type *uarte = cfg->uarte;
	uint32_t flags;
	uint32_t int_mask;

	data->in_irq = true;
	flags = atomic_and(&data->flags, ~(UARTE_FLAG_RX_BUF_REQ | UARTE_FLAG_RXTO));
	int_mask = nrf_uarte_int_enable_check(uarte, UINT32_MAX);

	if (event_check_clear(uarte, NRF_UARTE_EVENT_RXDRDY,
				NRF_UARTE_INT_RXDRDY_MASK, int_mask)) {
		rxdrdy_isr(dev);
	}

	if (event_check_clear(uarte, NRF_UARTE_EVENT_RXTO, NRF_UARTE_INT_RXTO_MASK, int_mask)) {
		/* RXTO. */
		rxto_isr(dev, true);
	}

	if (event_check_clear(uarte, NRF_UARTE_EVENT_TXSTOPPED,
				NRF_UARTE_INT_TXSTOPPED_MASK, int_mask)) {
		txstopped_isr(dev);
	}

	data->in_irq = false;
	NRF_P2->OUTCLR=BIT(8);
}

static bool timer_ch_evt_check_clear(NRF_TIMER_Type *timer, uint32_t ch)
{
	nrf_timer_event_t evt = nrf_timer_compare_event_get(ch);

	if (nrf_timer_event_check(timer, evt)) {
		nrf_timer_event_clear(timer, evt);
		return true;
	}

	return false;
}

static void timer_irq(void *arg)
{
	const struct device *dev = arg;
	const struct uart_cfg *cfg = dev->config;
	struct uart_data *data = dev->data;

	data->in_irq = true;

	if (timer_ch_evt_check_clear(cfg->timer, UARTE_TIMER_BUF_SWITCH_CH)) {
		bounce_buf_switch(dev);
	}

	if (timer_ch_evt_check_clear(cfg->timer, UARTE_TIMER_USR_CNT_CH)) {
		usr_buf_complete(dev);
	}

	if (flags & UARTE_FLAG_RX_BUF_REQ) {
		rx_buf_req(dev);
	}

	if (flags & UARTE_FLAG_RXTO) {
		rxto_isr(dev, false);
	}

	data->in_irq = false;
}

static void byte_count_setup(NRF_UARTE_Type *uarte, NRF_TIMER_Type *timer)
{
	uint32_t evt = nrf_uarte_event_address_get(uarte, NRF_UARTE_EVENT_RXDRDY);
	uint32_t tsk = nrf_timer_task_address_get(timer, NRF_TIMER_TASK_COUNT);
	nrfx_err_t ret;
	uint8_t ch;

	nrf_timer_mode_set(timer, NRF_TIMER_MODE_COUNTER);
	nrf_timer_bit_width_set(timer, NRF_TIMER_BIT_WIDTH_32);

	ret = nrfx_gppi_channel_alloc(&ch);
	nrfx_gppi_channel_endpoints_setup(ch, evt, tsk);
	nrfx_gppi_channels_enable(BIT(ch));


	NRF_GPIOTE20->CONFIG[0]= (1 << 9) | (13 <<4) | 3 | (3 << 16);
	NRF_GPIOTE20->CONFIG[1]= (1 << 9) | (14 <<4) | 3 | (3 << 16);
	NRF_GPIOTE20->CONFIG[2]= (1 << 9) | (12 <<4) | 3 | (3 << 16);
	NRF_GPIOTE20->SUBSCRIBE_OUT[0] = uarte->PUBLISH_RXDRDY;
	/*uint32_t evt2 = nrf_uarte_event_address_get(uarte, NRF_UARTE_EVENT_FRAME_TIMEOUT);*/
	uint32_t evt2 = (uint32_t)&NRF_RRAMC->EVENTS_WOKENUP;
	uint32_t tsk2 = (uint32_t)&NRF_GPIOTE20->TASKS_OUT[1];
	uint8_t ch2;
	ret = nrfx_gppi_channel_alloc(&ch2);
	nrfx_gppi_channel_endpoints_setup(ch2, evt2, tsk2);
	nrfx_gppi_channels_enable(BIT(ch2));

#if 0
	uint32_t evt3 = nrf_uarte_event_address_get(uarte, NRF_UARTE_EVENT_ENDRX);
	uint32_t tsk3 = (uint32_t)&NRF_GPIOTE20->TASKS_OUT[2];
	uint8_t ch3;
	ret = nrfx_gppi_channel_alloc(&ch3);
	nrfx_gppi_channel_endpoints_setup(ch3, evt3, tsk3);
	nrfx_gppi_channels_enable(BIT(ch3));
#endif
}

static int init(const struct device *dev)
{
	const struct uart_cfg *cfg = dev->config;
	struct uart_data *data = dev->data;
	nrf_uarte_config_t config = {
		.frame_timeout = NRF_UARTE_FRAME_TIMEOUT_EN,
		.hwfc = cfg->hwfc ? NRF_UARTE_HWFC_ENABLED : NRF_UARTE_HWFC_DISABLED,
	};

	byte_count_setup(cfg->uarte, cfg->timer);

	*(volatile uint32_t *)((uint32_t)cfg->uarte + 0x714) = 1;
	cfg->uarte->DMA.RX.MAXCNT = 1;
	nrf_uarte_baudrate_set(cfg->uarte, cfg->baudrate);
	nrf_uarte_configure(cfg->uarte, &config);
	nrf_uarte_shorts_enable(cfg->uarte, NRF_UARTE_SHORT_ENDTX_STOPTX);

	IRQ_CONNECT(UARTE21_IRQn, 1, uarte_irq, DEVICE_DT_GET(UARTE(21)), 0);
	irq_enable(UARTE21_IRQn);
	IRQ_CONNECT(TIMER21_IRQn, 1, timer_irq, DEVICE_DT_GET(UARTE(21)), 0);
	irq_enable(TIMER21_IRQn);

	nrf_uarte_int_enable(cfg->uarte, NRF_UARTE_INT_TXSTOPPED_MASK);

	k_timer_init(&data->rx_timer, rx_timeout, NULL);
	k_timer_user_data_set(&data->rx_timer, (void *)dev);

	data->uart_config = cfg->uart_config;


	return 0;
}

#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
static int config_get(const struct device *dev, struct uart_config *cfg)
{
	struct uart_data *data = dev->data;

	*cfg = data->uart_config;
	return 0;
}

static int configure(const struct device *dev, const struct uart_config *cfg)
{
	struct uart_data *data = dev->data;
	const struct uart_cfg *dev_cfg = dev->config;
	nrf_uarte_config_t uarte_cfg;

	uarte_cfg.frame_timeout = NRF_UARTE_FRAME_TIMEOUT_DIS;
#if defined(UARTE_CONFIG_STOP_Msk)
	switch (cfg->stop_bits) {
	case UART_CFG_STOP_BITS_1:
		uarte_cfg.stop = NRF_UARTE_STOP_ONE;
		break;
	case UART_CFG_STOP_BITS_2:
		uarte_cfg.stop = NRF_UARTE_STOP_TWO;
		break;
	default:
		LOG_ERR("%d cfg:%d", __LINE__, cfg->stop_bits);
		return -ENOTSUP;
	}
#else
	if (cfg->stop_bits != UART_CFG_STOP_BITS_1) {
		LOG_ERR("%d", __LINE__);
		return -ENOTSUP;
	}
#endif

	if (cfg->data_bits != UART_CFG_DATA_BITS_8) {
		LOG_ERR("%d", __LINE__);
		return -ENOTSUP;
	}

	switch (cfg->flow_ctrl) {
	case UART_CFG_FLOW_CTRL_NONE:
		uarte_cfg.hwfc = NRF_UARTE_HWFC_DISABLED;
		break;
	case UART_CFG_FLOW_CTRL_RTS_CTS:
		uarte_cfg.hwfc = NRF_UARTE_HWFC_ENABLED;
		break;
	default:
		LOG_ERR("%d", __LINE__);
		return -ENOTSUP;
	}

#if defined(UARTE_CONFIG_PARITYTYPE_Msk)
	uarte_cfg.paritytype = NRF_UARTE_PARITYTYPE_EVEN;
#endif
	switch (cfg->parity) {
	case UART_CFG_PARITY_NONE:
		uarte_cfg.parity = NRF_UARTE_PARITY_EXCLUDED;
		break;
	case UART_CFG_PARITY_EVEN:
		uarte_cfg.parity = NRF_UARTE_PARITY_INCLUDED;
		break;
#if defined(UARTE_CONFIG_PARITYTYPE_Msk)
	case UART_CFG_PARITY_ODD:
		uarte_cfg.parity = NRF_UARTE_PARITY_INCLUDED;
		uarte_cfg.paritytype = NRF_UARTE_PARITYTYPE_ODD;
		break;
#endif
	default:
		LOG_ERR("%d", __LINE__);
		return -ENOTSUP;
	}

	nrf_uarte_baudrate_t nrf_baudrate = NRF_BAUDRATE(cfg->baudrate);
	nrf_uarte_baudrate_set(dev_cfg->uarte, nrf_baudrate);


#if NRF_UARTE_HAS_FRAME_SIZE
	uarte_cfg.frame_size = NRF_UARTE_FRAME_SIZE_8_BIT;
	uarte_cfg.endian = NRF_UARTE_ENDIAN_MSB;
#endif

	nrf_uarte_configure(dev_cfg->uarte, &uarte_cfg);

	data->uart_config = *cfg;

	return 0;
}
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */

static DEVICE_API(uart, uart_driver_api) = {
	.callback_set		= callback_set,
	.tx			= tx,
	.tx_abort		= tx_abort,
	.rx_enable		= rx_enable,
	.rx_buf_rsp		= rx_buf_rsp,
	.rx_disable		= rx_disable,
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	.configure              = configure,
	.config_get             = config_get,
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */
};

PINCTRL_DT_DEFINE(UARTE(21));
static const struct uart_cfg uart_cfg = {
	.pcfg = PINCTRL_DT_DEV_CONFIG_GET(UARTE(21)),
	.uarte = NRF_UARTE21,
	.uarte_irqn = SERIAL21_IRQn,
	.timer = NRF_TIMER21,
	.timer_irqn = TIMER21_IRQn,
	.bounce_buf = { bounce_buf, &bounce_buf[BOUNCE_BUF_LEN / 2] },
	.bounce_len = BOUNCE_BUF_LEN / 2,
	.buf_switch_len = (BOUNCE_BUF_LEN / 2) - 56,
	.irq_prio = DT_IRQ(DT_NODELABEL(grtc), priority) != DT_IRQ(DT_NODELABEL(uart21), priority),
	.baudrate = NRF_BAUDRATE(DT_PROP(DT_NODELABEL(uart21), current_speed)),
	.hwfc = (DT_PROP(DT_NODELABEL(uart21), hw_flow_control) ==
			UART_CFG_FLOW_CTRL_RTS_CTS),
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	.uart_config = {
		.baudrate = DT_PROP(DT_NODELABEL(uart21), hw_flow_control),
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = DT_PROP(DT_NODELABEL(uart21), hw_flow_control)
	}
#endif
};
static struct uart_data uart_data;

#if 0
DEVICE_DT_DEFINE(UARTE(21),
		init,
		NULL,
		&uart_data,
		&uart_cfg,
		PRE_KERNEL_2,
		CONFIG_SERIAL_INIT_PRIORITY,
		&uart_driver_api);

#endif

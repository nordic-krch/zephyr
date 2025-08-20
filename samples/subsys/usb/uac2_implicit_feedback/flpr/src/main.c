#include <hal/nrf_gpio.h>
#include <hal/nrf_tdm.h>
#include <hal/nrf_grtc.h>
#include <dmm.h>
#include <zephyr/kernel.h>
#include <../drivers/usb/common/usb_dwc2_hw.h>
#include "../../src/feedback.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_WRN);
/* === BUFFERS === */
/* buffers configuration */
#define ISO_IN_CH_CNT  6
#define ISO_OUT_CH_CNT 2
#define TDM_WORD_SIZE 32

#if ISO_IN_CH_CNT > ISO_OUT_CH_CNT
	#if ISO_IN_CH_CNT > 4
		#define TDM_CH_CNT 8
	#elif ISO_IN_CH_CNT > 2
		#define TDM_CH_CNT 4
	#else
		#define TDM_CH_CNT ISO_IN_CH_CNT
	#endif
#else
	#if ISO_OUT_CH_CNT > 4
		#define TDM_CH_CNT 8
	#elif ISO_OUT_CH_CNT > 2
		#define TDM_CH_CNT 4
	#else
		#define TDM_CH_CNT ISO_OUT_CH_CNT
	#endif
#endif

#define TDM_RX_CH_CNT TDM_CH_CNT /*ISO_IN_CH_CNT*/
#define TDM_TX_CH_CNT TDM_CH_CNT /*ISO_OUT_CH_CNT*/

#define SAMPLES_NUM    (6 * HIGH_SPEED_SOF_PERIODS)
#define BUFFERS_NUM    3
#define NEXT_BUFFER(x) ((x + 1) % BUFFERS_NUM)

#define DBG_PIN_0 2
#define DBG_PIN_1 3
#define DBG_PIN_2 4
#define DBG_PIN_3 5

#define USE_DBG_PIN 1

#if USE_DBG_PIN
#define DBG_PIN_SET(x) NRF_P9->OUTSET = BIT(DBG_PIN_##x)
#define DBG_PIN_CLR(x) NRF_P9->OUTCLR = BIT(DBG_PIN_##x)
#define DBG_PIN_INIT(x) nrf_gpio_cfg_output(9*32 + DBG_PIN_##x)
#else
#define DBG_PIN_SET(x)
#define DBG_PIN_CLR(x)
#define DBG_PIN_INIT(x)
#endif

#define TDM_GET_MAX_LEN(buf_len, num_of_channels) ((buf_len * TDM_CH_CNT) / num_of_channels)

static struct feedback_ctx * mp_fbck;

typedef enum {
	tdm_tx,
	tdm_rx
} tdm_dir_t;

#if TDM_WORD_SIZE > 16
typedef uint32_t sample_t;
#else
typedef uint16_t sample_t;
#endif

typedef struct {
	void * ptr;
	/* Indicates how many samples for all channels is in the buffer. */
	size_t sample_num;
} buf_t;

static buf_t m_iso_in_buffers[BUFFERS_NUM];
static buf_t m_iso_out_buffers[BUFFERS_NUM];
static uint8_t m_iso_in_idx;
static uint8_t m_iso_out_idx;

//TODO MEMORY REGION VERIFY!
/*static uint32_t m_fake_sample_tx[SAMPLES_NUM + 1] __aligned(4);*/
/*static uint32_t m_fake_sample_rx[SAMPLES_NUM + 1] __aligned(4);*/

#define NRFX_TDM_NUM_OF_CHANNELS (TDM_CONFIG_CHANNEL_NUM_NUM_Max + 1)

#define NRFX_TDM_TX_CHANNELS_MASK                                                                  \
	GENMASK(TDM_CONFIG_CHANNEL_MASK_Tx0Enable_Pos + TDM_CONFIG_CHANNEL_NUM_NUM_Max,            \
		TDM_CONFIG_CHANNEL_MASK_Tx0Enable_Pos)
#define NRFX_TDM_RX_CHANNELS_MASK                                                                  \
	GENMASK(TDM_CONFIG_CHANNEL_MASK_Rx0Enable_Pos + TDM_CONFIG_CHANNEL_NUM_NUM_Max,            \
		TDM_CONFIG_CHANNEL_MASK_Rx0Enable_Pos)

static void buffers_flush(void)
{
	m_iso_in_idx = 0;
	m_iso_out_idx = 0;
	memset(m_iso_in_buffers, 0, sizeof(m_iso_in_buffers));
	memset(m_iso_out_buffers, 0, sizeof(m_iso_out_buffers));
}

/* === TDM === */
#define MCKCONST       1048576UL
#define AUDIOPLL_FREQ  12287963UL
#define WORD_SIZE      CONCAT(TDM_WORD_SIZE, UL)
#define FRAME_CLK_FREQ 48000UL
#define SCK_FREQ       WORD_SIZE * FRAME_CLK_FREQ * TDM_CH_CNT
#define SCK_DIV_VALUE  ((uint32_t)(((uint64_t)SCK_FREQ * MCKCONST) / (AUDIOPLL_FREQ + (SCK_FREQ / 2) )* 4096))

static const nrf_tdm_config_t m_cfg = {
	.mode = NRF_TDM_MODE_MASTER,
	.sample_width = CONCAT(NRF_TDM_SWIDTH_, TDM_WORD_SIZE, BIT),
	.channels = FIELD_PREP(NRFX_TDM_RX_CHANNELS_MASK, BIT_MASK(TDM_RX_CH_CNT)) | FIELD_PREP(NRFX_TDM_TX_CHANNELS_MASK, BIT_MASK(TDM_TX_CH_CNT)),
	.num_of_channels = CONCAT(NRF_TDM_CHANNELS_COUNT_, TDM_CH_CNT),
	.mck_setup = 0,
	.sck_setup = SCK_DIV_VALUE,
	// Set PCM long format
	.alignment = NRF_TDM_ALIGN_LEFT,
	.fsync_polarity = NRF_TDM_POLARITY_POSEDGE,
	.sck_polarity = NRF_TDM_POLARITY_NEGEDGE,
	.fsync_duration = NRF_TDM_FSYNC_DURATION_SCK,
	.channel_delay = NRF_TDM_CHANNEL_DELAY_NONE,
};

static const nrf_tdm_pins_t m_pins = {
	.sck_pin = NRF_GPIO_PIN_MAP(1,3),
	.fsync_pin = NRF_GPIO_PIN_MAP(1,6),
	.mck_pin = NRF_TDM_PIN_NOT_CONNECTED,
	.sdout_pin = NRF_GPIO_PIN_MAP(1,4),
	.sdin_pin = NRF_GPIO_PIN_MAP(1,5),
};

static bool    m_tdm_started;
static uint8_t m_tdm_counter;
static uint32_t m_tdm_plus_ones;
static uint32_t m_tdm_minus_ones;

/* Minimum Number of samples stored in ringbuf which allows fetching data from that ringbuf. */
#define RINGBUF_THR (3 * SAMPLES_NUM)

struct ringbuf {
	sample_t *buf;
	uint32_t cons_idx;
	uint32_t prod_idx;
	uint32_t total;
	uint32_t size;
	/* Number of valid channel samples in the input buffer to put function. */
	uint32_t ch_valid;
	/* Number of channel that shall be skipped in the input buffer to put function. */
	uint32_t ch_skip;
};

#define BUF_COUNT 6
#define BUF_ALIGN sizeof(uint32_t)

static sample_t iso_in_buf[ISO_IN_CH_CNT * (SAMPLES_NUM + 1) * 10];
static sample_t iso_out_buf[ISO_OUT_CH_CNT * (SAMPLES_NUM + 1) * 10];

K_MEM_SLAB_DEFINE_STATIC(iso_in_slab,
			 ROUND_UP((SAMPLES_NUM + 1) * ISO_IN_CH_CNT * sizeof(sample_t), BUF_ALIGN),
			 BUF_COUNT, BUF_ALIGN);
K_MEM_SLAB_DEFINE_STATIC(iso_out_slab,
			 ROUND_UP((SAMPLES_NUM + 1) * ISO_OUT_CH_CNT * sizeof(sample_t), BUF_ALIGN),
			 BUF_COUNT, BUF_ALIGN);

static sample_t tdm_tx_buffers[2][TDM_TX_CH_CNT * (SAMPLES_NUM + 1)]
	__aligned(sizeof(uint32_t)) DMM_MEMORY_SECTION(DT_NODELABEL(tdm130));

static sample_t tdm_rx_buffers[2][TDM_RX_CH_CNT * (SAMPLES_NUM + 1)]
	__aligned(sizeof(uint32_t)) DMM_MEMORY_SECTION(DT_NODELABEL(tdm130));

struct tdm_data {
	sample_t *buf[2];
	sample_t len[2];
	uint32_t idx;
	int len_offset;
};
struct uac2_context {
	struct ringbuf to_usb;
	struct ringbuf from_usb;
	struct tdm_data tdm_tx;
	struct tdm_data tdm_rx;
};

struct uac2_context context;

/* Simple ring buffer that holds data between TDM and USB. It is not thread safe. */
static void ringbuf_reset(struct ringbuf *rb)
{
	rb->total = 0;
	rb->prod_idx = 0;
	rb->cons_idx = 0;
}

static int ringbuf_init(struct ringbuf *rb, sample_t *buf, size_t size,
			uint32_t ch_valid, uint32_t ch_skip)
{
	if (size % ch_valid) {
		return -EINVAL;
	}

	rb->buf = buf;
	rb->size = size;
	rb->ch_valid = ch_valid;
	rb->ch_skip = ch_skip;
	ringbuf_reset(rb);

	return 0;
}

/* Put number of samples * number of channels. It takes into account how many
 * channels are valid and how many should be skipped.
 * For example ch_valid = 2 and ch_skip = 1 put will do following:
 * - copy 2 samples
 * - skip 1 sample
 * - copy 2 samples
 * - ...
 */
static int ringbuf_put(struct ringbuf *rb, sample_t *buf, size_t sample_num)
{
	size_t rem_space = rb->size - rb->prod_idx;
	size_t len = sample_num * rb->ch_valid;
	sample_t *dst = &rb->buf[rb->prod_idx];
	size_t cpy_len = MIN(len, rem_space);
	size_t cpy_sample = cpy_len / rb->ch_valid;

	if (rb->total + len > rb->size) {
		LOG_WRN("rb %p put no mem", rb);
		return -ENOMEM;
	}

	rb->total += len;
	for (int i = 0; i < cpy_sample; i++) {
		for (int j = 0; j < rb->ch_valid; j++) {
			*dst = *buf;
			dst++;
			buf++;
		}
		buf += rb->ch_skip;
	}
	rb->prod_idx += cpy_len;
	if (rb->prod_idx == rb->size) {
		rb->prod_idx = 0;
	}

	len -= cpy_len;

	if (len == 0) {
		return 0;
	}

	cpy_sample = sample_num - cpy_sample;
	dst = &rb->buf[0];
	for (int i = 0; i < cpy_sample; i++) {
		for (int j = 0; j < rb->ch_valid; j++) {
			*dst = *buf;
			dst++;
			buf++;
		}
		buf += rb->ch_skip;
	}
	rb->prod_idx = len;

	return 0;
}

static int ringbuf_get(struct ringbuf *rb, sample_t *buf, size_t sample_num)
{
	size_t len = sample_num * rb->ch_valid;
	size_t rem = rb->size - rb->cons_idx;

	if (rb->total < MAX(len, RINGBUF_THR)) {
		LOG_WRN("rb %p get no mem", rb);
		return -ENOMEM;
	}
	LOG_INF("rb %p get samples: %d (%d) (total:%d)", rb, sample_num, len, rb->total);
	rb->total -= len;

	if (len <= rem) {
		memcpy(buf, &rb->buf[rb->cons_idx], len * sizeof(sample_t));
		rb->cons_idx += len;
		if (rb->cons_idx == rb->size) {
			rb->cons_idx = 0;
		}
		return 0;
	}

	len -= rem;
	memcpy(buf, &rb->buf[rb->cons_idx], rem * sizeof(sample_t));
	memcpy(&buf[rem], &rb->buf[0], len * sizeof(sample_t));
	rb->cons_idx = len;

	return 0;
}

static void tdm_init(void)
{
	// TODO: delay access to USB core and TDM registers based on signal from APP
	k_msleep(100);

	nrf_gpio_cfg(NRF_GPIO_PIN_MAP(1,3), NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
		     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
	nrf_gpio_pin_clock_set(NRF_GPIO_PIN_MAP(1,3), true);
	nrf_gpio_cfg(NRF_GPIO_PIN_MAP(1,6), NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
		     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
	nrf_gpio_cfg(NRF_GPIO_PIN_MAP(1,4), NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
		     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
	nrf_gpio_cfg(NRF_GPIO_PIN_MAP(1,5), NRF_GPIO_PIN_DIR_INPUT, NRF_GPIO_PIN_INPUT_CONNECT,
		     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
	nrf_tdm_pins_set(NRF_TDM130, &m_pins);
}

static void context_init(void)
{
	int err;

	err = ringbuf_init(&context.to_usb, iso_in_buf, ARRAY_SIZE(iso_in_buf), ISO_IN_CH_CNT, TDM_CH_CNT - TDM_RX_CH_CNT);
	if (err < 0) {
		LOG_ERR("Wrong ring buffer configuration.");
	}

	err = ringbuf_init(&context.from_usb, iso_out_buf, ARRAY_SIZE(iso_out_buf),
			   ISO_OUT_CH_CNT, TDM_CH_CNT - TDM_TX_CH_CNT);
	if (err < 0) {
		LOG_ERR("Wrong ring buffer configuration.");
	}

	/* Initialize context with buffers from RAM3x space. */
	context.tdm_tx.buf[0] = tdm_tx_buffers[0];
	context.tdm_tx.buf[1] = tdm_tx_buffers[1];
	context.tdm_rx.buf[0] = tdm_rx_buffers[0];
	context.tdm_rx.buf[1] = tdm_rx_buffers[1];
}

static uint32_t tdm_get_len(struct tdm_data *data, struct tdm_data *propagate, tdm_dir_t dir)
{
	uint32_t len;
	uint32_t num_of_channels;

	if (dir == tdm_rx) {
		num_of_channels = TDM_RX_CH_CNT;
	} else if (dir == tdm_tx) {
		num_of_channels = TDM_TX_CH_CNT;
	} else {
		LOG_ERR("invalid dir value");
		return 0;
	}

	if (data->len_offset == 0) {
		return (SAMPLES_NUM * sizeof(sample_t) * num_of_channels) / sizeof(uint32_t);
	}

	len = SAMPLES_NUM + data->len_offset;
	if (propagate) {
		propagate->len_offset = data->len_offset;
	}
	data->len_offset = 0;

	return (len * sizeof(sample_t) * num_of_channels) / sizeof(uint32_t);
}

static void tdm_set_rx_ptr(bool first)
{
	sample_t *buf = context.tdm_rx.buf[context.tdm_rx.idx];
	int ret;

	if (!first) {
		ret = ringbuf_put(&context.to_usb, buf, context.tdm_rx.len[context.tdm_rx.idx] / ISO_IN_CH_CNT);
		if (ret < 0) {
			LOG_WRN("No room in ring buffer for TDM data.");
		}
	}

	context.tdm_rx.len[context.tdm_rx.idx] = tdm_get_len(&context.tdm_rx, NULL, tdm_rx);
	nrf_tdm_rx_count_set(NRF_TDM130, context.tdm_rx.len[context.tdm_rx.idx]);
	nrf_tdm_rx_buffer_set(NRF_TDM130, (uint32_t *)buf);

	context.tdm_rx.idx = (context.tdm_rx.idx + 1) & 0x1;
}

static void tdm_set_tx_ptr(void)
{
	sample_t *tx_buf = context.tdm_tx.buf[context.tdm_tx.idx];
	uint32_t len;
	int ret;

	/* Length correction propagates from TDM TX to TDM RX. */
	len = tdm_get_len(&context.tdm_tx, &context.tdm_rx, tdm_tx);
	ret = ringbuf_get(&context.from_usb, tx_buf, len / sizeof(sample_t));
	if (ret < 0) {
		//memset(tx_buf, 0, len * sizeof(uint32_t));
		//LOG_ERR("No TDM data to send.");
	}

	context.tdm_tx.idx = (context.tdm_tx.idx + 1) & 0x1;
	nrf_tdm_tx_count_set(NRF_TDM130, len);
	nrf_tdm_tx_buffer_set(NRF_TDM130, (uint32_t *)tx_buf);
}

static void tdm_start(void)
{
	bool sck_bypass = (SCK_DIV_VALUE > 0x80000000) ? (true) : (false);

	nrf_tdm_enable(NRF_TDM130);
	nrf_tdm_configure(NRF_TDM130, &m_cfg);
	nrf_tdm_sck_configure(NRF_TDM130, NRF_TDM_SRC_ACLK, sck_bypass);
	nrf_tdm_mck_configure(NRF_TDM130, NRF_TDM_SRC_ACLK, false);
	nrf_tdm_event_clear(NRF_TDM130, NRF_TDM_EVENT_RXPTRUPD);
	nrf_tdm_event_clear(NRF_TDM130, NRF_TDM_EVENT_TXPTRUPD);
	nrf_tdm_event_clear(NRF_TDM130, NRF_TDM_EVENT_STOPPED);
	tdm_set_rx_ptr(true);
	tdm_set_tx_ptr();
	nrf_tdm_transfer_direction_set(NRF_TDM130, NRF_TDM_RXTXEN_DUPLEX);
	nrf_barrier_w();
	nrf_tdm_task_trigger(NRF_TDM130, NRF_TDM_TASK_START);
}

static void tdm_disable(void)
{
	nrf_tdm_task_trigger(NRF_TDM130, NRF_TDM_TASK_STOP);
	// TODO: wait for stopped and only then call tdm_start()
	m_tdm_started = false;
	m_tdm_counter = 0;
}

static bool tdm_data_ready(void)
{
	return m_tdm_counter >= 2;
}

static bool tdm_needs_restart(void)
{
	// TODO
	return false;
}

/* === USB === */

static struct usb_dwc2_reg * const dwc2 = (struct usb_dwc2_reg *)NRF_USBHSCORE0;

const uint32_t diepmsk = USB_DWC2_DIEPINT_INEPNAKEFF | USB_DWC2_DIEPINT_EPDISBLD |
	USB_DWC2_DIEPINT_XFERCOMPL;
const uint32_t doepmsk = USB_DWC2_DOEPINT_EPDISBLD | USB_DWC2_DOEPINT_XFERCOMPL;

static uint32_t sof_prev;

static bool m_iso_in_act;
static bool m_iso_out_act;

static bool m_iso_in_queued, m_iso_out_queued;

static buf_t queued_in_buf;
static buf_t queued_out_buf;

#define ISO_IN_EP 1
#define ISO_OUT_EP 1

static int num_iso_out_queued, num_iso_in_queued, num_iso_out_received, num_iso_in_sent;
static bool iso_processing_enabled;

static uint32_t get_next_sample_num(void)
{
	int offset = feedback_samples_offset(mp_fbck);

	m_tdm_plus_ones <<= 1;
	m_tdm_minus_ones <<= 1;

	if ((offset < 0) && (POPCOUNT(m_tdm_plus_ones) < -offset)) {
		m_tdm_plus_ones |= 1;
		LOG_ERR("rx+1");
		//DBG_PIN_SET(1);
		//DBG_PIN_CLR(1);
		return SAMPLES_NUM + 1;
	} else if ((offset > 0) && (POPCOUNT(m_tdm_minus_ones) < offset)) {
		LOG_ERR("rx-1");
		m_tdm_minus_ones |= 1;
		//DBG_PIN_SET(1);
		//DBG_PIN_CLR(1);
		return SAMPLES_NUM - 1;
	}
	return SAMPLES_NUM;
}

static void get_next_iso_in_data(buf_t *p_buf)
{
	/*  should fill the ptr and siz emembers. After get_next_iso_in_data() finishes, the
	 *  underlying buffer pointed to by ptr is owned by USB and TDM or FLPR must not
	 *  access it. The buffer must contain nominal-1, nominal or nominal+1 samples
	 *  depending on feedback. The number of samples in this buffer will affect how
	 *  many samples host sends on OUT endpoint later. The samples must be “carried over”
	 *  or “borrowed from” adjacent (in time domain) TDM buffer.
	 */
	int ret;

	ret = k_mem_slab_alloc(&iso_in_slab, &p_buf->ptr, K_NO_WAIT);
	if (ret < 0) {
		LOG_WRN("No buffer to alloc");
		p_buf->sample_num = 0;
		return;
	}

	p_buf->sample_num = get_next_sample_num();
	ret = ringbuf_get(&context.to_usb, p_buf->ptr, p_buf->sample_num);
	if (ret < 0) {
		LOG_WRN("No data, sending empty");
	}
}

static void release_iso_in_data(buf_t * p_buf)
{
	/* should ideally only take void *ptr parameter which is the value that was
	 * previously provided by get_next_iso_in_data(buf_t * p_buf. When USB handling
	 * code calls this function, the buffer is owned by FLPR (USB no longer accesses it).
	 */
	k_mem_slab_free(&iso_in_slab, p_buf->ptr);
}

static void iso_out_data_received(buf_t * p_buf)
{
	/* is called when USB transfer on ISO OUT endpoint is complete (or if there was no ISO
	 * OUT packet and SOF arrived). When this function is called, the buffer is owned by FLPR.
	 */
	static const sample_t dummy_buf[ISO_OUT_CH_CNT * (SAMPLES_NUM + 1)];
	size_t sample_num = p_buf->sample_num ? p_buf->sample_num : SAMPLES_NUM;
	sample_t *ptr = p_buf->sample_num ? p_buf->ptr : (uint32_t *)dummy_buf;
	int size_diff = sample_num - SAMPLES_NUM;
	int ret;

	if (size_diff != 0) {
		context.tdm_tx.len_offset = size_diff;
		LOG_ERR("len_offset %d", context.tdm_tx.len_offset);
	}

	ret = ringbuf_put(&context.from_usb, ptr, sample_num);
	if (ret < 0) {
		LOG_WRN("no room for data");
	}

	k_mem_slab_free(&iso_out_slab, p_buf->ptr);
}

static void get_recv_buffer_for_iso_out(buf_t * p_buf)
{
	/* provides USB a buffer where ISO OUT data should be stored. The buffer must
	 * be available before SOF and can have the data written to after SOF. The buffer
	 * is owned by USB until iso_out_data_received(buf_t *p_buf)is called.
	 */
	int ret;

	ret = k_mem_slab_alloc(&iso_out_slab, &p_buf->ptr, K_NO_WAIT);
	if (ret < 0) {
		LOG_WRN("No buffer to alloc");
		p_buf->sample_num = 0;
		return;
	}
	p_buf->sample_num = SAMPLES_NUM + 1;
}

static void usb_process_buffers(void)
{
	bool iso_in_act = !!(dwc2->in_ep[ISO_IN_EP].diepctl & USB_DWC2_DEPCTL_USBACTEP);
	bool iso_out_act = !!(dwc2->out_ep[ISO_OUT_EP].doepctl & USB_DWC2_DEPCTL_USBACTEP);

	if (m_iso_in_act != iso_in_act) {
		m_iso_in_act = iso_in_act;
		if (m_iso_in_act) {
			LOG_ERR("IN EP activated");
		} else {
			LOG_ERR("IN EP deactivated, disable TDM");
			if (m_iso_in_queued) {
				LOG_ERR("releasing IN buffer");
				release_iso_in_data(&queued_in_buf);
				m_iso_in_queued = false;
			}
			tdm_disable();
			buffers_flush();
		}
	}

	if (m_iso_out_act != iso_out_act) {
		m_iso_out_act = iso_out_act;
		if (m_iso_out_act) {
			LOG_ERR("OUT EP activated");
		} else {
			if (m_iso_out_queued) {
				LOG_ERR("OUT EP deactivated, dropping queue");
				queued_out_buf.sample_num = 0;
				iso_out_data_received(&queued_out_buf);
				m_iso_out_queued = false;
			} else {
				LOG_ERR("OUT EP deactivated");
			}
		}
	}
}

static void usb_process_in(void)
{
	if (m_iso_in_queued) {
		uint32_t diepint = dwc2->in_ep[ISO_IN_EP].diepint;
		uint32_t status = diepint & diepmsk;

		if (status) {
			dwc2->in_ep[ISO_IN_EP].diepint = status;
		}

		if (status & USB_DWC2_DIEPINT_XFERCOMPL) {
			release_iso_in_data(&queued_in_buf);
			m_iso_in_queued = false;

			num_iso_in_sent++;
		}
	}

	if (!m_iso_in_queued) {
		uint32_t addr, len;
		uint32_t diepctl;

		get_next_iso_in_data(&queued_in_buf);
		m_iso_in_queued = true;

		addr = (uint32_t)queued_in_buf.ptr;
		len = queued_in_buf.sample_num * ISO_IN_CH_CNT * sizeof(sample_t);

		dwc2->in_ep[ISO_IN_EP].dieptsiz =
			usb_dwc2_set_dieptsizn_mc(1) |
			usb_dwc2_set_dieptsizn_pktcnt(1) |
			usb_dwc2_set_dieptsizn_xfersize(len);
		dwc2->in_ep[ISO_IN_EP].diepdma = addr;

		nrf_barrier_r();
		diepctl = dwc2->in_ep[ISO_IN_EP].diepctl;
		if (!(diepctl & USB_DWC2_DEPCTL_USBACTEP)) {
			LOG_ERR("IN queueing but ep not active");
			/* TODO: Synchronize endpoint disable with app core */
			release_iso_in_data(&queued_in_buf);
			m_iso_in_queued = false;
			return;
		}

		diepctl |= USB_DWC2_DEPCTL_EPENA | USB_DWC2_DEPCTL_CNAK;

		nrf_barrier_w();
		dwc2->in_ep[ISO_IN_EP].diepctl = diepctl;

		num_iso_in_queued++;
	}
}

static void usb_process_out(void)
{
	if (m_iso_out_queued) {
		uint32_t doepint = dwc2->out_ep[ISO_OUT_EP].doepint;
		uint32_t status = doepint & doepmsk;

		if (status) {
			dwc2->out_ep[ISO_OUT_EP].doepint = status;
		}

		if (status & USB_DWC2_DOEPINT_XFERCOMPL) {
			uint32_t orig_len = queued_out_buf.sample_num * ISO_OUT_CH_CNT * sizeof(sample_t);
			uint32_t doeptsiz = dwc2->out_ep[ISO_OUT_EP].doeptsiz;
			uint32_t bcnt;

			bcnt = usb_dwc2_get_doeptsizn_xfersize(orig_len) -
			       usb_dwc2_get_doeptsizn_xfersize(doeptsiz);

			if (usb_dwc2_get_doeptsizn_pktcnt(doeptsiz) != 0 ||
			    usb_dwc2_get_doeptsizn_rxdpid(doeptsiz) != USB_DWC2_DOEPTSIZN_RXDPID_DATA0) {
				LOG_ERR("invalid data pid or pktcnt 0x%08x", doeptsiz);
				/* bcnt = 0; */
			}

			queued_out_buf.sample_num = bcnt / (ISO_OUT_CH_CNT * sizeof(sample_t));
			if (queued_out_buf.sample_num != SAMPLES_NUM)
				LOG_ERR("RX %d samples %d bytes", queued_out_buf.sample_num, bcnt);
			iso_out_data_received(&queued_out_buf);

			m_tdm_counter++;

			if (bcnt % (ISO_OUT_CH_CNT * sizeof(sample_t))) {
				LOG_ERR("Received invalid number of bytes %d", bcnt);
			} else if (queued_out_buf.sample_num != SAMPLES_NUM) {
				LOG_ERR("RX %d samples", queued_out_buf.sample_num);
			}

			m_iso_out_queued = false;

			num_iso_out_received++;
		}
	}

	if (!m_iso_out_queued) {
		uint32_t addr, len;
		uint32_t doepctl;

		get_recv_buffer_for_iso_out(&queued_out_buf);
		m_iso_out_queued = true;

		addr = (uint32_t)queued_out_buf.ptr;
		len = queued_out_buf.sample_num * ISO_OUT_CH_CNT * sizeof(sample_t);

		dwc2->out_ep[ISO_OUT_EP].doeptsiz =
			usb_dwc2_set_doeptsizn_pktcnt(1) |
			usb_dwc2_set_doeptsizn_xfersize(len);
		dwc2->out_ep[ISO_OUT_EP].doepdma = addr;

		nrf_barrier_r();
		doepctl = dwc2->out_ep[ISO_OUT_EP].doepctl;
		if (!(doepctl & USB_DWC2_DEPCTL_USBACTEP)) {
			/* TODO: Synchronize endpoint disable with app core */
			queued_out_buf.sample_num = 0;
			iso_out_data_received(&queued_out_buf);
			m_tdm_counter++;
			m_iso_out_queued = false;
			return;
		}

		doepctl |= USB_DWC2_DEPCTL_EPENA | USB_DWC2_DEPCTL_CNAK;

		nrf_barrier_w();
		dwc2->out_ep[ISO_OUT_EP].doepctl = doepctl;

		num_iso_out_queued++;
	}
}

static int decimator = HIGH_SPEED_SOF_PERIODS;

/**
 * @brief Detect SOF boundary.
 *
 * @retval true SOF boundary detected.
 * @retval false Not detected.
 */
static bool usb_sof_changed(void)
{
#if 1
	static struct usb_dwc2_reg * const regs = (struct usb_dwc2_reg *)NRF_USBHSCORE0;
	volatile uint32_t sof_curr;
	uint32_t diff;
	int rpt = 3;
	bool ret = true;

	do {
		nrf_barrier_r();
		sof_curr = usb_dwc2_get_dsts_soffn(regs->dsts);
		if (sof_prev == sof_curr) {
			return false;
		}
		diff = (sof_curr - sof_prev) & 0x3FFF;
		if (diff == 1) {
			break;
		} else {
			LOG_DBG("sof re-read");
		}
		rpt--;
	} while (rpt > 0);

	if (diff > 1) {
		ret = false;
		DBG_PIN_SET(1);
		DBG_PIN_SET(2);
		LOG_WRN("sof re-read failed prev:%d curr:%d", sof_prev, sof_curr);
		DBG_PIN_CLR(1);
		DBG_PIN_CLR(2);
	}

	sof_prev = sof_curr;

	decimator--;
	if (decimator > 0) {
		return false;
	}
	decimator = HIGH_SPEED_SOF_PERIODS;

#else
	bool ret = true;

	/* TODO: Figure out why events are missing */
	nrf_barrier_r();
	if (NRF_TIMER131->EVENTS_COMPARE[0]) {
		nrf_barrier_w();
		NRF_TIMER131->EVENTS_COMPARE[0] = 0;
		nrf_barrier_rw();
	} else {
		return false;
	}
#endif

	if (iso_processing_enabled) {

		if (!num_iso_in_queued || !num_iso_out_queued ||
		    !num_iso_in_sent || !num_iso_out_received) {
			LOG_ERR("%d Q %d %d Xfer %d %d", sof_curr,
				num_iso_in_queued, num_iso_out_queued,
				num_iso_in_sent, num_iso_out_received);

			DBG_PIN_SET(1);
			DBG_PIN_CLR(1);
		}
	}

	num_iso_in_queued = 0;
	num_iso_out_queued = 0;
	num_iso_in_sent = 0;
	num_iso_out_received = 0;

	return ret;
}

/* === */

int main(void)
{
	tdm_init();
	mp_fbck = feedback_init();
	int rpt = 10;

	DBG_PIN_INIT(0);
	DBG_PIN_INIT(1);
	DBG_PIN_INIT(2);
	DBG_PIN_INIT(3);

	LOG_DBG("FLPR started");

	//LOG_ERR("sample_width = %u channels = %x num_of_channels = %u", m_cfg.sample_width, m_cfg.channels, m_cfg.num_of_channels);

	context_init();

	uint32_t iso_in_delay = 0;

	while (1 || rpt) {
		nrf_barrier_rw();

		if (m_iso_in_act) {
			if (iso_in_delay == 8000 * 5) {
				iso_processing_enabled = true;
				usb_process_in();
			} else {
				iso_in_delay++;
			}
		} else {
			iso_in_delay = 0;
			iso_processing_enabled = false;
		}

		if (m_iso_out_act) {
			if (iso_in_delay == 8000 * 5) {
				usb_process_out();
			}
		}

		if (usb_sof_changed())
		{
			rpt--;
			DBG_PIN_SET(0);
			LOG_INF("sof pending tdm rx:%d", context.to_usb.total);
			feedback_process(mp_fbck);
			usb_process_buffers();
			DBG_PIN_CLR(0);
		}

		if (nrf_tdm_event_check(NRF_TDM130, NRF_TDM_EVENT_TXPTRUPD))
		{
			DBG_PIN_SET(2);
			nrf_tdm_event_clear(NRF_TDM130, NRF_TDM_EVENT_TXPTRUPD);
			tdm_set_tx_ptr();
			DBG_PIN_CLR(2);
		}

		if (nrf_tdm_event_check(NRF_TDM130, NRF_TDM_EVENT_RXPTRUPD))
		{
			static bool once;

			DBG_PIN_SET(3);
			nrf_tdm_event_clear(NRF_TDM130, NRF_TDM_EVENT_RXPTRUPD);
			tdm_set_rx_ptr(!once);
			once = true;
			DBG_PIN_CLR(3);
		}

		if (tdm_needs_restart())
		{
			LOG_INF("start TDM");
			tdm_disable();
			buffers_flush();
		}

		if (!m_tdm_started && tdm_data_ready())
		{
			LOG_INF("start TDM");

			DBG_PIN_SET(3);
			/*tdm_start(&m_iso_in_buffers[0], &m_iso_out_buffers[0]);*/
			tdm_start();
			decimator = HIGH_SPEED_SOF_PERIODS - 2;
			feedback_start(mp_fbck, m_tdm_counter, true);
			m_tdm_started = true;
			DBG_PIN_CLR(3);
		}
	}

	return 0;
}

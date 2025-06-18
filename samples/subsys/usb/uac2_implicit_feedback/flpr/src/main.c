#include <hal/nrf_gpio.h>
#include <hal/nrf_tdm.h>
#include <zephyr/kernel.h>
#include <../drivers/usb/common/usb_dwc2_hw.h>
#include "../../src/feedback.h"

/* === BUFFERS === */
#define SAMPLES_NUM    6
#define CHANNELS_NUM   2
#define BUFFERS_NUM    3
#define NEXT_BUFFER(x) ((x + 1) % BUFFERS_NUM)

static struct feedback_ctx * mp_fbck;

typedef struct {
	void * ptr;
	size_t size;
} buf_t;

static buf_t m_iso_in_buffers[BUFFERS_NUM];
static buf_t m_iso_out_buffers[BUFFERS_NUM];
static uint8_t m_iso_in_idx;
static uint8_t m_iso_out_idx;

//TODO MEMORY REGION VERIFY!
static uint32_t m_fake_sample_tx[SAMPLES_NUM + 1] __aligned(4);
static uint32_t m_fake_sample_rx[SAMPLES_NUM + 1] __aligned(4);

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
#define WORD_SIZE      16UL
#define FRAME_CLK_FREQ 48000UL
#define SCK_FREQ       WORD_SIZE * FRAME_CLK_FREQ * CHANNELS_NUM
#define SCK_VALUE      ((uint32_t)(((uint64_t)SCK_FREQ * MCKCONST) / (AUDIOPLL_FREQ + (SCK_FREQ / 2) )* 4096))

static const nrf_tdm_config_t m_cfg = {
	.mode = NRF_TDM_MODE_MASTER,
	.alignment = NRF_TDM_ALIGN_LEFT,
	.sample_width = NRF_TDM_SWIDTH_16BIT,
	.channels = NRF_TDM_CHANNEL_TX0_MASK | NRF_TDM_CHANNEL_TX1_MASK | NRF_TDM_CHANNEL_RX0_MASK | NRF_TDM_CHANNEL_RX1_MASK,
	.num_of_channels = NRF_TDM_CHANNELS_COUNT_2,
	.channel_delay = NRF_TDM_CHANNEL_DELAY_1CK,
	.mck_setup = 0,
	.sck_setup = SCK_VALUE,
	.sck_polarity = NRF_TDM_POLARITY_POSEDGE,
	.fsync_polarity = NRF_TDM_POLARITY_NEGEDGE,
	.fsync_duration = NRF_TDM_FSYNC_DURATION_CHANNEL,
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
static uint8_t m_tdm_plus_ones;
static uint8_t m_tdm_minus_ones;

static void tdm_init(void)
{
	// TODO: delay access to USB core and TDM registers based on signal from APP
	k_msleep(100);

	nrf_gpio_cfg(NRF_GPIO_PIN_MAP(1,3), NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
		     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
	nrf_gpio_cfg(NRF_GPIO_PIN_MAP(1,6), NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
		     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
	nrf_gpio_cfg(NRF_GPIO_PIN_MAP(1,4), NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
		     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
	nrf_gpio_cfg(NRF_GPIO_PIN_MAP(1,5), NRF_GPIO_PIN_DIR_INPUT, NRF_GPIO_PIN_INPUT_CONNECT,
		     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
	nrf_tdm_pins_set(NRF_TDM130, &m_pins);
}

static void tdm_start(buf_t * p_in, buf_t * p_out)
{
	nrf_tdm_enable(NRF_TDM130);
	nrf_tdm_configure(NRF_TDM130, &m_cfg);
	nrf_tdm_sck_configure(NRF_TDM130, NRF_TDM_SRC_ACLK, false);
	nrf_tdm_mck_configure(NRF_TDM130, NRF_TDM_SRC_ACLK, false);
	nrf_tdm_event_clear(NRF_TDM130, NRF_TDM_EVENT_RXPTRUPD);
	nrf_tdm_event_clear(NRF_TDM130, NRF_TDM_EVENT_TXPTRUPD);
	nrf_tdm_event_clear(NRF_TDM130, NRF_TDM_EVENT_STOPPED);
	nrf_tdm_tx_count_set(NRF_TDM130, SAMPLES_NUM);
	nrf_tdm_rx_count_set(NRF_TDM130, SAMPLES_NUM);
	nrf_tdm_tx_buffer_set(NRF_TDM130, (const uint32_t *)p_in->ptr);
	nrf_tdm_rx_buffer_set(NRF_TDM130, (uint32_t *)p_out->ptr);
	nrf_tdm_transfer_direction_set(NRF_TDM130, NRF_TDM_RXTXEN_DUPLEX);
	nrf_tdm_task_trigger(NRF_TDM130, NRF_TDM_TASK_START);
}

static void tdm_disable(void)
{
	nrf_tdm_task_trigger(NRF_TDM130, NRF_TDM_TASK_STOP);
	// TODO: wait for stopped and only then call tdm_start()
	m_tdm_started = false;
	m_tdm_counter = 0;
}

// TODO TBD how should size from usb be handled?

static void tdm_set_new_rx_ptr(buf_t * p_buf)
{
	int offset = feedback_samples_offset(mp_fbck);

	m_tdm_plus_ones <<= 1;
	m_tdm_minus_ones <<= 1;

	if ((offset < 0) && (POPCOUNT(m_tdm_plus_ones) < -offset)) {
		m_tdm_plus_ones |= 1;
		p_buf->size = SAMPLES_NUM + 1;
	} else if ((offset > 0) && (POPCOUNT(m_tdm_minus_ones) < offset)) {
		m_tdm_minus_ones |= 1;
		p_buf->size = SAMPLES_NUM - 1;
	} else {
		p_buf->size = SAMPLES_NUM;
	}

	// TODO case when pending_mic_samples >= samples_to_send ?
	nrf_tdm_tx_count_set(NRF_TDM130, p_buf->size);
	nrf_tdm_rx_buffer_set(NRF_TDM130, (uint32_t *)p_buf->ptr);
}

static void tdm_set_new_tx_ptr(buf_t * p_buf)
{
	if (p_buf->size == 0)
	{
		memset(p_buf->ptr, 0, SAMPLES_NUM + 1);
	}

	if (m_tdm_plus_ones & 1) {
		p_buf->size = SAMPLES_NUM + 1;
	} else if (m_tdm_minus_ones & 1) {
		p_buf->size = SAMPLES_NUM - 1;
	} else {
		p_buf->size = SAMPLES_NUM;
	};

	nrf_tdm_tx_count_set(NRF_TDM130, p_buf->size);
	nrf_tdm_tx_buffer_set(NRF_TDM130, (const uint32_t *)p_buf->ptr);
	p_buf->ptr = NULL;
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

static struct usb_dwc2_reg * const regs = (struct usb_dwc2_reg *)NRF_USBHSCORE0;

static bool usb_sof_changed(void)
{
	static uint32_t sof_prev;
	volatile uint32_t sof_curr = usb_dwc2_get_dsts_soffn(regs->dsts);

	if (sof_prev != sof_curr)
	{
		sof_prev = sof_curr;
		return true;
	}

	return false;
}

static void get_next_iso_in_data(buf_t * p_buf)
{
	// TODO @tmon
	p_buf->ptr = m_fake_sample_rx;
}

static void release_iso_in_data(buf_t * p_buf)
{
	// TODO @tmon
	(void)p_buf;
}

// TODO TBD - what is responsibility of this func vs new_tx_ptr()?
static void iso_out_data_received(buf_t * p_buf)
{
}

static void get_recv_buffer_for_iso_out(buf_t * p_buf)
{
	// TODO @tmon
	p_buf->ptr = m_fake_sample_tx;
	p_buf->size = 0;
}

static void usb_process_buffers(void)
{
	m_iso_in_idx = NEXT_BUFFER(m_iso_in_idx);
	get_next_iso_in_data(&m_iso_in_buffers[m_iso_in_idx]);

	uint8_t next_in_idx = NEXT_BUFFER(m_iso_in_idx);
	if (m_iso_in_buffers[next_in_idx].ptr) {
		release_iso_in_data(&m_iso_in_buffers[next_in_idx]);
		m_iso_in_buffers[next_in_idx].ptr = NULL;
	}

	m_iso_out_idx = NEXT_BUFFER(m_iso_out_idx);
	get_recv_buffer_for_iso_out(&m_iso_out_buffers[m_iso_out_idx]);

	uint8_t next_out_idx = NEXT_BUFFER(m_iso_out_idx);
	if (m_iso_out_buffers[next_out_idx].ptr) {
		iso_out_data_received(&m_iso_out_buffers[next_out_idx]);
		m_iso_out_buffers[next_out_idx].ptr = NULL;
	}

	m_tdm_counter++;
}

/* === */

int main(void)
{
	tdm_init();
	mp_fbck = feedback_init();

	while (1) {
		if (usb_sof_changed())
		{
			feedback_process(mp_fbck);
			usb_process_buffers();
		}

		if (nrf_tdm_event_check(NRF_TDM130, NRF_TDM_EVENT_TXPTRUPD))
		{
			tdm_set_new_tx_ptr(&m_iso_out_buffers[m_iso_out_idx]);
			nrf_tdm_event_clear(NRF_TDM130, NRF_TDM_EVENT_TXPTRUPD);
		}

		if (nrf_tdm_event_check(NRF_TDM130, NRF_TDM_EVENT_RXPTRUPD))
		{
			tdm_set_new_rx_ptr(&m_iso_in_buffers[m_iso_in_idx]);
			nrf_tdm_event_clear(NRF_TDM130, NRF_TDM_EVENT_RXPTRUPD);
		}

		if (tdm_needs_restart())
		{
			tdm_disable();
			buffers_flush();
		}

		if (!m_tdm_started && tdm_data_ready())
		{
			tdm_start(&m_iso_in_buffers[0], &m_iso_out_buffers[0]);
			feedback_start(mp_fbck, m_tdm_counter, true);
			m_tdm_started = true;
		}
	}

	return 0;
}

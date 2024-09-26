/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nrfx_saadc.h>
#include <helpers/nrfx_gppi.h>
#include <nrfx_timer.h>
#include <nrfx_gpiote.h>
#include <hal/nrf_gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util_macro.h>
#include <dmm.h>
LOG_MODULE_REGISTER(app);

/**
 * @defgroup nrfx_saadc_maximum_performance_example Maximum performance SAADC example
 * @{
 * @ingroup nrfx_saadc_examples
 *
 * @brief Example showing advanced functionality of nrfx_saadc driver operating at its peak
 *performance.
 *
 * @details Application initializes nrfx_saadc driver and starts operating in the non-blocking mode.
 *Sampling is performed at the highest supported frequency. In the example, @ref m_single_channel is
 *configured and the SAADC driver is set to the advanced mode. To achieve maximum performance:
 *			  - Performing sampling at @ref MAX_SAADC_SAMPLE_FREQUENCY requires an
 *external timer. It is done by setting up endpoints of the channel @ref m_gppi_channels [ @p
 *gppi_channels_purpose_t::SAADC_SAMPLING ] to trigger SAADC sample task ( @p
 *nrf_saadc_task_t::NRF_SAADC_TASK_SAMPLE ) on the timer compare event.
 *			  - Hardware start-on-end must be provided. It is done by setting up
 *endpoints of the channel
 *				@ref m_gppi_channels [ @p gppi_channels_purpose_t::SAADC_START_ON_END ]
 *to trigger SAADC task start ( @p nrf_saadc_task_t::NRF_SAADC_TASK_START ) on the SAADC event end (
 *@p nrf_saadc_event_t::NRF_SAADC_EVENT_END ).
 *
 *		  Calibration in a non-blocking manner is triggered by @p
 *nrfx_saadc_offset_calibrate. Then at @p NRFX_SAADC_EVT_CALIBRATEDONE event in @ref saadc_handler()
 *sampling is initiated by calling @p nrfx_saadc_mode_trigger() function. Consecutive sample tasks
 *are triggered by the external timer at the sample rate specified in  @ref SAADC_SAMPLE_FREQUENCY
 *symbol.
 *
 *		  In the example there is GPPI channel configured to test the functionality of
 *SAADC. The endpoints are setup up in a way that @p NRF_SAADC_EVENT_RESULTDONE event is connected
 *with the GPIOTE task that toggles the @ref OUT_GPIO_PIN pin.
 */

/**
 * @brief SAADC channel configuration for the single-ended mode with 3 us sample acquisition time.
 *		The 3 us acquisition time will work correctly when the source resistance of @p
 *_pin_p input analog pin is less than 10 kOhm.
 *
 * This configuration sets up single-ended SAADC channel with the following options:
 * - resistor ladder disabled
 * - gain: 1
 * - reference voltage: internal
 * - sample acquisition time: 3 us
 * - burst disabled
 *
 * @param[in] _pin_p Positive input analog pin.
 * @param[in] _index Channel index.
 *
 * @sa nrfx_saadc_channel_t
 */
#define SAADC_CHANNEL_SE_ACQ_3US(_pin_p, _index)                         \
{                                                                        \
	.channel_config = {                                              \
		IF_ENABLED(NRF_SAADC_HAS_CH_CONFIG_RES,                  \
			(.resistor_p = NRF_SAADC_RESISTOR_DISABLED,))    \
		IF_ENABLED(NRF_SAADC_HAS_CH_CONFIG_RES,                  \
			(.resistor_n = NRF_SAADC_RESISTOR_DISABLED,))    \
		.gain = GAIN,                                            \
		.reference = NRF_SAADC_REFERENCE_INTERNAL,               \
		.acq_time = ACQTIME,                                     \
		.mode = NRF_SAADC_MODE_SINGLE_ENDED,                     \
		.burst = NRF_SAADC_BURST_DISABLED,                       \
		IF_ENABLED(NRF_SAADC_HAS_CONVTIME, (.conv_time = (CONV_TIME * 4 - 1),)) }, \
		.pin_p = (nrf_saadc_input_t)_pin_p,                      \
		.pin_n = NRF_SAADC_INPUT_DISABLED,                       \
		.channel_index = _index,                                 \
	}

#if defined(CONFIG_SOC_NRF54H20)

#define OUT_GPIO_PIN NRF_GPIO_PIN_MAP(9, 0)
#define OUT_GPIO_PIN2 NRF_GPIO_PIN_MAP(9, 1)
#define ANALOG_INPUT_A0 NRF_GPIO_PIN_MAP(1, 0)
#define ANALOG_INPUT_TO_SAADC_AIN(x) (x)
#define ACQTIME 23
#define GAIN NRF_SAADC_GAIN1

/** @brief Symbol specifying timer instance to be used. */
#define TIMER_INST_IDX  131

#else
#error "Not supported"
#endif

/** @brief Symbol specifying analog input to be observed by SAADC channel 0. */
#define CH0_AIN ANALOG_INPUT_TO_SAADC_AIN(ANALOG_INPUT_A0)

/** @brief Symbol specifying GPIO pin used to test the functionality of SAADC. */
#define OUT_GPIO_PIN LOOPBACK_PIN_1B

/** @brief Acquisition time [us] for source resistance <= 10 kOhm (see SAADC electrical
 * specification). */
#define ACQ_TIME_10K 3UL

/** @brief Conversion time [us] (see SAADC electrical specification). */
#define CONV_TIME 2UL

/** @brief Symbol specifying maximal possible SAADC sample rate (see SAADC electrical
 * specification). */
#define MAX_SAADC_SAMPLE_FREQUENCY 200000UL

/** @brief Symbol specifying SAADC sample frequency for the continuous sampling. */
#define SAADC_SAMPLE_FREQUENCY MAX_SAADC_SAMPLE_FREQUENCY

/** @brief Symbol specifying time in microseconds to wait for timer handler execution. */
#define TIME_TO_WAIT_US (uint32_t)(1000000UL / SAADC_SAMPLE_FREQUENCY)

/**
 * @brief Symbol specifying the number of sample buffers ( @ref m_sample_buffers ).
 *		Two buffers are required for performing double-buffered conversions.
 */
#define BUFFER_COUNT 2UL

/** @brief Symbol specifying the size of singular sample buffer ( @ref m_sample_buffers ). */
#define BUFFER_SIZE 8UL

/** @brief Symbol specifying the number of SAADC samplings to trigger. */
#define SAMPLING_ITERATIONS 3UL

/** @brief Symbol specifying the resolution of the SAADC. */
#define RESOLUTION NRF_SAADC_RESOLUTION_10BIT

/** @brief SAADC channel configuration structure for single channel use. */
static const nrfx_saadc_channel_t m_single_channel = SAADC_CHANNEL_SE_ACQ_3US(CH0_AIN, 0);

/** @brief Samples buffer to store values from a SAADC channel. */
/* Use DMM macro to allocate memory in RAM which can be used by SAADC EasyDMA (RAM3). */
static uint16_t m_sample_buffers[BUFFER_COUNT][BUFFER_SIZE] DMM_MEMORY_SECTION(DT_NODELABEL(adc));


/** @brief Array of the GPPI channels. */
static uint8_t m_gppi_channels[2];

/** @brief Enum with intended uses of GPPI channels defined as @ref m_gppi_channels. */
typedef enum {
	SAADC_SAMPLING,     ///< Triggers SAADC sampling task on external timer event.
	SAADC_START_ON_END, ///< Triggers SAADC start task on SAADC end event.
} gppi_channels_purpose_t;

/** Maximum sampling rate of SAADC is 200 [kHz]. */
NRFX_STATIC_ASSERT(SAADC_SAMPLE_FREQUENCY <= (MAX_SAADC_SAMPLE_FREQUENCY));

/** For continuous sampling the sample rate SAADC_SAMPLE_FREQUENCY should fulfill the following
 * criteria (see SAADC Continuous sampling). */
NRFX_STATIC_ASSERT(SAADC_SAMPLE_FREQUENCY <= (1000000UL / (ACQ_TIME_10K + CONV_TIME)));

static int16_t samples[256];
static int scnt;
/**
 * @brief Function for handling SAADC driver events.
 *
 * @param[in] p_event Pointer to an SAADC driver event.
 */
static void saadc_handler(nrfx_saadc_evt_t const *p_event)
{
	nrfx_err_t status;
	(void)status;

	static uint16_t buffer_index = 1;
	static uint16_t buf_req_evt_counter;
	uint16_t samples_number;

	switch (p_event->type) {
	case NRFX_SAADC_EVT_CALIBRATEDONE:
		LOG_INF("SAADC event: CALIBRATEDONE");

		status = nrfx_saadc_mode_trigger();
		NRFX_ASSERT(status == NRFX_SUCCESS);
		break;

	case NRFX_SAADC_EVT_READY:
		LOG_INF("SAADC event: READY");

		nrfx_gppi_channels_enable(NRFX_BIT(m_gppi_channels[SAADC_SAMPLING]));
		break;

	case NRFX_SAADC_EVT_BUF_REQ:
		LOG_INF("SAADC event: BUF_REQ");

		if (++buf_req_evt_counter < SAMPLING_ITERATIONS) {
			/* Next available buffer must be set on the NRFX_SAADC_EVT_BUF_REQ event to
			 * achieve the continuous conversion. */
			status = nrfx_saadc_buffer_set(m_sample_buffers[buffer_index++],
						       BUFFER_SIZE);
			NRFX_ASSERT(status == NRFX_SUCCESS);
			buffer_index = buffer_index % BUFFER_COUNT;
		} else {
			nrfx_gppi_channels_disable(NRFX_BIT(m_gppi_channels[SAADC_START_ON_END]));
		}
		break;

	case NRFX_SAADC_EVT_DONE:
		LOG_INF("SAADC event: DONE");
		LOG_INF("Sample buffer address == %p", p_event->data.done.p_buffer);

		/* Change state of pin that is shortened with analog pin that is sampled
		 * by SAADC.
		 */
		nrf_gpio_pin_set(OUT_GPIO_PIN);

		samples_number = p_event->data.done.size;
		for (uint16_t i = 0; i < samples_number; i++) {
			samples[scnt++] = NRFX_SAADC_SAMPLE_GET(RESOLUTION,
						p_event->data.done.p_buffer, i);
		}
		break;

	case NRFX_SAADC_EVT_FINISHED:
		LOG_INF("FINISHED");

		nrfx_gppi_channels_disable(NRFX_BIT(m_gppi_channels[SAADC_SAMPLING]));
		break;

	default:
		break;
	}
}

/**
 * @brief Function for application main entry.
 *
 * @return Nothing.
 */
int main(void)
{
	nrfx_err_t status;
	(void)status;

	IRQ_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_SAADC), IRQ_PRIO_LOWEST, nrfx_saadc_irq_handler, 0, 0);

	LOG_INF("Starting nrfx_saadc maximum performance example.");

	status = nrfx_saadc_init(NRFX_SAADC_DEFAULT_CONFIG_IRQ_PRIORITY);
	NRFX_ASSERT(status == NRFX_SUCCESS);

	nrfx_timer_t timer_inst = NRFX_TIMER_INSTANCE(TIMER_INST_IDX);
	uint32_t base_frequency = NRF_TIMER_BASE_FREQUENCY_GET(timer_inst.p_reg);
	nrfx_timer_config_t timer_config = NRFX_TIMER_DEFAULT_CONFIG(base_frequency);
	timer_config.bit_width = NRF_TIMER_BIT_WIDTH_32;
	timer_config.p_context = &timer_inst;

	status = nrfx_timer_init(&timer_inst, &timer_config, NULL);
	NRFX_ASSERT(status == NRFX_SUCCESS);

	nrfx_timer_clear(&timer_inst);

	/* Creating variable desired_ticks to store the output of nrfx_timer_us_to_ticks function.
	 */
	uint32_t desired_ticks = nrfx_timer_us_to_ticks(&timer_inst, TIME_TO_WAIT_US);

	/*
	 * Setting the timer channel NRF_TIMER_CC_CHANNEL0 in the extended compare mode to clear
	 * the timer and to not trigger an interrupt if the internal counter register is equal to
	 * desired_ticks.
	 */
	nrfx_timer_extended_compare(&timer_inst, NRF_TIMER_CC_CHANNEL0, desired_ticks,
				    NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, false);

	status = nrfx_saadc_channel_config(&m_single_channel);
	NRFX_ASSERT(status == NRFX_SUCCESS);

	/*
	 * Setting the advanced configuration with triggering sampling by the internal timer
	 * disabled (internal_timer_cc = 0) and without software start task on end event
	 * (start_on_end = false).
	 */
	nrfx_saadc_adv_config_t adv_config = NRFX_SAADC_DEFAULT_ADV_CONFIG;
	adv_config.internal_timer_cc = 0;
	adv_config.start_on_end = false;

	uint32_t channel_mask = nrfx_saadc_channels_configured_get();
	status = nrfx_saadc_advanced_mode_set(channel_mask, RESOLUTION, &adv_config, saadc_handler);
	NRFX_ASSERT(status == NRFX_SUCCESS);

	status = nrfx_saadc_buffer_set(m_sample_buffers[0], BUFFER_SIZE);
	NRFX_ASSERT(status == NRFX_SUCCESS);

	/*
	 * Allocate a dedicated channel and configure endpoints of that channel so that the timer
	 * compare event is connected with the SAADC sample task. This means that each time the
	 * timer interrupt occurs, the SAADC sampling will be triggered.
	 */
	status = nrfx_gppi_channel_alloc(&m_gppi_channels[SAADC_SAMPLING]);
	NRFX_ASSERT(status == NRFX_SUCCESS);

	nrfx_gppi_channel_endpoints_setup(
		m_gppi_channels[SAADC_SAMPLING],
		nrfx_timer_compare_event_address_get(&timer_inst, NRF_TIMER_CC_CHANNEL0),
		nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_SAMPLE));

	/*
	 * Allocate a dedicated channel and configure endpoints of that so that the SAADC event end
	 * is connected with the SAADC task start. This means that each time the SAADC fills up the
	 * result buffer, the SAADC will be restarted and the result buffer will be prepared in RAM.
	 */
	status = nrfx_gppi_channel_alloc(&m_gppi_channels[SAADC_START_ON_END]);
	NRFX_ASSERT(status == NRFX_SUCCESS);

	nrfx_gppi_channel_endpoints_setup(
		m_gppi_channels[SAADC_START_ON_END],
		nrf_saadc_event_address_get(NRF_SAADC, NRF_SAADC_EVENT_END),
		nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_START));

	nrf_gpio_cfg_output(OUT_GPIO_PIN);

	nrfx_timer_enable(&timer_inst);

	nrfx_gppi_channels_enable(NRFX_BIT(m_gppi_channels[SAADC_START_ON_END]));

	status = nrfx_saadc_offset_calibrate(saadc_handler);
	NRFX_ASSERT(status == NRFX_SUCCESS);
	k_msleep(100);

	for (uint32_t i =0; i < scnt;i++) {
		LOG_INF("[Sample %lu.%lu]: %d",i / BUFFER_SIZE, i % BUFFER_SIZE, samples[i]);
	}
}

/** @} */

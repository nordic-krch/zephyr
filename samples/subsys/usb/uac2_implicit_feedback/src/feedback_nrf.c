/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "feedback.h"
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_SOC_NRF54H20_CPUFLPR)
#define DO_INIT 0
#else
#define DO_INIT 1
#endif

#if DO_INIT
#include <stdlib.h>
#include <zephyr/logging/log.h>
#include <nrfx_dppi.h>
#include <nrfx_timer.h>
#include <helpers/nrfx_gppi.h>
LOG_MODULE_REGISTER(feedback, LOG_LEVEL_INF);
#else
#include <hal/nrf_timer.h>
#endif


#define FEEDBACK_TIMER_USBD_SOF_CAPTURE 0
#define FEEDBACK_TIMER_I2S_FRAMESTART_CAPTURE 1

#if IS_ENABLED(CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)

#include <hal/nrf_usbd.h>
#include <hal/nrf_i2s.h>

#define FEEDBACK_TIMER_INSTANCE_NUMBER 2
#define USB_SOF_EVENT_ADDRESS nrf_usbd_event_address_get(NRF_USBD, NRF_USBD_EVENT_SOF)
#define I2S_FRAMESTART_EVENT_ADDRESS nrf_i2s_event_address_get(NRF_I2S0, NRF_I2S_EVENT_FRAMESTART)

static inline void feedback_target_init(void)
{
	/* No target specific init necessary */
}

#elif IS_ENABLED(CONFIG_SOC_SERIES_NRF54HX)

#if DO_INIT
#include <hal/nrf_tdm.h>

#define FEEDBACK_TIMER_INSTANCE_NUMBER 131
#define USB_SOF_EVENT_ADDRESS nrf_timer_event_address_get(NRF_TIMER131, NRF_TIMER_EVENT_COMPARE5)
#define I2S_FRAMESTART_EVENT_ADDRESS nrf_tdm_event_address_get(NRF_TDM130, NRF_TDM_EVENT_MAXCNT)

static inline void feedback_target_init(void)
{
	/* Enable Start-of-Frame workaround in TIMER131 */
	*(volatile uint32_t *)0x5F9A3C04 = 0x00000002;
	*(volatile uint32_t *)0x5F9A3C04 = 0x00000003;
	*(volatile uint32_t *)0x5F9A3C80 = 0x00000082;
}

#include <nrfx_gpiote.h>
#include <nrfx_timer.h>
#include <helpers/nrfx_gppi.h>

static const nrfx_gpiote_t gpiote = NRFX_GPIOTE_INSTANCE(130);

#define GPIOTE_PPI_SOF_PIN    (9 * 32)
#define GPIOTE_PPI_MAXCNT_PIN (9 * 32) + 1

static uint32_t gpiote_setup(uint32_t pin)
{
	uint8_t gpiote_ch;
	nrfx_err_t err;

	err = nrfx_gpiote_channel_alloc(&gpiote, &gpiote_ch);
	if (err != NRFX_SUCCESS) {
		LOG_ERR("failed to allocate gpiote");
		return 0;
	}

	nrfx_gpiote_task_config_t task_config = {
		.task_ch = gpiote_ch,
		.polarity = NRF_GPIOTE_POLARITY_TOGGLE,
		.init_val = NRF_GPIOTE_INITIAL_VALUE_LOW
	};
	nrfx_gpiote_output_config_t out_config = {
		.drive = NRF_GPIO_PIN_S0S1,
		.input_connect = NRF_GPIO_PIN_INPUT_DISCONNECT,
		.pull = NRF_GPIO_PIN_NOPULL
	};

	err = nrfx_gpiote_output_configure(&gpiote, pin, &out_config, &task_config);
	if (err != NRFX_SUCCESS) {
		LOG_ERR("failed to configure pin");
		return 0;
	}

	nrfx_gpiote_out_task_enable(&gpiote, pin);

	return nrfx_gpiote_out_task_address_get(&gpiote, pin);
}
#endif

#else
#error "Unsupported target"
#endif

#if DO_INIT
static const nrfx_timer_t feedback_timer_instance =
	NRFX_TIMER_INSTANCE(FEEDBACK_TIMER_INSTANCE_NUMBER);
#endif

/* While it might be possible to determine I2S FRAMESTART to USB SOF offset
 * entirely in software, the I2S API lacks appropriate timestamping. Therefore
 * this sample uses target-specific code to perform the measurements. Note that
 * the use of dedicated target-specific peripheral essentially eliminates
 * software scheduling jitter and it is likely that a pure software only
 * solution would require additional filtering in indirect offset measurements.
 *
 * Use timer clock (independent from both Audio clock and USB host SOF clock)
 * values directly to determine samples offset. This works fine because the
 * regulator cares only about error (SOF offset is both error and regulator
 * input) and achieves its goal by sending nominal + 1 or nominal - 1 samples.
 * SOF offset is around 0 when regulated and therefore the relative clock
 * frequency discrepancies are essentially negligible.
 */
#define CLKS_PER_SAMPLE	(16000000 / (SAMPLE_RATE))

static struct feedback_ctx {
	int32_t rel_sof_offset;
	int32_t base_sof_offset;
	int nominal;
} fb_ctx;

struct feedback_ctx *feedback_init(void)
{
#if DO_INIT
	nrfx_err_t err;
	uint8_t usbd_sof_gppi_channel;
	uint8_t i2s_framestart_gppi_channel;
	const nrfx_timer_config_t cfg = {
		.frequency = NRFX_MHZ_TO_HZ(16UL),
		.mode = NRF_TIMER_MODE_TIMER,
		.bit_width = NRF_TIMER_BIT_WIDTH_32,
		.interrupt_priority = NRFX_TIMER_DEFAULT_CONFIG_IRQ_PRIORITY,
		.p_context = NULL,
	};

	feedback_target_init();
#endif

	feedback_reset_ctx(&fb_ctx);

#if DO_INIT
	err = nrfx_timer_init(&feedback_timer_instance, &cfg, NULL);
	if (err != NRFX_SUCCESS) {
		LOG_ERR("nrfx timer init error - Return value: %d", err);
		return &fb_ctx;
	}

	/* Subscribe TIMER CAPTURE task to USBD SOF event */
	err = nrfx_gppi_channel_alloc(&usbd_sof_gppi_channel);
	if (err != NRFX_SUCCESS) {
		LOG_ERR("gppi_channel_alloc failed with: %d\n", err);
		return &fb_ctx;
	}

	/* GPIOTE needs to hop through bridge, so we have to set it up first.
	 * The "forks" are not hopping through, and the GPIOTE would fail if
	 * setup was called with USB SOF to TIMER CAPTURE.
	 */
	nrfx_gppi_channel_endpoints_setup(usbd_sof_gppi_channel,
		USB_SOF_EVENT_ADDRESS,
		gpiote_setup(GPIOTE_PPI_SOF_PIN));
	nrfx_gppi_fork_endpoint_setup(usbd_sof_gppi_channel,
		nrfx_timer_capture_task_address_get(&feedback_timer_instance,
			FEEDBACK_TIMER_USBD_SOF_CAPTURE));
	nrfx_gppi_fork_endpoint_setup(usbd_sof_gppi_channel,
		nrfx_timer_task_address_get(&feedback_timer_instance,
			NRF_TIMER_TASK_CLEAR));

	nrfx_gppi_channels_enable(BIT(usbd_sof_gppi_channel));

	/* Subscribe TIMER CAPTURE task to I2S FRAMESTART event */
	err = nrfx_gppi_channel_alloc(&i2s_framestart_gppi_channel);
	if (err != NRFX_SUCCESS) {
		LOG_ERR("gppi_channel_alloc failed with: %d\n", err);
		return &fb_ctx;
	}

	nrfx_gppi_channel_endpoints_setup(i2s_framestart_gppi_channel,
		I2S_FRAMESTART_EVENT_ADDRESS,
		nrfx_timer_capture_task_address_get(&feedback_timer_instance,
			FEEDBACK_TIMER_I2S_FRAMESTART_CAPTURE));
	nrfx_gppi_fork_endpoint_setup(i2s_framestart_gppi_channel,
		gpiote_setup(GPIOTE_PPI_MAXCNT_PIN));

	nrfx_gppi_channels_enable(BIT(i2s_framestart_gppi_channel));

	/* Enable feedback timer */
	nrfx_timer_enable(&feedback_timer_instance);
#endif

	return &fb_ctx;
}

static void update_sof_offset(struct feedback_ctx *ctx, uint32_t sof_cc,
			      uint32_t framestart_cc)
{
	int sof_offset;

	/* /2 because we treat the middle as a turning point from being
	 * "too late" to "too early".
	 */
	if (framestart_cc > (ctx->nominal * CLKS_PER_SAMPLE)/2) {
		sof_offset = framestart_cc - ctx->nominal * CLKS_PER_SAMPLE;
	} else {
		sof_offset = framestart_cc;
	}

	/* The heuristic above is not enough when the offset gets too large.
	 * If the sign of the simple heuristic changes, check whether the offset
	 * crossed through the zero or the outer bound.
	 */
	if ((ctx->rel_sof_offset >= 0) != (sof_offset >= 0)) {
		uint32_t abs_diff;
		int32_t base_change;

		if (sof_offset >= 0) {
			abs_diff = sof_offset - ctx->rel_sof_offset;
			base_change = -(ctx->nominal * CLKS_PER_SAMPLE);
		} else {
			abs_diff = ctx->rel_sof_offset - sof_offset;
			base_change = ctx->nominal * CLKS_PER_SAMPLE;
		}

		/* Adjust base offset only if the change happened through the
		 * outer bound. The actual changes should be significantly lower
		 * than the threshold here.
		 */
		if (abs_diff > (ctx->nominal * CLKS_PER_SAMPLE)/2) {
			ctx->base_sof_offset += base_change;
		}
	}

	ctx->rel_sof_offset = sof_offset;
}

void feedback_process(struct feedback_ctx *ctx)
{
	uint32_t sof_cc;
	uint32_t framestart_cc;

#if DO_INIT
	sof_cc = nrfx_timer_capture_get(&feedback_timer_instance,
		FEEDBACK_TIMER_USBD_SOF_CAPTURE);
	framestart_cc = nrfx_timer_capture_get(&feedback_timer_instance,
		FEEDBACK_TIMER_I2S_FRAMESTART_CAPTURE);
#else
	sof_cc = nrf_timer_cc_get(NRF_TIMER131, FEEDBACK_TIMER_USBD_SOF_CAPTURE);
	framestart_cc = nrf_timer_cc_get(NRF_TIMER131, FEEDBACK_TIMER_I2S_FRAMESTART_CAPTURE);
#endif

	update_sof_offset(ctx, sof_cc, framestart_cc);
}

void feedback_reset_ctx(struct feedback_ctx *ctx)
{
	ARG_UNUSED(ctx);
}

void feedback_start(struct feedback_ctx *ctx, int i2s_blocks_queued,
		    bool microframes)
{
	if (microframes) {
		ctx->nominal = SAMPLE_RATE / 8000;
	} else {
		ctx->nominal = SAMPLE_RATE / 1000;
	}

	/* I2S data was supposed to go out at SOF, but it is inevitably
	 * delayed due to triggering I2S start by software. Set relative
	 * SOF offset value in a way that ensures that values past "half
	 * frame" are treated as "too late" instead of "too early"
	 */
	ctx->rel_sof_offset = (ctx->nominal * CLKS_PER_SAMPLE) / 2;
	/* If there are more than 2 I2S TX blocks queued, use feedback regulator
	 * to correct the situation.
	 */
	ctx->base_sof_offset = (i2s_blocks_queued - 2) *
		(ctx->nominal * CLKS_PER_SAMPLE);
}

int feedback_samples_offset(struct feedback_ctx *ctx)
{
	int32_t offset = ctx->rel_sof_offset + ctx->base_sof_offset;

	return offset / CLKS_PER_SAMPLE;
}

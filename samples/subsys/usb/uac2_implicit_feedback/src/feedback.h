/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FEEDBACK_H_
#define FEEDBACK_H_

#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/usb/usbd.h>

/* This sample is currently supporting only 48 kHz sample rate. */
#define SAMPLE_RATE         48000

BUILD_ASSERT(UTIL_AND(
	IS_EQ(DT_NODE_HAS_PROP(DT_NODELABEL(as_iso_in), polling_period_us),
	      DT_NODE_HAS_PROP(DT_NODELABEL(as_iso_out), polling_period_us)),
	IS_EQ(DT_PROP(DT_NODELABEL(as_iso_in), polling_period_us),
	      DT_PROP(DT_NODELABEL(as_iso_out), polling_period_us))),
	"Sample requires common IN and OUT polling periods");

#define FS_SOF_PERIODS(us) BIT(USB_FS_ISO_EP_INTERVAL(us) - 1)
#define HS_SOF_PERIODS(us) BIT(USB_HS_ISO_EP_INTERVAL(us) - 1)

#define FS_PERIOD_US DT_PROP_OR(DT_NODELABEL(as_iso_in), polling_period_us, 1000)
#define HS_PERIOD_US DT_PROP_OR(DT_NODELABEL(as_iso_in), polling_period_us, 125)

#define HIGH_SPEED_SOF_PERIODS HS_SOF_PERIODS(HS_PERIOD_US)
#define FULL_SPEED_SOF_PERIODS FS_SOF_PERIODS(FS_PERIOD_US)

#if (USBD_SUPPORTS_HIGH_SPEED && HS_PERIOD_US >= 250) || (FS_PERIOD_US >= 2000)
#define SOF_PERIOD_TRACKING_NEEDED 1
#else
#define SOF_PERIOD_TRACKING_NEEDED 0
#endif

struct feedback_ctx *feedback_init(void);
void feedback_reset_ctx(struct feedback_ctx *ctx);
void feedback_process(struct feedback_ctx *ctx);
void feedback_start(struct feedback_ctx *ctx, int i2s_blocks_queued,
                    bool microframes);

/* Return offset between I2S block start and USB SOF in samples.
 *
 * Positive offset means that I2S block started at least 1 sample after SOF and
 * to correct the situation, shorter than nominal buffers are needed.
 *
 * Negative offset means that I2S block started at least 1 sample before SOF and
 * to correct the situation, larger than nominal buffers are needed.
 *
 * Offset 0 means that I2S block started within 1 sample around SOF. This is the
 * dominant value expected during normal operation.
 */
int feedback_samples_offset(struct feedback_ctx *ctx);

#endif /* FEEDBACK_H_ */

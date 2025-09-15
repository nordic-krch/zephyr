/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <nrf_ironside/krch.h>

LOG_MODULE_REGISTER(krch_sample, CONFIG_LOG_DEFAULT_LEVEL);

/* SPU133 base address from nRF54H20 MDK */
#define NRF_SPU133_BASE 0x5F990000UL

/*
 * Estimated offset to SPU133->FEATURE.GRTC.CC[2] register
 * This is a SPU feature register that is locked to NRF_OWNER_SECURE
 * and should only be accessible through IronSide services.
 *
 * Based on typical SPU structure layout:
 * - PERIPH array at beginning (estimated ~0x1000 for all peripheral entries)
 * - FEATURE structure follows PERIPH array
 * - GRTC is one of the feature blocks within FEATURE
 * - CC[2] is the 3rd capture/compare register (2 * 4 bytes offset)
 */
#define SPU133_FEATURE_GRTC_CC2_OFFSET 0x1500UL
#define SPU133_FEATURE_GRTC_CC2_ADDR   (NRF_SPU133_BASE + SPU133_FEATURE_GRTC_CC2_OFFSET)

int main(void)
{
	int err;

	LOG_INF("IronSide KRCH Service Example - SPU Register Write");
	LOG_INF("This sample demonstrates writing to a SPU register that only IronSide should "
		"access");

	/*
	 * Write to SPU133 FEATURE.GRTC.CC[2] register
	 * This register is configured as:
	 * NRF_SPU133->FEATURE.GRTC.CC[2] = FEATURE_PERM(NRF_OWNER_SECURE, SECATTR_SECURE, LOCKED);
	 *
	 * Only IronSide services should be able to write to this register.
	 * Writing 0x12345678 as a test value.
	 */
	LOG_INF("Writing to SPU133.FEATURE.GRTC.CC[2] at address 0x%08lX",
		SPU133_FEATURE_GRTC_CC2_ADDR);
	err = ironside_krch_memory_write(SPU133_FEATURE_GRTC_CC2_ADDR, 0x12345678);
	if (err == 0) {
		LOG_INF("Successfully wrote to SPU133.FEATURE.GRTC.CC[2] = 0x12345678");
	} else {
		LOG_ERR("Failed to write to SPU133.FEATURE.GRTC.CC[2]: %d", err);
	}

	LOG_INF("KRCH service example completed - SPU register write demonstrated");
	return 0;
}

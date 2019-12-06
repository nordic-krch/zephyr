/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_DRIVERS_CLOCK_CONTROL_NRF_CLOCK_CALIBRATION_H_
#define ZEPHYR_DRIVERS_CLOCK_CONTROL_NRF_CLOCK_CALIBRATION_H_

#include <device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calibration interrupts handler
 *
 * Must be called from clock interrupt context.
 */
void z_nrf_clock_calibration_isr(void);

/**
 * @brief Notify calibration module about LF clock start
 *
 * @param clk_dev CLK device.
 */
void z_nrf_clock_calibration_lfclk_started(struct device *clk_dev);

/**
 * @brief Stop calibration.
 *
 * Function called when LFCLK RC clock is being stopped.
 *
 * @param dev LFCLK device.
 *
 * @retval true if clock can be stopped.
 * @retval false if due to ongoing calibration clock cannot be stopped. In that
 *	   case calibration module will stop clock when calibration is
 *	   completed.
 */
void z_nrf_clock_calibration_stop(struct device *dev);


#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_CLOCK_CONTROL_NRF_CLOCK_CALIBRATION_H_ */

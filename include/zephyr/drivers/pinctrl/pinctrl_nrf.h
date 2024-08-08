#include <zephyr/drivers/pinctrl.h>

#ifndef ZEPHYR_INCLUDE_DRIVERS_PINCTRL_PINCTRL_NRF_H_
#define ZEPHYR_INCLUDE_DRIVERS_PINCTRL_PINCTRL_NRF_H_

int nrf_pinctrl_clock_pin_check(const struct pinctrl_dev_config *pcfg,
				const uint32_t *clock_pins,
				size_t cnt,
				uint32_t *err_fun);

#endif /* ZEPHYR_INCLUDE_DRIVERS_PINCTRL_PINCTRL_NRF_H_ */

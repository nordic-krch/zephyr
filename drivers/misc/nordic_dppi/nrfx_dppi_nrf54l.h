#ifndef ZEPHYR_DRIVERS_MISC_NORDIC_DPPI_NRF54L_H__
#define ZEPHYR_DRIVERS_MISC_NORDIC_DPPI_NRF54L_H__

#include <zephyr/drivers/misc/ppi/nrfx_dppi.h>

/* Domain ID decremented by 1 compared to value in the address. */
#define DPPI_LUMOS_DOMAIN_MCU 0
#define DPPI_LUMOS_DOMAIN_RAD 1
#define DPPI_LUMOS_DOMAIN_PERI 2
#define DPPI_LUMOS_DOMAIN_LP 3

#endif /* ZEPHYR_DRIVERS_MISC_NORDIC_DPPI_NRF54L_H__ */

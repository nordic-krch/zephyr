#ifndef ZEPHYR_DRIVERS_MISC_NORDIC_DPPI_NRF54H_H__
#define ZEPHYR_DRIVERS_MISC_NORDIC_DPPI_NRF54H_H__

#include <zephyr/drivers/misc/ppi/nrfx_dppi.h>
#include "nrfx_dppi_routes.h"

enum nrf_dppi_domain {
	/* Global domain */
	NRF_DPPI_DOMAIN_APB22,
	NRF_DPPI_DOMAIN_APB32,
	NRF_DPPI_DOMAIN_APB38,
	NRF_DPPI_DOMAIN_APB39,
	NRF_DPPI_DOMAIN_APB3A,
	NRF_DPPI_DOMAIN_APB3B,
	NRF_DPPI_DOMAIN_APB3C,
	NRF_DPPI_DOMAIN_APB3D,
	/* Radio domain */
	NRF_DPPI_DOMAIN_APB2,
	NRF_DPPI_DOMAIN_APB3,
};

static inline NRF_DPPIC_Type *nrfx_dppi_get_reg(uint32_t id)
{
	static const NRF_DPPIC_Type *dppi_regs[] = {
		NRF_DPPIC120,
		NRF_DPPIC130,
		NRF_DPPIC131,
		NRF_DPPIC132,
		NRF_DPPIC133,
		NRF_DPPIC134,
		NRF_DPPIC135,
		NRF_DPPIC136,
	#ifdef NRF_DPPIC020
		NRF_DPPIC020,
	#endif
	#ifdef NRF_DPPIC030
		NRF_DPPIC030,
	#endif
	};

	return (NRF_DPPIC_Type *)dppi_regs[id];
}

#endif /* ZEPHYR_DRIVERS_MISC_NORDIC_DPPI_NRF54H_H__ */

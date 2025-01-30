#include <zephyr/drivers/misc/ppi/nrfx_dppi.h>
#include "nrfx_dppi_nrf54h.h"

uint32_t nrf_dppi_get_domain_id(uint32_t addr)
{
	uint32_t domain = (addr >> 24) & 0xf;
	uint32_t apb = (addr >> 16) & 0xff;

	if (domain == 0x3) {
		return (apb == 2) ? NRF_DPPI_DOMAIN_APB2 : NRF_DPPI_DOMAIN_APB3;
	}

	__ASSERT_NO_MSG(domain == 0xf);

	if (apb < 0x92) {
		return NRF_DPPI_DOMAIN_APB22;
	} else if (apb <= 0x93) {
		return NRF_DPPI_DOMAIN_APB32;
	} else {
		return apb - 0x98 + NRF_DPPI_DOMAIN_APB38;
	}
}

#if !defined(CONFIG_SOC_NRF54H20_CPUSEC)
int nrf_dppi_service_alloc(uint32_t producer, uint32_t consumer, nrf_dppi_route_handle_t *handle);
void nrf_dppi_service_free(nrf_dppi_route_handle_t handle);
#if defined(CONFIG_SOC_NRF54H20_CPURAD)
int nrf_dppi_domain_local_alloc(uint32_t producer, uint32_t consumer, nrf_dppi_route_handle_t *handle);
void nrf_dppi_domain_local_free(nrf_dppi_route_handle_t handle);
#endif

int nrf_dppi_domain_conn_alloc(uint32_t producer, uint32_t consumer, nrf_dppi_route_handle_t *handle)
{
#if defined(CONFIG_SOC_NRF54H20_CPURAD)
#define NRF_DPPI_IS_RAD_DOMAIN(x) ((((uintptr_t)x >> 24) & 0xF) == 3)
	if (NRF_DPPI_IS_RAD_DOMAIN(producer) && NRF_DPPI_IS_RAD_DOMAIN(consumer)) {
		return nrf_dppi_domain_local_alloc(producer, consumer, handle);
	}
	if (!NRF_DPPI_IS_RAD_DOMAIN(producer) && !NRF_DPPI_IS_RAD_DOMAIN(consumer)) {
		return nrf_dppi_service_alloc(producer, consumer, handle);
	}

	return -EINVAL;
#else
	return nrf_dppi_service_alloc(producer, consumer, handle);
#endif
}

void nrf_dppi_domain_conn_free(nrf_dppi_route_handle_t handle)
{
#if defined(CONFIG_SOC_NRF54H20_CPURAD)
	if (handle & BIT(24)) {
		return nrf_dppi_domain_local_free(handle);
	} else {
		nrf_dppi_service_free(handle);
	}
#else
	nrf_dppi_service_free(handle);
#endif
}
#endif

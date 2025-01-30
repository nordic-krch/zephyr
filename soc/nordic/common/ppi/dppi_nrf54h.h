/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOC_NORDIC_COMMON_PPI_DPPI_NRF54H_H_
#define SOC_NORDIC_COMMON_PPI_DPPI_NRF54H_H_

#include <ppi/gppi.h>
#include "dppi_routes.h"

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
};

void gppi_set_channel_resource(uint32_t domain_id, uint32_t ch_mask);
void gppi_set_group_channel_resource(uint32_t domain_id, uint32_t ch_mask);

#endif /* SOC_NORDIC_COMMON_PPI_DPPI_NRF54H_H_ */

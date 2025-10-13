/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ppi/gppi.h>
#include "dppi_nrf54h.h"
#include <nrf_ironside/krch.h>

struct domain_data {
	NRF_DPPIC_Type *dppic;
	NRF_PPIB_Type *ppib;
	uint32_t ch_cnt;
	uint32_t group_ch_cnt;
};

static const struct domain_data domains[] = {
	[NRF_DPPI_DOMAIN_APB22] = {
		.dppic = NRF_DPPIC120,
		.ppib = (NRF_PPIB_Type *)DT_REG_ADDR(DT_NODELABEL(ppib121)),
		.ch_cnt = DPPIC120_CH_NUM_SIZE,
		.group_ch_cnt = DPPIC120_GROUP_NUM_SIZE
	},
	[NRF_DPPI_DOMAIN_APB32] = {
		.dppic = NRF_DPPIC130,
		.ppib = NULL,
		.ch_cnt = DPPIC130_CH_NUM_SIZE,
		.group_ch_cnt = DPPIC130_GROUP_NUM_SIZE
	},
	[NRF_DPPI_DOMAIN_APB38] = {
		.dppic = NRF_DPPIC131,
		.ppib = (NRF_PPIB_Type *)DT_REG_ADDR(DT_NODELABEL(ppib132)),
		.ch_cnt = DPPIC131_CH_NUM_SIZE,
		.group_ch_cnt = DPPIC131_GROUP_NUM_SIZE
	},
	[NRF_DPPI_DOMAIN_APB39] = {
		.dppic = NRF_DPPIC132,
		.ppib = (NRF_PPIB_Type *)DT_REG_ADDR(DT_NODELABEL(ppib133)),
		.ch_cnt = DPPIC132_CH_NUM_SIZE,
		.group_ch_cnt = DPPIC132_GROUP_NUM_SIZE
	},
	[NRF_DPPI_DOMAIN_APB3A] = {
		.dppic = NRF_DPPIC133,
		.ppib = (NRF_PPIB_Type *)DT_REG_ADDR(DT_NODELABEL(ppib134)),
		.ch_cnt = DPPIC133_CH_NUM_SIZE,
		.group_ch_cnt = DPPIC133_GROUP_NUM_SIZE
	},
	[NRF_DPPI_DOMAIN_APB3B] = {
		.dppic = NRF_DPPIC134,
		.ppib = (NRF_PPIB_Type *)DT_REG_ADDR(DT_NODELABEL(ppib135)),
		.ch_cnt = DPPIC134_CH_NUM_SIZE,
		.group_ch_cnt = DPPIC134_GROUP_NUM_SIZE
	},
	[NRF_DPPI_DOMAIN_APB3C] = {
		.dppic = NRF_DPPIC135,
		.ppib = (NRF_PPIB_Type *)DT_REG_ADDR(DT_NODELABEL(ppib136)),
		.ch_cnt = DPPIC135_CH_NUM_SIZE,
		.group_ch_cnt = DPPIC135_GROUP_NUM_SIZE
	},
	[NRF_DPPI_DOMAIN_APB3D] = {
		.dppic = NRF_DPPIC136,
		.ppib = (NRF_PPIB_Type *)DT_REG_ADDR(DT_NODELABEL(ppib137)),
		.ch_cnt = DPPIC136_CH_NUM_SIZE,
		.group_ch_cnt = DPPIC136_GROUP_NUM_SIZE
	},
};

int nrf_ppib_write(volatile uint32_t *addr, uint32_t val)
{
	return ironside_krch_memory_write((uint32_t)addr, val);
}

static int init_dppi_instance(uint32_t domain_id, uint32_t *ch_mask)
{
	uint32_t mask = 0;
	uint32_t value;
	int err;
	const struct domain_data *domain = &domains[domain_id];

	for (uint32_t i = 0; i < domain->group_ch_cnt; i++) {
		err = ironside_krch_memory_read((uint32_t)&domain->dppic->CHG[i], &value);
		if (err < 0) {
			return err;
		}
		if (value == 0) {
			mask |= BIT(i);
		}
	}

	gppi_set_group_channel_resource(domain_id, mask);

	if (domain->ppib == NULL) {
		*ch_mask = BIT_MASK(domain->ch_cnt);
		return 0;
	};

	mask = 0;
	for (uint32_t i = 0; i < domain->ch_cnt; i++) {
		err = ironside_krch_memory_read((uint32_t)&domain->ppib->SUBSCRIBE_SEND[i], &value);
		if (err < 0) {
			return err;
		}

		if (value & IPCT_SUBSCRIBE_SEND_EN_Msk) {
			continue;
		}

		err = ironside_krch_memory_read((uint32_t)&domain->ppib->PUBLISH_RECEIVE[i], &value);
		if (err < 0) {
			return err;
		}

		if (value & IPCT_PUBLISH_RECEIVE_EN_Msk) {
			continue;
		}

		mask |= BIT(i);
	}

	gppi_set_channel_resource(domain_id, mask);
	*ch_mask = mask;

	return 0;
}

static int init_dppi_resources(void)
{
	if (1) {
		for (int i = 0; i < NRF_DPPI_NODES_COUNT; i++) {
			gppi_set_channel_resource(i, 0xFF);
		}
		return 0;
	}
	uint32_t complete_mask = UINT32_MAX;
	uint32_t mask = 0;
	int err;

	for (size_t i = 0; i < ARRAY_SIZE(domains); i++) {
		err = init_dppi_instance(i, &mask);
		complete_mask &= mask;
	}

	/* APB32 is special as it is a central point so any channel pre-allocated in
	 * any PPIB need to be pre-allocated in the APB32 resources.
	 */
	gppi_set_channel_resource(NRF_DPPI_DOMAIN_APB32, complete_mask);

	return 0;
}

SYS_INIT(init_dppi_resources, POST_KERNEL, UTIL_INC(CONFIG_NRF_IRONSIDE_CALL_INIT_PRIORITY));

/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/nrf_ppi.h>
#include <ppi/gppi.h>

static atomic_t ch_mask = BIT_MASK(PPI_CH_NUM);
static atomic_t group_mask = BIT_MASK(PPI_GROUP_NUM);

#define PPI_EP_IS_EVT(_ep) ((_ep) & BIT(8))
static int alloc_ch(atomic_t *mask, uint32_t *ch)
{
	uint32_t key = irq_lock();

	if (ch_mask == 0) {
		irq_unlock(key);
		return -ENOMEM;
	}

	*ch = 31 - __builtin_clz(ch_mask);
	ch_mask &= ~BIT(*ch);
	irq_unlock(key);
	return 0;
}

static void free_ch(atomic_t *mask, uint32_t ch)
{
	uint32_t prev = atomic_or(mask, BIT(ch));
	(void)prev;
	__ASSERT_NO_MSG((prev & BIT(ch)) == 0);
}

int gppi_domain_conn_alloc(uint32_t producer, uint32_t consumer, gppi_handle_t *handle)
{
	ARG_UNUSED(producer);
	ARG_UNUSED(consumer);
	return alloc_ch(&ch_mask, handle);
}

void gppi_conn_free(uint32_t eep, uint32_t tep, gppi_handle_t handle)
{
	nrf_ppi_fork_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)handle, 0);
	nrf_ppi_task_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)handle, 0);
	nrf_ppi_event_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)handle, 0);
	free_ch(&ch_mask, handle);
}

void gppi_domain_conn_free(gppi_handle_t handle)
{
	free_ch(&ch_mask, handle);
}

int gppi_group_alloc(uint32_t *ep, size_t ep_cnt, gppi_group_handle_t *handle)
{
	ARG_UNUSED(ep);
	ARG_UNUSED(ep_cnt);
	return alloc_ch(&group_mask, handle);
}

void gppi_group_free(gppi_group_handle_t handle)
{
	free_ch(&group_mask, handle);
}

int gppi_ep_attach(gppi_handle_t handle, uint32_t ep)
{
	nrf_ppi_channel_t ch = (nrf_ppi_channel_t)handle;

	if (PPI_EP_IS_EVT(ep)) {
		if (NRF_PPI->CH[ch].EEP != 0) {
			return -ENOTSUP;
		}
		NRF_PPI->CH[ch].EEP = ep;
		return 0;
	}

	if (nrf_ppi_task_endpoint_get(NRF_PPI, ch) == 0) {
		nrf_ppi_task_endpoint_setup(NRF_PPI, ch, ep);
		return 0;
	}

#ifdef PPI_FEATURE_FORKS_PRESENT
	if (nrf_ppi_fork_endpoint_get(NRF_PPI, ch) != 0) {
		return -EBUSY;
	}
	nrf_ppi_fork_endpoint_setup(NRF_PPI, ch, ep);
	return 0;
#else
	return -ENOTSUP;
#endif
}

void gppi_ep_clear(uint32_t ep)
{
	if (!PPI_EP_IS_EVT(ep)) {
#ifdef PPI_FEATURE_FORKS_PRESENT
		for (int i = 0; i < PPI_CH_NUM; i++) {
			if (nrf_ppi_fork_endpoint_get(NRF_PPI, i) == ep) {
				nrf_ppi_fork_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)i, 0);
				return;
			}
		}
#endif
		for (int i = 0; i < PPI_CH_NUM; i++) {
			if (NRF_PPI->CH[i].TEP == ep) {
				nrf_ppi_task_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)i, 0);
				return;
			}
		}
	} else {
		for (int i = 0; i < PPI_CH_NUM; i++) {
			if (NRF_PPI->CH[i].EEP == ep) {
				nrf_ppi_event_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)i, 0);
				return;
			}
		}
	}
}

void gppi_conn_enable(gppi_handle_t handle)
{
	nrf_ppi_channel_enable(NRF_PPI, (nrf_ppi_channel_t)handle);
}

void gppi_conn_disable(gppi_handle_t handle)
{
	nrf_ppi_channel_disable(NRF_PPI, (nrf_ppi_channel_t)handle);
}

void gppi_chan_enable(uint32_t domain_id, uint32_t ch)
{
	ARG_UNUSED(domain_id);
	nrf_ppi_channel_enable(NRF_PPI, (nrf_ppi_channel_t)ch);
}

void gppi_chan_disable(uint32_t domain_id, uint32_t ch)
{
	ARG_UNUSED(domain_id);
	nrf_ppi_channel_disable(NRF_PPI, (nrf_ppi_channel_t)ch);
}

void gppi_group_ch_add(gppi_group_handle_t handle, uint32_t channel)
{
	nrf_ppi_channel_include_in_group(NRF_PPI, channel,
					 (nrf_ppi_channel_group_t)handle);
}

void gppi_group_ch_remove(gppi_group_handle_t handle, uint32_t channel)
{
	nrf_ppi_channel_remove_from_group(NRF_PPI, channel,
					  (nrf_ppi_channel_group_t)handle);
}

void gppi_group_en(gppi_group_handle_t handle)
{
	nrf_ppi_group_enable(NRF_PPI, (nrf_ppi_channel_group_t)handle);
}

void gppi_group_dis(gppi_group_handle_t handle)
{
	nrf_ppi_group_disable(NRF_PPI, (nrf_ppi_channel_group_t)handle);
}

uint32_t gppi_group_task_en_addr(gppi_group_handle_t handle)
{
	return nrf_ppi_task_group_enable_address_get(NRF_PPI, (nrf_ppi_channel_group_t)handle);
}

uint32_t gppi_group_task_dis_addr(gppi_group_handle_t handle)
{
	return nrf_ppi_task_group_disable_address_get(NRF_PPI, (nrf_ppi_channel_group_t)handle);
}

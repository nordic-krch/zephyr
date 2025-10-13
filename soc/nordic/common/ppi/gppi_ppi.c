/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/nrf_ppi.h>
#include <ppi/nrfx_gppi.h>

#define PPI_EP_IS_EVT(_ep) ((_ep) & BIT(8))

static int alloc_ch(atomic_t *mask, uint32_t *ch)
{
	uint32_t key = irq_lock();

	if (*mask == 0) {
		irq_unlock(key);
		return -ENOMEM;
	}

	*ch = 31 - __builtin_clz(*mask);
	*mask &= ~BIT(*ch);
	irq_unlock(key);
	return 0;
}

static void free_ch(atomic_t *mask, uint32_t ch)
{
	uint32_t prev = atomic_or(mask, BIT(ch));
	(void)prev;
	__ASSERT_NO_MSG((prev & BIT(ch)) == 0);
}

int nrfx_gppi_domain_conn_alloc(nrfx_gppi_t *gppi, uint32_t producer,
			   uint32_t consumer, nrfx_gppi_handle_t *handle)
{
	ARG_UNUSED(producer);
	ARG_UNUSED(consumer);
	return alloc_ch(&gppi->ch_mask, handle);
}

void nrfx_gppi_conn_free(nrfx_gppi_t *gppi, uint32_t eep, uint32_t tep, nrfx_gppi_handle_t handle)
{
	nrf_ppi_fork_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)handle, 0);
	nrf_ppi_task_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)handle, 0);
	nrf_ppi_event_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)handle, 0);
	free_ch(&gppi->ch_mask, handle);
}

void nrfx_gppi_domain_conn_free(nrfx_gppi_t *gppi, nrfx_gppi_handle_t handle)
{
	free_ch(&gppi->ch_mask, handle);
}

int nrfx_gppi_group_alloc(nrfx_gppi_t *gppi, uint32_t *ep, size_t ep_cnt, nrfx_gppi_group_handle_t *handle)
{
	ARG_UNUSED(ep);
	ARG_UNUSED(ep_cnt);
	return alloc_ch(&gppi->group_mask, handle);
}

void nrfx_gppi_group_free(nrfx_gppi_t *gppi, nrfx_gppi_group_handle_t handle)
{
	free_ch(&gppi->group_mask, handle);
}

int nrfx_gppi_ep_attach(nrfx_gppi_t *gppi, nrfx_gppi_handle_t handle, uint32_t ep)
{
	ARG_UNUSED(gppi);
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

int nrfx_gppi_ep_channel(uint32_t ep)
{
	if (!PPI_EP_IS_EVT(ep)) {
#ifdef PPI_FEATURE_FORKS_PRESENT
		for (int i = 0; i < PPI_CH_NUM; i++) {
			if (nrf_ppi_fork_endpoint_get(NRF_PPI, i) == ep) {
				nrf_ppi_fork_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)i, 0);
				return i;
			}
		}
#endif
		for (int i = 0; i < PPI_CH_NUM; i++) {
			if (NRF_PPI->CH[i].TEP == ep) {
				nrf_ppi_task_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)i, 0);
				return i;
			}
		}
	} else {
		for (int i = 0; i < PPI_CH_NUM; i++) {
			if (NRF_PPI->CH[i].EEP == ep) {
				nrf_ppi_event_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)i, 0);
				return i;
			}
		}
	}

	return -EINVAL;
}

void nrfx_gppi_ep_clear(uint32_t ep)
{
	int ch = nrfx_gppi_ep_channel(ep);

	if (ch < 0) {
		return;
	}

	if (PPI_EP_IS_EVT(ep)) {
		nrf_ppi_event_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)ch, 0);
		return;
	}
#ifdef PPI_FEATURE_FORKS_PRESENT
	if (nrf_ppi_fork_endpoint_get(NRF_PPI, ch) == ep) {
		nrf_ppi_fork_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)ch, 0);
		return;
	}
#endif
	nrf_ppi_task_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)ch, 0);
}

void nrfx_gppi_conn_enable(nrfx_gppi_t *gppi, nrfx_gppi_handle_t handle)
{
	ARG_UNUSED(gppi);
	nrf_ppi_channel_enable(NRF_PPI, (nrf_ppi_channel_t)handle);
}

void nrfx_gppi_conn_disable(nrfx_gppi_t *gppi, nrfx_gppi_handle_t handle)
{
	ARG_UNUSED(gppi);
	nrf_ppi_channel_disable(NRF_PPI, (nrf_ppi_channel_t)handle);
}

void nrfx_gppi_chan_enable(nrfx_gppi_t *gppi, uint32_t domain_id, uint32_t ch)
{
	ARG_UNUSED(gppi);
	ARG_UNUSED(domain_id);
	nrf_ppi_channel_enable(NRF_PPI, (nrf_ppi_channel_t)ch);
}

void nrfx_gppi_chan_disable(nrfx_gppi_t *gppi, uint32_t domain_id, uint32_t ch)
{
	ARG_UNUSED(gppi);
	ARG_UNUSED(domain_id);
	nrf_ppi_channel_disable(NRF_PPI, (nrf_ppi_channel_t)ch);
}

void nrfx_gppi_group_ch_add(nrfx_gppi_t *gppi, nrfx_gppi_group_handle_t handle, uint32_t channel)
{
	ARG_UNUSED(gppi);
	nrf_ppi_channel_include_in_group(NRF_PPI, channel,
					 (nrf_ppi_channel_group_t)handle);
}

void nrfx_gppi_group_ch_remove(nrfx_gppi_t *gppi, nrfx_gppi_group_handle_t handle, uint32_t channel)
{
	ARG_UNUSED(gppi);
	nrf_ppi_channel_remove_from_group(NRF_PPI, channel,
					  (nrf_ppi_channel_group_t)handle);
}

void nrfx_gppi_group_en(nrfx_gppi_t *gppi, nrfx_gppi_group_handle_t handle)
{
	ARG_UNUSED(gppi);
	nrf_ppi_group_enable(NRF_PPI, (nrf_ppi_channel_group_t)handle);
}

void nrfx_gppi_group_dis(nrfx_gppi_t *gppi, nrfx_gppi_group_handle_t handle)
{
	ARG_UNUSED(gppi);
	nrf_ppi_group_disable(NRF_PPI, (nrf_ppi_channel_group_t)handle);
}

uint32_t nrfx_gppi_group_task_en_addr(nrfx_gppi_t *gppi, nrfx_gppi_group_handle_t handle)
{
	ARG_UNUSED(gppi);
	return nrf_ppi_task_group_enable_address_get(NRF_PPI, (nrf_ppi_channel_group_t)handle);
}

uint32_t nrfx_gppi_group_task_dis_addr(nrfx_gppi_t *gppi, nrfx_gppi_group_handle_t handle)
{
	ARG_UNUSED(gppi);
	return nrf_ppi_task_group_disable_address_get(NRF_PPI, (nrf_ppi_channel_group_t)handle);
}

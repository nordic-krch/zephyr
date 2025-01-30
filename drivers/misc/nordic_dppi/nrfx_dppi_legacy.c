#include <hal/nrf_dppi.h>
#include <helpers/nrfx_flag32_allocator.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/misc/ppi/nrfx_dppi.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dppi, 3);

static atomic_t ch_mask =
	COND_CODE_1(IS_EQ(DPPIC_CH_NUM, 32), (0xFFFFFFFF), (BIT_MASK(DPPIC_CH_NUM))) &
	~BIT_MASK(NRFX_DPPI0_CHANNELS_USED);

static atomic_t group_mask = BIT_MASK(DPPIC_GROUP_NUM) & ~BIT_MASK(NRFX_DPPI0_GROUPS_USED);

static int alloc_bit(atomic_t *mask)
{
	int rv;
	int key = irq_lock();

	if (*mask == 0) {
		rv = -ENOMEM;
	} else {
		rv = 31 - __builtin_clz(*mask);
		*mask &= ~BIT(rv);
	}
	irq_unlock(key);

	return rv;
}

static void free_bit(atomic_t *mask, uint32_t bit)
{
	atomic_or(mask, BIT(bit));
}

int nrf_dppi_domain_conn_alloc(uint32_t producer, uint32_t consumer, nrf_dppi_handle_t *handle)
{
	int rv = alloc_bit(&ch_mask);

	if (rv < 0) {
		return rv;
	}

	*handle = (nrf_dppi_handle_t)rv;
	return 0;
}

void nrf_dppi_domain_conn_free(nrf_dppi_handle_t handle)
{
	free_bit(&ch_mask, (uint32_t)handle);
}

int nrf_dppi_ep_attach(nrf_dppi_handle_t handle, uint32_t ep)
{
	NRF_DPPI_ENDPOINT_SETUP(ep, (uint32_t)handle);
	return 0;
}

void nrf_dppi_ep_clear(uint32_t ep)
{
	NRF_DPPI_ENDPOINT_CLEAR(ep);
}

void nrf_dppi_conn_enable(nrf_dppi_handle_t handle)
{
	nrf_dppi_channels_enable(NRF_DPPIC, BIT((uint32_t)handle));
}

void nrf_dppi_conn_disable(nrf_dppi_handle_t handle)
{
	nrf_dppi_channels_disable(NRF_DPPIC, BIT((uint32_t)handle));
}

void nrf_dppi_chan_enable(uint32_t domain_id, uint32_t ch)
{
	nrf_dppi_channels_enable(NRF_DPPIC, BIT(ch));
}

void nrf_dppi_chan_disable(uint32_t domain_id, uint32_t ch)
{
	nrf_dppi_channels_disable(NRF_DPPIC, BIT(ch));
}

int nrf_dppi_group_alloc(uint32_t *ep, size_t ep_cnt, nrf_dppi_group_handle_t *handle)
{
	ARG_UNUSED(ep);
	ARG_UNUSED(ep_cnt);
	int rv = alloc_bit(&group_mask);

	if (rv < 0) {
		return rv;
	}

	*handle = (nrf_dppi_handle_t)rv;
	return 0;
}

void nrf_dppi_group_free(nrf_dppi_group_handle_t handle)
{
	free_bit(&group_mask, (uint32_t)handle);
}

int nrf_dppi_group_ch_add(nrf_dppi_group_handle_t handle, uint32_t channel)
{
	nrf_dppi_channels_include_in_group(NRF_DPPIC, BIT(channel),
					   (nrf_dppi_channel_group_t)handle);
	return 0;
}

int nrf_dppi_group_ch_remove(nrf_dppi_group_handle_t handle, uint32_t channel)
{
	nrf_dppi_channels_remove_from_group(NRF_DPPIC, BIT(channel),
					    (nrf_dppi_channel_group_t)handle);
	return 0;
}

void nrf_dppi_group_en(nrf_dppi_group_handle_t handle)
{
	nrf_dppi_group_enable(NRF_DPPIC, (nrf_dppi_channel_group_t)handle);
}

void nrf_dppi_group_dis(nrf_dppi_group_handle_t handle)
{
	nrf_dppi_group_disable(NRF_DPPIC, (nrf_dppi_channel_group_t)handle);
}

uint32_t nrf_dppi_group_task_en_addr(nrf_dppi_group_handle_t handle)
{
	return nrf_dppi_task_address_get(NRF_DPPIC, nrf_dppi_group_enable_task_get(handle));
}

uint32_t nrf_dppi_group_task_dis_addr(nrf_dppi_group_handle_t handle)
{
	return nrf_dppi_task_address_get(NRF_DPPIC, nrf_dppi_group_disable_task_get(handle));
}

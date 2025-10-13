/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOC_NORDIC_COMMON_PPI_NRFX_GPPI_H_
#define SOC_NORDIC_COMMON_PPI_NRFX_GPPI_H_

#include <nrf.h>
#include <zephyr/kernel.h>

typedef uint32_t nrfx_gppi_handle_t;
typedef uint32_t nrfx_gppi_group_handle_t;

typedef struct {
#if defined(CONFIG_NORDIC_DPPI_MULTI_DOMAIN)
	const struct nrf_dppi_route *routes;
	const struct nrf_dppi_route ***route_map;
#else
	atomic_t ch_mask;
	atomic_t group_mask;
#endif
} nrfx_gppi_t;

/** @brief Get the domain ID from the group handle.
 *
 * @param handle Group handle.
 *
 * @return Domain ID.
 */
#ifdef CONFIG_NORDIC_DPPI_MULTI_DOMAIN
uint32_t nrfx_gppi_group_domain_id(nrfx_gppi_group_handle_t handle);
#else
static inline uint32_t nrfx_gppi_group_domain_id(nrfx_gppi_group_handle_t handle)
{
	return 0;
}
#endif

/** @brief Get the domain ID from the peripheral register address.
 *
 * @param addr Address.
 *
 * @return Domain ID.
 */
#ifdef CONFIG_NORDIC_DPPI_MULTI_DOMAIN
uint32_t nrfx_gppi_get_domain_id(uint32_t addr);
#else
static inline uint32_t nrfx_gppi_get_domain_id(uint32_t addr) {
	return 0;
}
#endif

/** @brief Get the channel that is used for an endpoint.
 *
 * @param ep Endpoint address (task, event, publish or subscribe register).
 * @retval non-negative Configured channel.
 * @retval -EINVAL endpoint does not have channel.
 */
#ifdef CONFIG_NORDIC_GPPI_PPI
int nrfx_gppi_ep_channel(uint32_t ep);
#else
static inline int nrfx_gppi_ep_channel(uint32_t ep)
{
	uint32_t sub_pub;
#if defined(NRF_RADIO) && defined(RADIO_SUBSCRIBE_TXEN_ResetValue)
	if ((NRFX_OFFSETOF(NRF_RADIO_Type, SUBSCRIBE_TXEN) != 0x80) &&
	    NRFX_IN_RANGE(ep, (uint32_t)NRF_RADIO, (uint32_t)NRF_RADIO + sizeof(NRF_RADIO_Type))) {
		sub_pub = (ep & BIT(8)) ? ep : (ep + offsetof(NRF_RADIO_Type, SUBSCRIBE_TXEN));
	} else {
		sub_pub = ((ep >> 4) & (0xF >= 8)) ? ep : (ep + 0x80);
	}
#else
	sub_pub = ((ep >> 4) & (0xF >= 8)) ? ep : (ep + 0x80);
#endif
	uint32_t val = *(volatile uint32_t *)sub_pub;

	return (val & BIT(31)) ? (val & 0xFF) : -EINVAL;
}
#endif

/** @brief Allocate and setup a connection between two domains.
 *
 * @param[in]  p_gppi GPPI instance.
 * @param[in]  producer Domain that will produce (publish) events.
 * @param[in]  consumer Domain that will consume (subsribe to) events.
 * @param[out] p_handle Handle used to control the connection.
 *
 * @retval 0 on successful connection allocation.
 * @retval -ENOMEM if there is not enough resources to allocate the connection.
 */
int nrfx_gppi_domain_conn_alloc(nrfx_gppi_t * p_gppi, uint32_t producer,
			   uint32_t consumer, nrfx_gppi_handle_t * p_handle);

/** @brief Attach an endpoint to the connection.
 *
 * Endpoint can be task or event. In order to allow attaching the endpoint it must
 * belong to the route that connection is using.
 *
 * @param p_gppi GPPI instance.
 * @param handle Connection handle.
 * @param ep Endpoint.
 *
 * @retval 0 Endpoint attached.
 * @retval -EINVAL Endpoint does not belong to the route and cannot be attached.
 */
int nrfx_gppi_ep_attach(nrfx_gppi_t * p_gppi, nrfx_gppi_handle_t handle, uint32_t ep);

/** @brief Allocate a connection between Task and Event.
 *
 * @param[in]  p_gppi GPPI instance.
 * @param[in]  eep Event endpoint.
 * @param[in]  tep Task endpoint.
 * @param[out] p_handle Handle used to control the connection.
 *
 * @retval 0 on successful connection allocation.
 * @retval -ENOMEM if there is not enough resources to allocate the connection.
 */
static inline int nrfx_gppi_conn_alloc(nrfx_gppi_t * p_gppi, uint32_t eep, uint32_t tep,
				       nrfx_gppi_handle_t * p_handle)
{
	int rv;

	rv = nrfx_gppi_domain_conn_alloc(p_gppi, nrfx_gppi_get_domain_id(eep),
					nrfx_gppi_get_domain_id(tep), p_handle);
	if (rv < 0) {
		return rv;
	}

	nrfx_gppi_ep_attach(p_gppi, *p_handle, eep);
	nrfx_gppi_ep_attach(p_gppi, *p_handle, tep);

	return 0;
}

/** @brief Clear and free connection between domains.
 *
 * For connection within a single domain that is no-op. For cross-domain connection
 * channels in bridges (PPIB) are cleared. Connection shall be disabled prior to clearing.
 *
 * @param p_gppi GPPI instance.
 * @param handle Connection handle.
 */
void nrfx_gppi_domain_conn_free(nrfx_gppi_t * p_gppi, nrfx_gppi_handle_t handle);

/** @brief Clear endpoint.
 *
 * Remove endpoint from DPPI channel.
 *
 * @param ep Endpoint.
 */
void nrfx_gppi_ep_clear(uint32_t ep);

/** @brief Clear and free the connection.
 *
 * Connection shall be disabled prior to clearing. Clear endpoints and bridges setup in
 * the connection. Free allocated channels.
 *
 * @param p_gppi GPPI instance.
 * @param eep Event endpoint.
 * @param tep Task endpoint.
 * @param handle Connection handle.
 */
#ifdef PPI_PRESENT
void nrfx_gppi_conn_free(nrfx_gppi_t * p_gppi, uint32_t eep, uint32_t tep, nrfx_gppi_handle_t handle);
#else
static inline void nrfx_gppi_conn_free(nrfx_gppi_t * p_gppi, uint32_t eep, uint32_t tep,
				       nrfx_gppi_handle_t handle)
{
	nrfx_gppi_ep_clear(eep);
	nrfx_gppi_ep_clear(tep);
	nrfx_gppi_domain_conn_free(p_gppi, handle);
}
#endif

/** @brief Enable a connection.
 *
 * @param p_gppi GPPI instance.
 * @param handle Connection handle.
 * @param enable True to enable all (D)PPI channels in the connection. False to disable all.
 */
void nrfx_gppi_conn_enable(nrfx_gppi_t * p_gppi, nrfx_gppi_handle_t handle);

/** @brief Disable a connection.
 *
 * @param p_gppi GPPI instance.
 * @param handle Connection handle.
 */
void nrfx_gppi_conn_disable(nrfx_gppi_t * p_gppi, nrfx_gppi_handle_t handle);

/** @brief Enable a channel in the domain.
 *
 * @param p_gppi GPPI instance.
 * @param handle Connection handle.
 */
void nrfx_gppi_chan_enable(nrfx_gppi_t * p_gppi, uint32_t domain_id, uint32_t ch);

/** @brief Enable a channel used by the endpoint.
 *
 * @param p_gppi GPPI instance.
 * @param ep Endpoint.
 *
 * @retval 0 successful operation.
 * @retval -EINVAL Endpoint has no channel.
 */
static inline int nrfx_gppi_ep_enable(nrfx_gppi_t * p_gppi, uint32_t ep)
{
	int ch = nrfx_gppi_ep_channel(ep);

	if (ch < 0) {
		return -EINVAL;
	}
	nrfx_gppi_chan_enable(p_gppi, nrfx_gppi_get_domain_id(ep), ch);
	return 0;
}

/** @brief Disable a channel in the domain.
 *
 * @param p_gppi GPPI instance.
 * @param handle Connection handle.
 */
void nrfx_gppi_chan_disable(nrfx_gppi_t * p_gppi, uint32_t domain_id, uint32_t ch);

/** @brief Disable a channel used by the endpoint.
 *
 * @param p_gppi GPPI instance.
 * @param ep Endpoint.
 *
 * @retval 0 successful operation.
 * @retval -EINVAL Endpoint has no channel.
 */
static inline int nrfx_gppi_ep_disable(nrfx_gppi_t * p_gppi, uint32_t ep)
{
	int ch = nrfx_gppi_ep_channel(ep);

	if (ch < 0) {
		return -EINVAL;
	}
	nrfx_gppi_chan_disable(p_gppi, nrfx_gppi_get_domain_id(ep), ch);
	return 0;
}

/** @brief Allocate group for given endpoints.
 *
 * Group can only enable or disable channels in a single DPPIC so all endpoints must
 * belong to a single domain. Provided endpoints must be set up prior to group allocation
 * as channels are retrieved from endpoints.
 *
 * @param[in] p_gppi GPPI instance.
 * @param[in] ep Array with endpoint addresses.
 * @param[in] ep_cnt Number of endpoint addresses.
 * @param[out] p_handle Location where handle is written.
 *
 * @retval 0 Successful allocation of a group. @p handle can be used.
 * @retval negative Failed to allocate.
 */
int nrfx_gppi_group_alloc(nrfx_gppi_t * p_gppi, uint32_t *ep, size_t ep_cnt,
			  nrfx_gppi_group_handle_t * p_handle);

/* Release group.
 *
 * @param p_gppi GPPI instance.
 * @param handle Group handle.
 */
void nrfx_gppi_group_free(nrfx_gppi_t * p_gppi, nrfx_gppi_group_handle_t handle);

/** @brief Add a channel to a group.
 *
 * @param p_gppi GPPI instance.
 * @param handle Group handle.
 * @param channel Channel.
 */
void nrfx_gppi_group_ch_add(nrfx_gppi_t * p_gppi, nrfx_gppi_group_handle_t handle, uint32_t channel);

/** @brief Remove a channel from a group.
 *
 * @param p_gppi GPPI instance.
 * @param handle Group handle.
 * @param channel Channel.
 */
void nrfx_gppi_group_ch_remove(nrfx_gppi_t * p_gppi, nrfx_gppi_group_handle_t handle, uint32_t channel);

/** @brief Add a configured endpoint to a group.
 *
 * Endpoint must be from the same domain as group and it must be already configured to
 * a channel.
 *
 * @param p_gppi GPPI instance.
 * @param handle Group handle.
 * @param ep Endpoint.
 *
 * @retval 0 Successful operation.
 * @retval negative Failed to extend the group.
 */
static inline int nrfx_gppi_group_ep_add(nrfx_gppi_t * p_gppi, nrfx_gppi_group_handle_t handle, uint32_t ep)
{
	if (nrfx_gppi_group_domain_id(handle) != nrfx_gppi_get_domain_id(ep)) {
		return -EINVAL;
	}

	nrfx_gppi_group_ch_add(p_gppi, handle, nrfx_gppi_ep_channel(ep));
	return 0;
}

/** @brief Remove a configured endpoint from a group.
 *
 * Endpoint must be from the same domain as group and it must be already configured to
 * a channel.
 *
 * @param p_gppi GPPI instance.
 * @param handle Group handle.
 * @param ep Endpoint.
 *
 * @retval 0 Successful operation.
 * @retval negative Failed to extend the group.
 */
static inline int nrfx_gppi_group_ep_remove(nrfx_gppi_t * p_gppi, nrfx_gppi_group_handle_t handle, uint32_t ep)
{
	if (nrfx_gppi_group_domain_id(handle) != nrfx_gppi_get_domain_id(ep)) {
		return -EINVAL;
	}

	nrfx_gppi_group_ch_remove(p_gppi, handle, nrfx_gppi_ep_channel(ep));
	return 0;
}

/** @brief Enable a group.
 *
 * @param p_gppi GPPI instance.
 * @param handle Group handle.
 */
void nrfx_gppi_group_en(nrfx_gppi_t * p_gppi, nrfx_gppi_group_handle_t handle);

/** @brief Disable a group.
 *
 * @param p_gppi GPPI instance.
 * @param handle Group handle.
 */
void nrfx_gppi_group_dis(nrfx_gppi_t * p_gppi, nrfx_gppi_group_handle_t handle);

/** @brief Get enable group task address.
 *
 * @param p_gppi GPPI instance.
 * @param handle Group handle.
 *
 * @retval Address of the task register.
 */
uint32_t nrfx_gppi_group_task_en_addr(nrfx_gppi_t * p_gppi, nrfx_gppi_group_handle_t handle);

/** @brief Get disable group task address.
 *
 * @param p_gppi GPPI instance.
 * @param handle Group handle.
 *
 * @retval Address of the task register.
 */
uint32_t nrfx_gppi_group_task_dis_addr(nrfx_gppi_t * p_gppi, nrfx_gppi_group_handle_t handle);

#endif /* SOC_NORDIC_COMMON_PPI_NRFX_GPPI_H_ */

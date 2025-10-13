/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOC_NORDIC_COMMON_PPI_GPPI_H_
#define SOC_NORDIC_COMMON_PPI_GPPI_H_

#include <nrf.h>
#include <ppi/nrfx_gppi.h>
#include <zephyr/kernel.h>

typedef nrfx_gppi_handle_t gppi_handle_t;
typedef nrfx_gppi_group_handle_t gppi_group_handle_t;

extern nrfx_gppi_t gppi_instance;

/** @brief Get the domain ID from the group handle.
 *
 * @param handle Group handle.
 *
 * @return Domain ID.
 */
static inline uint32_t gppi_group_domain_id(gppi_group_handle_t handle)
{
	return nrfx_gppi_group_domain_id(handle);
}

/** @brief Get the domain ID from the peripheral register address.
 *
 * @param addr Address.
 *
 * @return Domain ID.
 */
static inline uint32_t gppi_get_domain_id(uint32_t addr) {
	return nrfx_gppi_get_domain_id(addr);
}

/** @brief Get the channel that is used for an endpoint.
 *
 * @param ep Endpoint address (task, event, publish or subscribe register).
 * @retval non-negative Configured channel.
 * @retval -EINVAL endpoint does not have channel.
 */
static inline int gppi_ep_channel(uint32_t ep)
{
	return nrfx_gppi_ep_channel(ep);
}

/** @brief Allocate and setup a connection between two domains.
 *
 * @param[in]  prod Domain that will produce (publish) events.
 * @param[in]  cons Domain that will consume (subsribe to) events.
 * @param[out] handle Handle used to control the connection.
 *
 * @retval 0 on successful connection allocation.
 * @retval -ENOMEM if there is not enough resources to allocate the connection.
 */
static inline int gppi_domain_conn_alloc(uint32_t prod, uint32_t cons, gppi_handle_t *handle)
{
	return nrfx_gppi_domain_conn_alloc(&gppi_instance, prod, cons, handle);
}

/** @brief Attach an endpoint to the connection.
 *
 * Endpoint can be task or event. In order to allow attaching the endpoint it must
 * belong to the route that connection is using.
 *
 * @param handle Connection handle.
 * @param ep Endpoint.
 *
 * @retval 0 Endpoint attached.
 * @retval -EINVAL Endpoint does not belong to the route and cannot be attached.
 */
static inline int gppi_ep_attach(gppi_handle_t handle, uint32_t ep)
{
	return nrfx_gppi_ep_attach(&gppi_instance, handle, ep);
}

/** @brief Allocate a connection between Task and Event.
 *
 * @param[in]  eep Event endpoint.
 * @param[in]  tep Task endpoint.
 * @param[out] handle Handle used to control the connection.
 *
 * @retval 0 on successful connection allocation.
 * @retval -ENOMEM if there is not enough resources to allocate the connection.
 */
static inline int gppi_conn_alloc(uint32_t eep, uint32_t tep, gppi_handle_t *handle)
{
	return nrfx_gppi_conn_alloc(&gppi_instance, eep, tep, handle);
}

/** @brief Enable a connection.
 *
 * @param handle Connection handle.
 * @param enable True to enable all (D)PPI channels in the connection. False to disable all.
 */
static inline void gppi_conn_enable(gppi_handle_t handle)
{
	nrfx_gppi_conn_enable(&gppi_instance, handle);
}

/** @brief Disable a connection.
 *
 * @param handle Connection handle.
 */
static inline void gppi_conn_disable(gppi_handle_t handle)
{
	nrfx_gppi_conn_disable(&gppi_instance, handle);
}

/** @brief Enable a channel in the domain.
 *
 * @param handle Connection handle.
 */
static inline void gppi_chan_enable(uint32_t domain_id, uint32_t ch)
{
	nrfx_gppi_chan_enable(&gppi_instance, domain_id, ch);
}

/** @brief Enable a channel used by the endpoint.
 *
 * @param ep Endpoint.
 *
 * @retval 0 successful operation.
 * @retval -EINVAL Endpoint has no channel.
 */
static inline int gppi_ep_enable(uint32_t ep)
{
	return nrfx_gppi_ep_enable(&gppi_instance, ep);
}

/** @brief Disable a channel in the domain.
 *
 * @param handle Connection handle.
 */
static inline void gppi_chan_disable(uint32_t domain_id, uint32_t ch)
{
	nrfx_gppi_chan_disable(&gppi_instance, domain_id, ch);
}

/** @brief Disable a channel used by the endpoint.
 *
 * @param ep Endpoint.
 *
 * @retval 0 successful operation.
 * @retval -EINVAL Endpoint has no channel.
 */
static inline int gppi_ep_disable(uint32_t ep)
{
	return nrfx_gppi_ep_disable(&gppi_instance, ep);
}

/** @brief Clear and free connection between domains.
 *
 * For connection within a single domain that is no-op. For cross-domain connection
 * channels in bridges (PPIB) are cleared. Connection shall be disabled prior to clearing.
 *
 * @param handle Connection handle.
 */
static inline void gppi_domain_conn_free(gppi_handle_t handle)
{
	nrfx_gppi_domain_conn_free(&gppi_instance, handle);
}

/** @brief Clear endpoint.
 *
 * Remove endpoint from DPPI channel.
 *
 * @param ep Endpoint.
 */
static inline void gppi_ep_clear(uint32_t ep)
{
	nrfx_gppi_ep_clear(ep);
}

/** @brief Clear and free the connection.
 *
 * Connection shall be disabled prior to clearing. Clear endpoints and bridges setup in
 * the connection. Free allocated channels.
 *
 * @param eep Event endpoint.
 * @param tep Task endpoint.
 * @param handle Connection handle.
 */
static inline void gppi_conn_free(uint32_t eep, uint32_t tep, gppi_handle_t handle)
{
	nrfx_gppi_conn_free(&gppi_instance, eep, tep, handle);
}

/** @brief Allocate group for given endpoints.
 *
 * Group can only enable or disable channels in a single DPPIC so all endpoints must
 * belong to a single domain. Provided endpoints must be set up prior to group allocation
 * as channels are retrieved from endpoints.
 *
 * @param[in] ep Array with endpoint addresses.
 * @param[in] ep_cnt Number of endpoint addresses.
 * @param[out] Location where handle is written.
 *
 * @retval 0 Successful allocation of a group. @p handle can be used.
 * @retval negative Failed to allocate.
 */
static inline int gppi_group_alloc(uint32_t *ep, size_t ep_cnt, gppi_group_handle_t *handle)
{
	return nrfx_gppi_group_alloc(&gppi_instance, ep, ep_cnt, handle);
}

/** @brief Add a channel to a group.
 *
 * @param handle Group handle.
 * @param channel Channel.
 */
static inline void gppi_group_ch_add(gppi_group_handle_t handle, uint32_t channel)
{
	nrfx_gppi_group_ch_add(&gppi_instance, handle, channel);
}

/** @brief Remove a channel from a group.
 *
 * @param handle Group handle.
 * @param channel Channel.
 */
static inline void gppi_group_ch_remove(gppi_group_handle_t handle, uint32_t channel)
{
	nrfx_gppi_group_ch_remove(&gppi_instance, handle, channel);
}

/** @brief Add a configured endpoint to a group.
 *
 * Endpoint must be from the same domain as group and it must be already configured to
 * a channel.
 *
 * @param handle Group handle.
 * @param ep Endpoint.
 *
 * @retval 0 Successful operation.
 * @retval negative Failed to extend the group.
 */
static inline int gppi_group_ep_add(gppi_group_handle_t handle, uint32_t ep)
{
	return nrfx_gppi_group_ep_add(&gppi_instance, handle, ep);
}

/** @brief Remove a configured endpoint from a group.
 *
 * Endpoint must be from the same domain as group and it must be already configured to
 * a channel.
 *
 * @param handle Group handle.
 * @param ep Endpoint.
 *
 * @retval 0 Successful operation.
 * @retval negative Failed to extend the group.
 */
static inline int gppi_group_ep_remove(gppi_group_handle_t handle, uint32_t ep)
{
	return nrfx_gppi_group_ep_remove(&gppi_instance, handle, ep);
}

/** @brief Enable a group.
 *
 * @param handle Group handle.
 */
static inline void gppi_group_en(gppi_group_handle_t handle)
{
	nrfx_gppi_group_en(&gppi_instance, handle);
}

/** @brief Disable a group.
 *
 * @param handle Group handle.
 */
static inline void gppi_group_dis(gppi_group_handle_t handle)
{
	nrfx_gppi_group_dis(&gppi_instance, handle);
}

/** @brief Get enable group task address.
 *
 * @param handle Group handle.
 *
 * @retval Address of the task register.
 */
static inline uint32_t gppi_group_task_en_addr(gppi_group_handle_t handle)
{
	return nrfx_gppi_group_task_en_addr(&gppi_instance, handle);
}

/** @brief Get disable group task address.
 *
 * @param handle Group handle.
 *
 * @retval Address of the task register.
 */
static inline uint32_t gppi_group_task_dis_addr(gppi_group_handle_t handle)
{
	return nrfx_gppi_group_task_dis_addr(&gppi_instance, handle);
}

/* Release group.
 *
 * @param handle Group handle.
 */
static inline void gppi_group_free(gppi_group_handle_t handle)
{
	nrfx_gppi_group_free(&gppi_instance, handle);
}

#endif /* SOC_NORDIC_COMMON_PPI_GPPI_H_ */


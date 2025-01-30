#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_PPI_NRFX_DPPI_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_PPI_NRFX_DPPI_H_

#include <nrf.h>
#include <zephyr/kernel.h>

typedef uint32_t nrf_dppi_handle_t;
typedef uint32_t nrf_dppi_group_handle_t;


/** @brief Get the domain ID from the group handle.
 *
 * @param handle Group handle.
 *
 * @return Domain ID.
 */
#ifdef CONFIG_NORDIC_DPPI_MULTI_DOMAIN
uint32_t nrf_dppi_group_domain_id(nrf_dppi_group_handle_t handle);
#else
static inline uint32_t nrf_dppi_group_domain_id(nrf_dppi_group_handle_t handle)
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
uint32_t nrf_dppi_get_domain_id(uint32_t addr);
#else
static inline uint32_t nrf_dppi_get_domain_id(uint32_t addr) {
	return 0;
}
#endif

/** @brief Get the channel that is used for an endpoint.
 *
 * @param ep Endpoint address (task, event, publish or subscribe register).
 * @retval non-negative Configured channel.
 * @retval -EINVAL endpoint does not have channel.
 */
static inline int nrf_dppi_ep_channel(uint32_t ep)
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

/** @brief Allocate and setup a connection between two domains.
 *
 * @param[in]  producer Domain that will produce (publish) events.
 * @param[in]  consumer Domain that will consume (subsribe to) events.
 * @param[out] handle Handle used to control the connection.
 *
 * @retval 0 on successful connection allocation.
 * @retval -ENOMEM if there is not enough resources to allocate the connection.
 */
int nrf_dppi_domain_conn_alloc(uint32_t producer, uint32_t consumer, nrf_dppi_handle_t *handle);

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
int nrf_dppi_ep_attach(nrf_dppi_handle_t handle, uint32_t ep);

/** @brief Allocate a connection between Task and Event.
 *
 * @param[in]  eep Event endpoint.
 * @param[in]  tep Task endpoint.
 * @param[out] handle Handle used to control the connection.
 *
 * @retval 0 on successful connection allocation.
 * @retval -ENOMEM if there is not enough resources to allocate the connection.
 */
static inline int nrf_dppi_conn_alloc(uint32_t eep, uint32_t tep, nrf_dppi_handle_t *handle)
{
	int ret = nrf_dppi_domain_conn_alloc(nrf_dppi_get_domain_id(eep),
					     nrf_dppi_get_domain_id(tep),
					     handle);

	if (ret < 0) {
		return ret;
	}

	nrf_dppi_ep_attach(*handle, eep);
	nrf_dppi_ep_attach(*handle, tep);

	return 0;
}

/** @brief Enable a connection.
 *
 * @param handle Connection handle.
 * @param enable True to enable all (D)PPI channels in the connection. False to disable all.
 */
void nrf_dppi_conn_enable(nrf_dppi_handle_t handle);

/** @brief Disable a connection.
 *
 * @param handle Connection handle.
 */
void nrf_dppi_conn_disable(nrf_dppi_handle_t handle);

/** @brief Enable a channel in the domain.
 *
 * @param handle Connection handle.
 */
void nrf_dppi_chan_enable(uint32_t domain_id, uint32_t ch);

/** @brief Enable a channel used by the endpoint.
 *
 * @param ep Endpoint.
 *
 * @retval 0 successful operation.
 * @retval -EINVAL Endpoint has no channel.
 */
static inline int nrf_dppi_ep_enable(uint32_t ep)
{
	int ch = nrf_dppi_ep_channel(ep);

	if (ch < 0) {
		return -EINVAL;
	}
	nrf_dppi_chan_enable(nrf_dppi_get_domain_id(ep), ch);
	return 0;
}

/** @brief Disable a channel in the domain.
 *
 * @param handle Connection handle.
 */
void nrf_dppi_chan_disable(uint32_t domain_id, uint32_t ch);

/** @brief Disable a channel used by the endpoint.
 *
 * @param ep Endpoint.
 *
 * @retval 0 successful operation.
 * @retval -EINVAL Endpoint has no channel.
 */
static inline int nrf_dppi_ep_disable(uint32_t ep)
{
	int ch = nrf_dppi_ep_channel(ep);

	if (ch < 0) {
		return -EINVAL;
	}
	nrf_dppi_chan_disable(nrf_dppi_get_domain_id(ep), ch);
	return 0;
}

/** @brief Clear and free connection between domains.
 *
 * For connection within a single domain that is no-op. For cross-domain connection
 * channels in bridges (PPIB) are cleared. Connection shall be disabled prior to clearing.
 *
 * @param handle Connection handle.
 */
void nrf_dppi_domain_conn_free(nrf_dppi_handle_t handle);

/** @brief Clear endpoint.
 *
 * Remove endpoint from DPPI channel.
 *
 * @param ep Endpoint.
 */
void nrf_dppi_ep_clear(uint32_t ep);

/** @brief Clear and free the connection.
 *
 * Connection shall be disabled prior to clearing. Clear endpoints and bridges setup in
 * the connection. Free allocated channels.
 *
 * @param eep Event endpoint.
 * @param tep Task endpoint.
 * @param handle Connection handle.
 */
static inline void nrf_dppi_conn_free(uint32_t eep, uint32_t tep, nrf_dppi_handle_t handle)
{
	nrf_dppi_ep_clear(eep);
	nrf_dppi_ep_clear(tep);
	nrf_dppi_domain_conn_free(handle);
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
int nrf_dppi_group_alloc(uint32_t *ep, size_t ep_cnt, nrf_dppi_group_handle_t *handle);

/** @brief Add a channel to a group.
 *
 * @param handle Group handle.
 * @param channel Channel.
 */
void nrf_dppi_group_ch_add(nrf_dppi_group_handle_t handle, uint32_t channel);

/** @brief Remove a channel from a group.
 *
 * @param handle Group handle.
 * @param channel Channel.
 */
void nrf_dppi_group_ch_remove(nrf_dppi_group_handle_t handle, uint32_t channel);

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
static inline int nrf_dppi_group_ep_add(nrf_dppi_group_handle_t handle, uint32_t ep)
{
	if (nrf_dppi_group_domain_id(handle) != nrf_dppi_get_domain_id(ep)) {
		return -EINVAL;
	}

	nrf_dppi_group_ch_add(handle, nrf_dppi_ep_channel(ep));
	return 0;
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
static inline int nrf_dppi_group_ep_remove(nrf_dppi_group_handle_t handle, uint32_t ep)
{
	if (nrf_dppi_group_domain_id(handle) != nrf_dppi_get_domain_id(ep)) {
		return -EINVAL;
	}

	nrf_dppi_group_ch_remove(handle, nrf_dppi_ep_channel(ep));
	return 0;
}

/** @brief Enable a group.
 *
 * @param handle Group handle.
 */
void nrf_dppi_group_en(nrf_dppi_group_handle_t handle);

/** @brief Disable a group.
 *
 * @param handle Group handle.
 */
void nrf_dppi_group_dis(nrf_dppi_group_handle_t handle);

/** @brief Get enable group task address.
 *
 * @param handle Group handle.
 *
 * @retval Address of the task register.
 */
uint32_t nrf_dppi_group_task_en_addr(nrf_dppi_group_handle_t handle);

/** @brief Get disable group task address.
 *
 * @param handle Group handle.
 *
 * @retval Address of the task register.
 */
uint32_t nrf_dppi_group_task_dis_addr(nrf_dppi_group_handle_t handle);

/* Release group.
 *
 * @param handle Group handle.
 */
void nrf_dppi_group_free(nrf_dppi_group_handle_t handle);

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_PPI_NRFX_DPPI_H_ */

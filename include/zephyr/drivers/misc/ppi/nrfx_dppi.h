#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_PPI_NRFX_DPPI_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_PPI_NRFX_DPPI_H_

#include <nrf.h>
#include <zephyr/kernel.h>

typedef uint32_t nrf_dppi_handle_t;
typedef uint32_t nrf_dppi_group_handle_t;

uint32_t nrf_dppi_get_domain_id(uint32_t addr);

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

	(void)nrf_dppi_ep_attach(*handle, eep);
	(void)nrf_dppi_ep_attach(*handle, tep);

	return 0;
}

/** @brief Enable of disable connection.
 *
 * @param handle Connection handle.
 * @param enable True to enable all (D)PPI channels in the connection. False to disable all.
 */
void nrf_dppi_conn_ctrl(nrf_dppi_handle_t handle, bool enable);

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

/** @brief Add or remote channel from a group.
 *
 * Endpoint must be from the same domain as group.
 *
 * @param handle Group handle.
 * @param ep Endpoint.
 * @param add True to add endpoint and false to remove.
 *
 * @retval 0 Successful operation.
 * @retval negative Failed to extend the group.
 */
int nrf_dppi_group_modify(nrf_dppi_group_handle_t handle, uint32_t ep, bool add);

/** @brief Enable or disable channels in the group.
 *
 * @param handle Group handle.
 * @param enable True to enable or false to disable.
 */
void nrf_dppi_group_ctrl(nrf_dppi_group_handle_t handle, bool enable);

/** @brief Get subscriber endpoint that can be used to control channels in the group.
 *
 * @param handle Group handle.
 * @param enable True to get endpoint for enabling and false for disabling.
 *
 * @retval Address of subscribe register.
 */
uint32_t nrf_dppi_group_ep(nrf_dppi_group_handle_t handle, bool enable);

/* Release group.
 *
 * @param handle Group handle.
 */
void nrf_dppi_group_free(nrf_dppi_group_handle_t handle);

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_PPI_NRFX_DPPI_H_ */

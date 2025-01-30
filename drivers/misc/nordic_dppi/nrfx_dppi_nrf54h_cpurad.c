#include <zephyr/drivers/misc/ppi/nrfx_dppi.h>
#include "nrfx_dppi_nrf54h.h"

enum nrf_dppi_node_id {
	NRF_DPPI_NODE_DPPIC020,
	NRF_DPPI_NODE_DPPIC030,
	NRF_DPPI_NODE_PPIB020_030,
};

/* Available channels for each node. */
static atomic_t channels[] = {
	[NRF_DPPI_NODE_DPPIC020] = 0xffff,
	[NRF_DPPI_NODE_DPPIC030] = 0xffff,
	[NRF_DPPI_NODE_PPIB020_030] = 0xffff,
};

/* All nodes in the system. */
static const struct nrf_dppi_node nodes[] = {
	DPPIC_NODE_DEFINE(020, NRF_DPPI_DOMAIN_APB2),
	DPPIC_NODE_DEFINE(030, NRF_DPPI_DOMAIN_APB3),
	PPIB_NODE_DEFINE(020,030),
};

/* All routes in the system. */
const struct nrf_dppi_route dppi_routes[] = {
	NRF_DPPI_ROUTE_DEFINE("apb2", NRF_DPPI_DOMAIN_APB2, (&nodes[NRF_DPPI_NODE_DPPIC020])),
	NRF_DPPI_ROUTE_DEFINE("apb3", NRF_DPPI_DOMAIN_APB3, (&nodes[NRF_DPPI_NODE_DPPIC030])),
	NRF_DPPI_ROUTE_DEFINE("apb2_apb3", NRF_DPPI_DOMAIN_APB2,
			(&nodes[NRF_DPPI_NODE_DPPIC020],
			 &nodes[NRF_DPPI_NODE_PPIB020_030],
			 &nodes[NRF_DPPI_NODE_DPPIC030])),
};

/* Helper arrays to find route based on source and destination domain ID.
 * Since domain index starts from 1 everything is shifted by 1 to save
 * space in arrays.
 */
static const struct nrf_dppi_route *apb2_routes[] = {
	&dppi_routes[0], &dppi_routes[2]
};

static const struct nrf_dppi_route *apb3_routes[] = {
	&dppi_routes[2], &dppi_routes[1]
};

const struct nrf_dppi_route **dppi_route_map[] = {
	apb2_routes, apb3_routes
};

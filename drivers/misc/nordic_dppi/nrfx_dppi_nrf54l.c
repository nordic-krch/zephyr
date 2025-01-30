#include <zephyr/drivers/misc/ppi/nrfx_dppi.h>
#include "nrfx_dppi_routes.h"
#include "nrfx_dppi_nrf54l.h"

/* All nodes in the system. */
enum nrf_dppi_node_id {
	NRF_DPPI_NODE_DPPIC00,
	NRF_DPPI_NODE_DPPIC10,
	NRF_DPPI_NODE_DPPIC20,
	NRF_DPPI_NODE_DPPIC30,
	NRF_DPPI_NODE_PPIB10_00,
	NRF_DPPI_NODE_PPIB11_21,
	NRF_DPPI_NODE_PPIB01_20,
	NRF_DPPI_NODE_PPIB22_30,
};

/* Available channels for each node. */
static atomic_t channels[] = {
	[NRF_DPPI_NODE_DPPIC00] = 0xff,
	[NRF_DPPI_NODE_DPPIC10] = 0xffffff,
	[NRF_DPPI_NODE_DPPIC20] = 0xffff,
	[NRF_DPPI_NODE_DPPIC30] = 0xf,
	[NRF_DPPI_NODE_PPIB10_00] = 0xff,
	[NRF_DPPI_NODE_PPIB11_21] = 0xffff,
	[NRF_DPPI_NODE_PPIB01_20] = 0xff,
	[NRF_DPPI_NODE_PPIB22_30] = 0xf,
};

static atomic_t group_channels[] = {
	[NRF_DPPI_NODE_DPPIC00] = BIT_MASK(2),
	[NRF_DPPI_NODE_DPPIC10] = BIT_MASK(6),
	[NRF_DPPI_NODE_DPPIC20] = BIT_MASK(6),
	[NRF_DPPI_NODE_DPPIC30] = BIT_MASK(2),
};

/* All nodes in the system. */
static const struct nrf_dppi_node nodes[] = {
	DPPIC_NODE_DEFINE(00, DPPI_LUMOS_DOMAIN_MCU),
	DPPIC_NODE_DEFINE(10, DPPI_LUMOS_DOMAIN_RAD),
	DPPIC_NODE_DEFINE(20, DPPI_LUMOS_DOMAIN_PERI),
	DPPIC_NODE_DEFINE(30, DPPI_LUMOS_DOMAIN_LP),
	PPIB_NODE_DEFINE(10,00),
	PPIB_NODE_DEFINE(11,21),
	PPIB_NODE_DEFINE(01,20),
	PPIB_NODE_DEFINE(22,30),
};

/* All routes in the system. */
const struct nrf_dppi_route dppi_routes[] = {
	NRF_DPPI_ROUTE_DEFINE("mcu", DPPI_LUMOS_DOMAIN_MCU, (&nodes[NRF_DPPI_NODE_DPPIC00])),
	NRF_DPPI_ROUTE_DEFINE("rad", DPPI_LUMOS_DOMAIN_RAD, (&nodes[NRF_DPPI_NODE_DPPIC10])),
	NRF_DPPI_ROUTE_DEFINE("peri", DPPI_LUMOS_DOMAIN_PERI, (&nodes[NRF_DPPI_NODE_DPPIC20])),
	NRF_DPPI_ROUTE_DEFINE("lp", DPPI_LUMOS_DOMAIN_LP, (&nodes[NRF_DPPI_NODE_DPPIC30])),
	NRF_DPPI_ROUTE_DEFINE("rad_mcu", DPPI_LUMOS_DOMAIN_RAD,
			(&nodes[NRF_DPPI_NODE_DPPIC10],
			 &nodes[NRF_DPPI_NODE_PPIB10_00],
			 &nodes[NRF_DPPI_NODE_DPPIC00])),
	NRF_DPPI_ROUTE_DEFINE("rad_peri", DPPI_LUMOS_DOMAIN_RAD,
			(&nodes[NRF_DPPI_NODE_DPPIC10],
			 &nodes[NRF_DPPI_NODE_PPIB11_21],
			 &nodes[NRF_DPPI_NODE_DPPIC20])),
	NRF_DPPI_ROUTE_DEFINE("rad_lp", DPPI_LUMOS_DOMAIN_RAD,
			(&nodes[NRF_DPPI_NODE_DPPIC10],
			 &nodes[NRF_DPPI_NODE_PPIB11_21],
			 &nodes[NRF_DPPI_NODE_DPPIC20],
			 &nodes[NRF_DPPI_NODE_PPIB22_30],
			 &nodes[NRF_DPPI_NODE_DPPIC30])),
	NRF_DPPI_ROUTE_DEFINE("mcu_peri", DPPI_LUMOS_DOMAIN_MCU,
			(&nodes[NRF_DPPI_NODE_DPPIC00],
			 &nodes[NRF_DPPI_NODE_PPIB01_20],
			 &nodes[NRF_DPPI_NODE_DPPIC20])),
	NRF_DPPI_ROUTE_DEFINE("mcu_lp", DPPI_LUMOS_DOMAIN_MCU,
			(&nodes[NRF_DPPI_NODE_DPPIC00],
			 &nodes[NRF_DPPI_NODE_PPIB01_20],
			 &nodes[NRF_DPPI_NODE_DPPIC20],
			 &nodes[NRF_DPPI_NODE_PPIB22_30],
			 &nodes[NRF_DPPI_NODE_DPPIC30])),
	NRF_DPPI_ROUTE_DEFINE("peri_lp", DPPI_LUMOS_DOMAIN_PERI,
			(&nodes[NRF_DPPI_NODE_DPPIC20],
			 &nodes[NRF_DPPI_NODE_PPIB22_30],
			 &nodes[NRF_DPPI_NODE_DPPIC30])),
};

/* Helper arrays to find route based on source and destination domain ID.
 * Since domain index starts from 1 everything is shifted by 1 to save
 * space in arrays.
 */
static const struct nrf_dppi_route *rad_routes[] = {
	&dppi_routes[4], &dppi_routes[1], &dppi_routes[5], &dppi_routes[6]
};

static const struct nrf_dppi_route *mcu_routes[] = {
	&dppi_routes[0], &dppi_routes[4], &dppi_routes[7], &dppi_routes[8]
};

static const struct nrf_dppi_route *peri_routes[] = {
	&dppi_routes[7], &dppi_routes[5], &dppi_routes[2] , &dppi_routes[9]
};

static const struct nrf_dppi_route *lp_routes[] = {
	&dppi_routes[8], &dppi_routes[6], &dppi_routes[9], &dppi_routes[3]
};

const struct nrf_dppi_route **dppi_route_map[] = {
	mcu_routes, rad_routes, peri_routes, lp_routes
};

uint32_t nrf_dppi_get_domain_id(uint32_t addr)
{
	return ((addr >> 18) & 0x7) - 1;
}

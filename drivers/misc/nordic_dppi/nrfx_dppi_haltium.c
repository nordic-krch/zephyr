#include <zephyr/drivers/misc/ppi/nrfx_dppi.h>

enum nrf_dppi_node_id {
	NRF_DPPI_NODE_DPPIC120,
	NRF_DPPI_NODE_DPPIC130,
	NRF_DPPI_NODE_DPPIC131,
	NRF_DPPI_NODE_DPPIC132,
	NRF_DPPI_NODE_DPPIC133,
	NRF_DPPI_NODE_DPPIC134,
	NRF_DPPI_NODE_DPPIC135,
	NRF_DPPI_NODE_DPPIC136,

	NRF_DPPI_NODE_PPIB130_132,
	NRF_DPPI_NODE_PPIB130_133,
	NRF_DPPI_NODE_PPIB130_134,
	NRF_DPPI_NODE_PPIB130_135,

	NRF_DPPI_NODE_PPIB131_136,
	NRF_DPPI_NODE_PPIB131_137,
	NRF_DPPI_NODE_PPIB131_121,

	/* Reversed dir. */
	NRF_DPPI_NODE_PPIB132_130,
	NRF_DPPI_NODE_PPIB133_130,
	NRF_DPPI_NODE_PPIB134_130,
	NRF_DPPI_NODE_PPIB135_130,

	NRF_DPPI_NODE_PPIB136_131,
	NRF_DPPI_NODE_PPIB137_131,
	NRF_DPPI_NODE_PPIB121_131,
};

enum nrf_dppi_domain {
	/* Global domain */
	NRF_DPPI_DOMAIN_APB22,
	NRF_DPPI_DOMAIN_APB32,
	NRF_DPPI_DOMAIN_APB38,
	NRF_DPPI_DOMAIN_APB39,
	NRF_DPPI_DOMAIN_APB3A,
	NRF_DPPI_DOMAIN_APB3B,
	NRF_DPPI_DOMAIN_APB3C,
	NRF_DPPI_DOMAIN_APB3D,
	/* Radio domain */
	NRF_DPPI_DOMAIN_APB2,
	NRF_DPPI_DOMAIN_APB3,
};

static uint32_t channels[] = {
	[NRF_DPPI_NODE_DPPIC120] = 0xff,
	[NRF_DPPI_NODE_DPPIC130] = 0xff,
	[NRF_DPPI_NODE_DPPIC131] = 0xff,
	[NRF_DPPI_NODE_DPPIC132] = 0xff,
	[NRF_DPPI_NODE_DPPIC133] = 0xff,
	[NRF_DPPI_NODE_DPPIC134] = 0xff,
	[NRF_DPPI_NODE_DPPIC135] = 0xff,
	[NRF_DPPI_NODE_DPPIC136] = 0xff,

	[NRF_DPPI_NODE_PPIB130_132] = 0xff,
	[NRF_DPPI_NODE_PPIB130_133] = 0xff,
	[NRF_DPPI_NODE_PPIB130_134] = 0xff,
	[NRF_DPPI_NODE_PPIB130_135] = 0xff,

	[NRF_DPPI_NODE_PPIB131_136] = 0xff,
	[NRF_DPPI_NODE_PPIB131_137] = 0xff,
	[NRF_DPPI_NODE_PPIB131_121] = 0xff,
};

static const struct nrf_dppi_node nodes[] = {
	DPPIC_NODE_DEFINE(120),
	DPPIC_NODE_DEFINE(130),
	DPPIC_NODE_DEFINE(131),
	DPPIC_NODE_DEFINE(132),
	DPPIC_NODE_DEFINE(133),
	DPPIC_NODE_DEFINE(134),
	DPPIC_NODE_DEFINE(135),
	DPPIC_NODE_DEFINE(136),

	PPIB_EXT_NODE_DEFINE(130,132, 130_132, 0, 0),
	PPIB_EXT_NODE_DEFINE(130,133, 130_133, 8, 0),
	PPIB_EXT_NODE_DEFINE(130,134, 130_134, 16, 0),
	PPIB_EXT_NODE_DEFINE(130,135, 130_135, 24, 0),
	PPIB_EXT_NODE_DEFINE(131,136, 131_136, 0, 0),
	PPIB_EXT_NODE_DEFINE(131,137, 131_137, 8, 0),
	PPIB_EXT_NODE_DEFINE(131,121, 131_121, 16, 0),

	PPIB_EXT_NODE_DEFINE(132,130, 130_132, 0, 0),
	PPIB_EXT_NODE_DEFINE(133,130, 130_133, 0, 8),
	PPIB_EXT_NODE_DEFINE(134,130, 130_134, 0, 16),
	PPIB_EXT_NODE_DEFINE(135,130, 130_135, 0, 24),
	PPIB_EXT_NODE_DEFINE(136,131, 131_136, 0, 0),
	PPIB_EXT_NODE_DEFINE(137,131, 131_137, 0, 8),
	PPIB_EXT_NODE_DEFINE(121,131, 131_121, 0, 16),
};

const struct nrf_dppi_route dppi_routes[] = {
	/*0*/NRF_DPPI_ROUTE_DEFINE("apb22", NRF_DPPI_DOMAIN_APB22, (&nodes[NRF_DPPI_NODE_DPPIC120])),
	/*1*/NRF_DPPI_ROUTE_DEFINE("apb32", NRF_DPPI_DOMAIN_APB32, (&nodes[NRF_DPPI_NODE_DPPIC130])),
	/*2*/NRF_DPPI_ROUTE_DEFINE("apb38", NRF_DPPI_DOMAIN_APB38, (&nodes[NRF_DPPI_NODE_DPPIC131])),
	/*3*/NRF_DPPI_ROUTE_DEFINE("apb39", NRF_DPPI_DOMAIN_APB39, (&nodes[NRF_DPPI_NODE_DPPIC132])),
	/*4*/NRF_DPPI_ROUTE_DEFINE("apb3a", NRF_DPPI_DOMAIN_APB3A, (&nodes[NRF_DPPI_NODE_DPPIC133])),
	/*5*/NRF_DPPI_ROUTE_DEFINE("apb3b", NRF_DPPI_DOMAIN_APB3B, (&nodes[NRF_DPPI_NODE_DPPIC134])),
	/*6*/NRF_DPPI_ROUTE_DEFINE("apb3c", NRF_DPPI_DOMAIN_APB3C, (&nodes[NRF_DPPI_NODE_DPPIC135])),
	/*7*/NRF_DPPI_ROUTE_DEFINE("apb3d", NRF_DPPI_DOMAIN_APB3D, (&nodes[NRF_DPPI_NODE_DPPIC136])),

	/*8*/NRF_DPPI_ROUTE_DEFINE("apb32_apb22", NRF_DPPI_DOMAIN_APB32,
			(&nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_121],
			 &nodes[NRF_DPPI_NODE_DPPIC120])),
	/*9*/NRF_DPPI_ROUTE_DEFINE("apb32_apb38", NRF_DPPI_DOMAIN_APB32,
			(&nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB130_132],
			 &nodes[NRF_DPPI_NODE_DPPIC131])),
	/*10*/NRF_DPPI_ROUTE_DEFINE("apb32_apb39", NRF_DPPI_DOMAIN_APB32,
			(&nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB130_133],
			 &nodes[NRF_DPPI_NODE_DPPIC132])),
	/*11*/NRF_DPPI_ROUTE_DEFINE("apb32_apb3a", NRF_DPPI_DOMAIN_APB32,
			(&nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB130_134],
			 &nodes[NRF_DPPI_NODE_DPPIC133])),
	/*12*/NRF_DPPI_ROUTE_DEFINE("apb32_apb3b", NRF_DPPI_DOMAIN_APB32,
			(&nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB130_135],
			 &nodes[NRF_DPPI_NODE_DPPIC134])),
	/*13*/NRF_DPPI_ROUTE_DEFINE("apb32_apb3c", NRF_DPPI_DOMAIN_APB32,
			(&nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_136],
			 &nodes[NRF_DPPI_NODE_DPPIC135])),
	/*14*/NRF_DPPI_ROUTE_DEFINE("apb32_apb3d", NRF_DPPI_DOMAIN_APB32,
			(&nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_137],
			 &nodes[NRF_DPPI_NODE_DPPIC136])),

	/*15*/NRF_DPPI_ROUTE_DEFINE("apb38_apb39", NRF_DPPI_DOMAIN_APB38,
			(&nodes[NRF_DPPI_NODE_DPPIC131],
			 &nodes[NRF_DPPI_NODE_PPIB132_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB130_133],
			 &nodes[NRF_DPPI_NODE_DPPIC132])),
	/*16*/NRF_DPPI_ROUTE_DEFINE("apb38_apb3a", NRF_DPPI_DOMAIN_APB38,
			(&nodes[NRF_DPPI_NODE_DPPIC131],
			 &nodes[NRF_DPPI_NODE_PPIB132_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB130_134],
			 &nodes[NRF_DPPI_NODE_DPPIC133])),
	/*17*/NRF_DPPI_ROUTE_DEFINE("apb38_apb3b", NRF_DPPI_DOMAIN_APB38,
			(&nodes[NRF_DPPI_NODE_DPPIC131],
			 &nodes[NRF_DPPI_NODE_PPIB132_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB130_135],
			 &nodes[NRF_DPPI_NODE_DPPIC134])),
	/*18*/NRF_DPPI_ROUTE_DEFINE("apb38_apb3c", NRF_DPPI_DOMAIN_APB38,
			(&nodes[NRF_DPPI_NODE_DPPIC131],
			 &nodes[NRF_DPPI_NODE_PPIB132_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_136],
			 &nodes[NRF_DPPI_NODE_DPPIC135])),
	/*19*/NRF_DPPI_ROUTE_DEFINE("apb38_apb3d", NRF_DPPI_DOMAIN_APB38,
			(&nodes[NRF_DPPI_NODE_DPPIC131],
			 &nodes[NRF_DPPI_NODE_PPIB132_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_137],
			 &nodes[NRF_DPPI_NODE_DPPIC136])),
	/*20*/NRF_DPPI_ROUTE_DEFINE("apb38_apb22", NRF_DPPI_DOMAIN_APB38,
			(&nodes[NRF_DPPI_NODE_DPPIC131],
			 &nodes[NRF_DPPI_NODE_PPIB132_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_121],
			 &nodes[NRF_DPPI_NODE_DPPIC120])),

	/*21*/NRF_DPPI_ROUTE_DEFINE("apb39_apb3a", NRF_DPPI_DOMAIN_APB39,
			(&nodes[NRF_DPPI_NODE_DPPIC132],
			 &nodes[NRF_DPPI_NODE_PPIB133_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB130_134],
			 &nodes[NRF_DPPI_NODE_DPPIC133])),
	/*22*/NRF_DPPI_ROUTE_DEFINE("apb39_apb3b", NRF_DPPI_DOMAIN_APB39,
			(&nodes[NRF_DPPI_NODE_DPPIC132],
			 &nodes[NRF_DPPI_NODE_PPIB133_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB130_135],
			 &nodes[NRF_DPPI_NODE_DPPIC134])),
	/*23*/NRF_DPPI_ROUTE_DEFINE("apb39_apb3c", NRF_DPPI_DOMAIN_APB39,
			(&nodes[NRF_DPPI_NODE_DPPIC132],
			 &nodes[NRF_DPPI_NODE_PPIB133_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_136],
			 &nodes[NRF_DPPI_NODE_DPPIC135])),
	/*24*/NRF_DPPI_ROUTE_DEFINE("apb39_apb3d", NRF_DPPI_DOMAIN_APB39,
			(&nodes[NRF_DPPI_NODE_DPPIC132],
			 &nodes[NRF_DPPI_NODE_PPIB133_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_137],
			 &nodes[NRF_DPPI_NODE_DPPIC136])),
	/*25*/NRF_DPPI_ROUTE_DEFINE("apb39_apb22", NRF_DPPI_DOMAIN_APB39,
			(&nodes[NRF_DPPI_NODE_DPPIC132],
			 &nodes[NRF_DPPI_NODE_PPIB133_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_121],
			 &nodes[NRF_DPPI_NODE_DPPIC120])),

	/*26*/NRF_DPPI_ROUTE_DEFINE("apb3a_apb3b", NRF_DPPI_DOMAIN_APB3A,
			(&nodes[NRF_DPPI_NODE_DPPIC133],
			 &nodes[NRF_DPPI_NODE_PPIB134_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB130_135],
			 &nodes[NRF_DPPI_NODE_DPPIC134])),
	/*27*/NRF_DPPI_ROUTE_DEFINE("apb3a_apb3c", NRF_DPPI_DOMAIN_APB3A,
			(&nodes[NRF_DPPI_NODE_DPPIC133],
			 &nodes[NRF_DPPI_NODE_PPIB134_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_136],
			 &nodes[NRF_DPPI_NODE_DPPIC135])),
	/*28*/NRF_DPPI_ROUTE_DEFINE("apb3a_apb3d", NRF_DPPI_DOMAIN_APB3A,
			(&nodes[NRF_DPPI_NODE_DPPIC133],
			 &nodes[NRF_DPPI_NODE_PPIB134_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_137],
			 &nodes[NRF_DPPI_NODE_DPPIC136])),
	/*29*/NRF_DPPI_ROUTE_DEFINE("apb3a_apb22", NRF_DPPI_DOMAIN_APB3A,
			(&nodes[NRF_DPPI_NODE_DPPIC133],
			 &nodes[NRF_DPPI_NODE_PPIB135_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_121],
			 &nodes[NRF_DPPI_NODE_DPPIC120])),

	/*30*/NRF_DPPI_ROUTE_DEFINE("apb3b_apb3c", NRF_DPPI_DOMAIN_APB3B,
			(&nodes[NRF_DPPI_NODE_DPPIC134],
			 &nodes[NRF_DPPI_NODE_PPIB135_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_136],
			 &nodes[NRF_DPPI_NODE_DPPIC135])),
	/*31*/NRF_DPPI_ROUTE_DEFINE("apb3b_apb3d", NRF_DPPI_DOMAIN_APB3B,
			(&nodes[NRF_DPPI_NODE_DPPIC134],
			 &nodes[NRF_DPPI_NODE_PPIB135_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_137],
			 &nodes[NRF_DPPI_NODE_DPPIC136])),
	/*32*/NRF_DPPI_ROUTE_DEFINE("apb3b_apb22", NRF_DPPI_DOMAIN_APB3B,
			(&nodes[NRF_DPPI_NODE_DPPIC134],
			 &nodes[NRF_DPPI_NODE_PPIB135_130],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_121],
			 &nodes[NRF_DPPI_NODE_DPPIC120])),

	/*33*/NRF_DPPI_ROUTE_DEFINE("apb3c_apb3d", NRF_DPPI_DOMAIN_APB3C,
			(&nodes[NRF_DPPI_NODE_DPPIC135],
			 &nodes[NRF_DPPI_NODE_PPIB136_131],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_137],
			 &nodes[NRF_DPPI_NODE_DPPIC136])),
	/*34*/NRF_DPPI_ROUTE_DEFINE("apb3c_apb22", NRF_DPPI_DOMAIN_APB3C,
			(&nodes[NRF_DPPI_NODE_DPPIC135],
			 &nodes[NRF_DPPI_NODE_PPIB136_131],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_121],
			 &nodes[NRF_DPPI_NODE_DPPIC120])),

	/*35*/NRF_DPPI_ROUTE_DEFINE("apb3d_apb22", NRF_DPPI_DOMAIN_APB3D,
			(&nodes[NRF_DPPI_NODE_DPPIC136],
			 &nodes[NRF_DPPI_NODE_PPIB137_131],
			 &nodes[NRF_DPPI_NODE_DPPIC130],
			 &nodes[NRF_DPPI_NODE_PPIB131_121],
			 &nodes[NRF_DPPI_NODE_DPPIC120])),
};

static const struct nrf_dppi_route *apb22_routes[] = {
	&dppi_routes[0], &dppi_routes[8], &dppi_routes[20], &dppi_routes[25],
	&dppi_routes[29], &dppi_routes[32], &dppi_routes[34], &dppi_routes[35]
};
static const struct nrf_dppi_route *apb32_routes[] = {
	&dppi_routes[8], &dppi_routes[1], &dppi_routes[9], &dppi_routes[10],
	&dppi_routes[11], &dppi_routes[12], &dppi_routes[13], &dppi_routes[14]
};
static const struct nrf_dppi_route *apb38_routes[] = {
	&dppi_routes[20], &dppi_routes[9], &dppi_routes[2], &dppi_routes[15],
	&dppi_routes[16], &dppi_routes[17], &dppi_routes[18], &dppi_routes[19]
};
static const struct nrf_dppi_route *apb39_routes[] = {
	&dppi_routes[25], &dppi_routes[10], &dppi_routes[15], &dppi_routes[3],
	&dppi_routes[21], &dppi_routes[22], &dppi_routes[23], &dppi_routes[24]
};

static const struct nrf_dppi_route *apb3a_routes[] = {
	&dppi_routes[29], &dppi_routes[11], &dppi_routes[16], &dppi_routes[21],
	&dppi_routes[4], &dppi_routes[26], &dppi_routes[27], &dppi_routes[28]
};

static const struct nrf_dppi_route *apb3b_routes[] = {
	&dppi_routes[32], &dppi_routes[12], &dppi_routes[17], &dppi_routes[22],
	&dppi_routes[26], &dppi_routes[5], &dppi_routes[30], &dppi_routes[31]
};

static const struct nrf_dppi_route *apb3c_routes[] = {
	&dppi_routes[34], &dppi_routes[13], &dppi_routes[18], &dppi_routes[23],
	&dppi_routes[27], &dppi_routes[30], &dppi_routes[6], &dppi_routes[33]
};

static const struct nrf_dppi_route *apb3d_routes[] = {
	&dppi_routes[35], &dppi_routes[13], &dppi_routes[19], &dppi_routes[24],
	&dppi_routes[28], &dppi_routes[31], &dppi_routes[33], &dppi_routes[7]
};

const struct nrf_dppi_route **dppi_route_map[] = {
	apb22_routes, apb32_routes, apb38_routes, apb39_routes,
	apb3a_routes, apb3b_routes, apb3c_routes, apb3d_routes
};

uint32_t nrf_dppi_get_domain_id(uint32_t addr)
{
	uint32_t domain = (addr >> 24) & 0xf;
	uint32_t apb = (addr >> 16) & 0xff;

	__ASSERT_NO_MSG(domain == 0xf);

	if (apb < 0x92) {
		return NRF_DPPI_DOMAIN_APB22;
	} else if (apb <= 0x93) {
		return NRF_DPPI_DOMAIN_APB32;
	} else {
		return abp - 0x98 + NRF_DPPI_DOMAIN_APB38;
	}
}

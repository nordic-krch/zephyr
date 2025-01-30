#ifndef ZEPHYR_DRIVERS_MISC_NORDIC_DPPI_ROUTES_H__
#define ZEPHYR_DRIVERS_MISC_NORDIC_DPPI_ROUTES_H__

#define DPPI_USE_PPIB_CH_OFF 1
struct nrf_dppi_node_bridge {
	atomic_t *channels;
	NRF_PPIB_Type *reg[2];
#ifdef DPPI_USE_PPIB_CH_OFF
	uint8_t ch_off[2];
#endif
};

struct nrf_dppi_node_ctlr {
	atomic_t *channels;
	atomic_t *group_channels;
	NRF_DPPIC_Type *reg;
};

struct nrf_dppi_node_generic {
	atomic_t *channels;
};

enum nrf_dppi_node_type {
	NRF_DPPI_NODE_DOMAIN,
	NRF_DPPI_NODE_BRIDGE,
};

struct nrf_dppi_node {
	enum nrf_dppi_node_type type;
	uint8_t domain_id;
	const char *name;
	union {
		struct nrf_dppi_node_ctlr domain;
		struct nrf_dppi_node_bridge bridge;
		struct nrf_dppi_node_generic generic;
	};
};

#define NRF_DPPI_ROUTE_HAS_NAME 1
struct nrf_dppi_route {
#ifdef NRF_DPPI_ROUTE_HAS_NAME
	const char *name;
#endif
	const struct nrf_dppi_node * const *nodes;
	uint8_t len;
	uint8_t first_domain;
};
typedef uint32_t nrf_dppi_route_handle_t;

#define DPPIC_NODE_DEFINE(_id, _domain_id) \
[NRF_DPPI_NODE_DPPIC##_id] = { \
		.type = NRF_DPPI_NODE_DOMAIN, \
		.domain_id = _domain_id, \
		.name = "dppi" STRINGIFY(_id), \
		.domain = { \
			.channels = &channels[NRF_DPPI_NODE_DPPIC##_id], \
			.group_channels = &group_channels[NRF_DPPI_NODE_DPPIC##_id], \
			.reg = NRF_DPPIC##_id \
		} \
	}

#define PPIB_NODE_DEFINE(_id1, _id2) \
[NRF_DPPI_NODE_PPIB##_id1##_##_id2] = { \
		.type = NRF_DPPI_NODE_BRIDGE, \
		.name = "ppib" STRINGIFY(_id1) "_" STRINGIFY(_id2), \
		.bridge = { \
			.channels = &channels[NRF_DPPI_NODE_PPIB##_id1##_##_id2], \
			.reg = {NRF_PPIB##_id1, NRF_PPIB##_id2} \
		} \
	}

#define PPIB_REG(id) (NRF_PPIB_Type *)DT_REG_ADDR(DT_NODELABEL(ppib##id))

#define PPIB_EXT_NODE_DEFINE(_id1, _id2, _ch_id, _off1, _off2) \
[NRF_DPPI_NODE_PPIB##_id1##_##_id2] = { \
		.name = "ppib" STRINGIFY(_id1) "_" STRINGIFY(_id2), \
		.type = NRF_DPPI_NODE_BRIDGE, \
		.bridge = { \
			.channels = &channels[NRF_DPPI_NODE_PPIB##_ch_id], \
			.reg = {PPIB_REG(_id1), PPIB_REG(_id2)}, \
			.ch_off = { _off1, _off2 } \
		}, \
	}

#define NRF_DPPI_ROUTE_DEFINE(_name, _first_domain, _nodes) \
{ \
	IF_ENABLED(NRF_DPPI_ROUTE_HAS_NAME, (.name = _name,)) \
	.nodes = (const struct nrf_dppi_node * const[]){ __DEBRACKET _nodes}, \
	.len = UTIL_INC(NUM_VA_ARGS_LESS_1 _nodes), \
	.first_domain = _first_domain \
}

#endif /* ZEPHYR_DRIVERS_MISC_NORDIC_DPPI_ROUTES_H__ */


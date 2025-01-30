#include <hal/nrf_ppib.h>
#include <hal/nrf_dppi.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/misc/ppi/nrfx_dppi.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dppi, 3);

#include "nrfx_dppi_routes.h"
#if defined(CONFIG_SOC_NRF54H20)
#include "nrfx_dppi_nrf54h.h"
#endif

#define MAX_NODES 5
#define CH_BITS 5
#define CH_MASK BIT_MASK(CH_BITS)
#define IS_REV_BIT 25
#define ROUTE_OFF 26
#define IS_SEC_BIT 24 /* Used only on Haltium */
#define DPPI_ID_BITS 5
#define DPPI_CNT_MAX_BITS 3
#define DPPI_CNT_BITS 3
#define DPPI_CNT_OFF (DPPI_CNT_MAX_BITS * DPPI_ID_BITS)
#define DPPI_CH_OFF (DPPI_CNT_OFF + DPPI_CNT_BITS)

#define HANDLE_GET_ROUTE_ID(handle) (handle >> ROUTE_OFF)
#define HANDLE_IS_REVERSED(handle) (handle & BIT(IS_REV_BIT))

#if !defined(CONFIG_SOC_NRF54H20)
#define HANDLE_GET_CHAN(_handle, _i) ((_handle >> (_i * CH_BITS)) & CH_MASK)
#else
#define HANDLE_IS_SEC(_handle) (_handle & BIT(IS_SEC_BIT))
#define HANDLE_GET_CHAN(_handle, _i) ((_handle >> DPPI_CH_OFF) & CH_MASK)
#define HANDLE_GET_DPPI_CNT(_handle) ((_handle >> DPPI_CNT_OFF) & BIT_MASK(DPPI_CNT_BITS))
#define HANDLE_GET_DPPI_ID(_handle, _i) ((_handle >> (DPPI_ID_BITS * _i)) & BIT_MASK(DPPI_ID_BITS))


#endif

/* Handle has different layout for Lumos and Haltium. Lumos has different channels
 * on each node (DPPIC/PPIB) and Haltium has allocation through SDFW.
 *
 * Lumos:
 *
 * --------------------------------------------------------------------------
 * | Route ID 6b | Reversed 1b | ch4 5b | ch3 5b | ch2 5b | ch1 5b | ch0 5b |
 * --------------------------------------------------------------------------
 *
 *  Haltium:
 *  There is fixed channel but local domain has no access to routes so need to
 *  know which DPPIC instances belong to route (max 3).
 *
 * ---------------------------------------------------------------------------------------------
 * | Route ID 6b | Reversed 1b | sec 1b | ch 6b | dppi_cnt 3b | dppi2 5b | dppi1 5b | ddpi0 5b |
 * ---------------------------------------------------------------------------------------------
 *
 * sec bit indicates whether handle is managed by SDFW
 */
#if defined(NRF54L_SERIES) || defined(CONFIG_SOC_NRF54H20_CPURAD) || defined(CONFIG_HALTIUM_CPUSEC)
#define NRFX_DPPI_LOCAL_RESOURCE_MANAGEMENT 1
#endif

#ifdef NRFX_DPPI_LOCAL_RESOURCE_MANAGEMENT
extern const struct nrf_dppi_route dppi_routes[];
extern const struct nrf_dppi_route **dppi_route_map[];

#define D_ID_ADJUST(_d) (_d - (IS_ENABLED(CONFIG_SOC_NRF54H20_CPURAD) ? 8 : 0))

static int alloc_channels(uint8_t *channels, const struct nrf_dppi_route *route)
{
	uint32_t key;
	int rv = 0;

	key = irq_lock();

#if defined(CONFIG_SOC_NRF54H20)
	/* In Haltium connections between channels in DPPI and PPIB are fixed which
	 * means that to correctly allocate a route same channel must be available
	 * in all nodes.
	 */
	uint32_t mask = UINT32_MAX;
	uint32_t ch;

	for (size_t i = 0; i < route->len; i++) {
		mask &= *route->nodes[i]->generic.channels;
	}

	if (!mask) {
		rv = -ENOMEM;
		goto unlock;
	}

	ch = 31 - __builtin_clz(mask);
	for (size_t i = 0; i < route->len; i++) {
		*route->nodes[i]->generic.channels &= ~BIT(ch);
	}

	channels[0] = ch;
#else
	/* Lumus support flexible setup so any channel can be allocated in each node. */
	for (size_t i = 0; i < route->len; i++) {
		const struct nrf_dppi_node *node = route->nodes[i];

		if (*node->generic.channels == 0) {
			rv = -ENOMEM;
			goto unlock;
		}
	}
	for (size_t i = 0; i < route->len; i++) {
		const struct nrf_dppi_node *node = route->nodes[i];
		uint32_t ch = 31 - __builtin_clz(*node->generic.channels);

		*node->generic.channels &= ~BIT(ch);
		channels[i] = ch;
	}
#endif
unlock:

	irq_unlock(key);
	return rv;
}

static inline uint32_t get_sub_pub_ch(bool pub, uint8_t *channels, size_t i,
					const struct nrf_dppi_node_bridge *bridge, bool rev)
{
#ifdef CONFIG_SOC_NRF54H20
#ifdef DPPI_USE_PPIB_CH_OFF
	return channels[0] + bridge->ch_off[(pub ^ rev) ? 0 : 1];
#else
	return channels[0];
#endif
#else
	if (pub) {
		return rev ? channels[i - 1] : channels[i + 1];
	}
	return rev ? channels[i + 1] : channels[i - 1];
#endif
}

static inline uint32_t get_ppib_ch(uint8_t *channels, int i)
{
	return IS_ENABLED(CONFIG_SOC_NRF54H20) ? channels[0] : channels[i];
}


#ifdef CONFIG_SOC_NRF54H20_CPURAD
int nrf_dppi_domain_local_alloc(uint32_t src_d, uint32_t dst_d, nrf_dppi_handle_t *handle)
#else
int nrf_dppi_domain_conn_alloc(uint32_t src_d, uint32_t dst_d, nrf_dppi_handle_t *handle)
#endif
{
	uint8_t channels[MAX_NODES];
	const struct nrf_dppi_route *route;
	uint32_t h;
	int rv = 0;
	uint8_t route_idx;
	bool rev;

	route = dppi_route_map[D_ID_ADJUST(src_d)][D_ID_ADJUST(dst_d)];
	/* Return is allocation failed. */
	rv = alloc_channels(channels, route);
	if (rv < 0) {
		return rv;
	}

	route_idx = ((uintptr_t)route - (uintptr_t)dppi_routes) / sizeof(struct nrf_dppi_route);
	rev = (route->first_domain != src_d);
	LOG_INF("connect source:%d with dest:%d", src_d, dst_d);
	LOG_INF("alloc, source domain:%d destination domain:%d, route:%s (idx: %d len:%d) %s",
		src_d, dst_d, route->name, route_idx, route->len, rev ? "reversed" : "");

	h = (route_idx << ROUTE_OFF) | (rev ? BIT(IS_REV_BIT) : 0) |
		(IS_ENABLED(CONFIG_SOC_NRF54H20) ?
			((DIV_ROUND_UP(route->len, 2) << DPPI_CNT_OFF) |
			 (channels[0] << DPPI_CH_OFF) |
			(IS_ENABLED(SDFW) ? BIT(IS_SEC_BIT) : 0)) : 0);
	LOG_ERR("h:%08x %08x", h, route_idx << ROUTE_OFF);
	for (size_t i = 0; i < route->len; i++) {
		if (route->nodes[i]->type == NRF_DPPI_NODE_BRIDGE) {
			const struct nrf_dppi_node_bridge *bridge = &route->nodes[i]->bridge;
			nrf_ppib_event_t evt;
			nrf_ppib_task_t task;
			uint32_t ppib_ch = get_ppib_ch(channels, i);
			uint32_t sub_ch = get_sub_pub_ch(false, channels, i, bridge, rev);
			uint32_t pub_ch = get_sub_pub_ch(true, channels, i, bridge, rev);
			NRF_PPIB_Type *sub_reg = bridge->reg[rev ? 1 : 0];
			NRF_PPIB_Type *pub_reg = bridge->reg[rev ? 0 : 1];

			evt = nrf_ppib_receive_event_get(ppib_ch);
			task = nrf_ppib_send_task_get(ppib_ch);

			LOG_INF("Setup %s PPIB chan:%d, subscribe to ch:%d, publish to ch:%d",
				route->nodes[i]->name, ppib_ch, sub_ch, pub_ch);

			nrf_ppib_subscribe_set(sub_reg, task, sub_ch);
			nrf_ppib_publish_set(pub_reg, evt, pub_ch);
		} else if (IS_ENABLED(CONFIG_SOC_NRF54H20)) {
			h |= (uint32_t)(route->nodes[i]->domain_id << (DPPI_ID_BITS * (i / 2)));
		}

		if (!IS_ENABLED(CONFIG_SOC_NRF54H20)) {
			h |= (channels[i] << (5 * i));
		}
	}

	*handle = h;
	LOG_INF("Alloc done, handle:%08x route:%d", h, route_idx);

	return 0;
}

#ifdef CONFIG_SOC_NRF54H20_CPURAD
void nrf_dppi_domain_local_free(nrf_dppi_handle_t handle)
#else
void nrf_dppi_domain_conn_free(nrf_dppi_handle_t handle)
#endif
{
	uint32_t route_id = HANDLE_GET_ROUTE_ID(handle);
	const struct nrf_dppi_route *route = &dppi_routes[route_id];
	bool rev= handle & BIT(IS_REV_BIT);

	LOG_INF("Freeing connection handle:%08x (route %d)", handle, route_id);
	for (size_t i = 0; i < route->len; i++) {
		uint32_t chan = HANDLE_GET_CHAN(handle, i);
		const struct nrf_dppi_node *node = route->nodes[i];

		if (node->type == NRF_DPPI_NODE_BRIDGE) {
			/* Go over every second node which will be PPIB. */
			nrf_ppib_publish_clear(node->bridge.reg[rev ? 1 : 0],
					nrf_ppib_receive_event_get(chan));
			nrf_ppib_subscribe_clear(node->bridge.reg[rev ? 0 : 1],
					nrf_ppib_send_task_get(chan));
		}
		LOG_INF("%s Freeing chan %d", node->name, chan);
		atomic_or(node->generic.channels, BIT(chan));
	}
}
#endif /* NRFX_DPPI_LOCAL_RESOURCE_MANAGEMENT */

#if defined(CONFIG_SOC_NRF54H20)
#define FOR_EACH_DPPI(_handle, _ch, _reg, _d_id)						\
	uint32_t cnt = HANDLE_GET_DPPI_CNT(_handle);						\
	size_t i;										\
	_ch = HANDLE_GET_CHAN(_handle, 0);							\
	for (i = 0, _d_id = HANDLE_GET_DPPI_ID(_handle, 0),_reg = nrfx_dppi_get_reg(_d_id);	\
	     i < cnt;										\
	     i++, _d_id = HANDLE_GET_DPPI_ID(_handle, i), _reg = nrfx_dppi_get_reg(_d_id))
#else
#define FOR_EACH_DPPI(_handle, _ch, _reg, _d_id)						\
	const struct nrf_dppi_route *_route = &dppi_routes[HANDLE_GET_ROUTE_ID(_handle)];	\
	size_t i;										\
	for (i = 0, _ch = HANDLE_GET_CHAN(_handle, i), _d_id = _route->nodes[i]->domain_id,	\
			_reg = _route->nodes[i]->domain.reg;					\
	     i < _route->len;									\
	     i += 2, _ch = HANDLE_GET_CHAN(_handle, i), _d_id = _route->nodes[i]->domain_id,	\
	     _reg = _route->nodes[i]->domain.reg)
#endif

void nrf_dppi_conn_ctrl(nrf_dppi_handle_t handle, bool enable)
{
	NRF_DPPIC_Type *reg;
	uint8_t ch;
	uint8_t d_id;

	FOR_EACH_DPPI(handle, ch, reg, d_id) {
		if (enable) {
			nrf_dppi_channels_enable(reg, BIT(ch));
		} else {
			nrf_dppi_channels_disable(reg, BIT(ch));
		}
	}
}

void nrf_dppi_domain_ctrl(nrf_dppi_handle_t handle, uint32_t domain, bool enable)
{
	NRF_DPPIC_Type *reg;
	uint8_t ch;
	uint8_t d_id;

	FOR_EACH_DPPI(handle, ch, reg, d_id) {
		if (d_id == domain) {
			if (enable) {
				nrf_dppi_channels_enable(reg, BIT(ch));
			} else {
				nrf_dppi_channels_disable(reg, BIT(ch));
			}
		}
	}
}

void nrf_dppi_ep_clear(uint32_t ep)
{
	NRF_DPPI_ENDPOINT_CLEAR(ep);
}

int nrf_dppi_ep_attach(nrf_dppi_handle_t handle, uint32_t ep)
{
	NRF_DPPIC_Type *reg;
	uint8_t ch;
	uint8_t d_id;

	FOR_EACH_DPPI(handle, ch, reg, d_id) {
		if (d_id == nrf_dppi_get_domain_id(ep)) {
    			NRF_DPPI_ENDPOINT_SETUP(ep, ch);
			return 0;
		}
	}

	return -EINVAL;
}

int nrf_dppi_group_alloc(uint32_t *ep, size_t ep_cnt, nrf_dppi_group_handle_t *handle)
{
	uint32_t d = nrf_dppi_get_domain_id(ep[0]);
	const struct nrf_dppi_node *node = dppi_routes[d].nodes[0];
	uint32_t gch, prev_mask, mask;
	uint32_t group_mask = 0;

	for (size_t i = 0; i < ep_cnt; i++) {
		if ((nrf_dppi_get_domain_id(ep[0]) != nrf_dppi_get_domain_id(ep[i])) ||
		    ((*(uint32_t *)ep[i] & BIT(31)) == 0)) {
			return -EINVAL;
		}
	}

	do {
		mask = *node->domain.group_channels;
		if (mask == 0) {
			return -ENOMEM;
		}
		gch = 31 - __builtin_clz(mask);
		prev_mask = atomic_and(node->domain.group_channels, ~BIT(gch));
	} while ((prev_mask & BIT(gch)) == 0);

	for (size_t i = 0; i < ep_cnt; i++) {
		uint32_t tmp = ep[0] + NRF_SUBSCRIBE_PUBLISH_OFFSET(ep[0]);
		uint32_t ch = *(volatile uint32_t *)tmp & 0xFF;

		group_mask |= BIT(ch);
	}

	nrf_dppi_channels_group_set(node->domain.reg, group_mask, gch);
	*handle = d | (gch << 8);

	return 0;
}

int nrf_dppi_group_modify(nrf_dppi_group_handle_t handle, uint32_t ep, bool add)
{
	uint32_t d = handle & 0xFF;
	uint32_t group = handle >> 8;
	const struct nrf_dppi_node *node = dppi_routes[d].nodes[0];

	if (nrf_dppi_get_domain_id(ep) != d) {
		return -EINVAL;
	}

	uint32_t tmp = ep + NRF_SUBSCRIBE_PUBLISH_OFFSET(ep);
	uint32_t ch = *(volatile uint32_t *)tmp & 0xFF;

	if (add) {
		nrf_dppi_channels_include_in_group(node->domain.reg, BIT(ch), group);
	} else {
		nrf_dppi_channels_remove_from_group(node->domain.reg, BIT(ch), group);
	}

	return 0;
}

void nrf_dppi_group_ctrl(nrf_dppi_group_handle_t handle, bool enable)
{
	uint32_t d = handle & 0xFF;
	uint32_t ch = handle >> 8;
	const struct nrf_dppi_node *node = dppi_routes[d].nodes[0];

	if (enable) {
		nrf_dppi_group_enable(node->domain.reg, ch);
	} else {
		nrf_dppi_group_disable(node->domain.reg, ch);
	}
}

uint32_t nrf_dppi_group_ep(nrf_dppi_group_handle_t handle, bool enable)
{
	uint32_t d = handle & 0xFF;
	uint32_t ch = handle >> 8;
	const struct nrf_dppi_node *node = dppi_routes[d].nodes[0];
	NRF_DPPIC_Type *dppic = node->domain.reg;

	return enable ? (uint32_t)&dppic->TASKS_CHG[ch].EN : (uint32_t)&dppic->TASKS_CHG[ch].DIS;
}

void nrf_dppi_group_free(nrf_dppi_group_handle_t handle)
{
	uint32_t d = handle & 0xFF;
	uint32_t ch = handle >> 8;
	const struct nrf_dppi_node *node = dppi_routes[d].nodes[0];

	nrf_dppi_group_clear(node->domain.reg, ch);
	atomic_or(node->domain.group_channels, BIT(ch));
}

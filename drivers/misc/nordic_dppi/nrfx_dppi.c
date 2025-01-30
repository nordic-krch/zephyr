
#include <hal/nrf_dppi.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/misc/ppi/nrfx_dppi.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dppi, 2);

#if defined(CONFIG_NORDIC_DPPI_MULTI_DOMAIN)
#include <hal/nrf_ppib.h>
#include "nrfx_dppi_routes.h"
#endif

#if defined(CONFIG_SOC_NRF54H20)
#include "nrfx_dppi_nrf54h.h"
#include <nrf_ironside/krch.h>
#endif

#if defined(CONFIG_SOC_NRF54H20)
/* Due to fixed connections between DPPIC instances only one channel is used per connection. */
#define DPPI_CH_MAX_CNT 1

#define DPPI_EXT_OFF 0
#define DPPI_EXT_BITS 1

#define DPPI_INST_OFF (DPPI_EXT_OFF + DPPI_EXT_BITS)
#define DPPI_SINGLE_INST_BITS 5
#define DPPI_INST_MAX_CNT 3
#define DPPI_INST_BITS (DPPI_SINGLE_INST_BITS * DPPI_INST_MAX_CNT)

#define DPPI_INST_CNT_OFF (DPPI_INST_OFF + DPPI_INST_BITS)
#define DPPI_INST_CNT_BITS 3

#define DPPI_CH_OFF (DPPI_INST_CNT_OFF + DPPI_INST_CNT_BITS)
#define DPPI_CH_BITS 5

#define DPPI_REV_OFF (DPPI_CH_OFF + DPPI_CH_BITS)
#define DPPI_REV_BITS 1

#define DPPI_ROUTE_OFF (DPPI_REV_OFF + DPPI_REV_BITS)
#define DPPI_ROUTE_BITS 6

#define DPPI_RESERVED_BITS 1

#define DPPI_TOTAL_BITS (DPPI_RESERVED_BITS + DPPI_ROUTE_BITS + DPPI_REV_BITS + \
		DPPI_CH_BITS + DPPI_INST_CNT_BITS + DPPI_INST_BITS + DPPI_EXT_BITS)
BUILD_ASSERT(DPPI_TOTAL_BITS == 32);

/* Not used. */
#define HANDLE_CHAN(_i, _chan) 0

/* Extract channel from handle. */
#define HANDLE_GET_CHAN(_handle, _i) (((_handle) >> DPPI_CH_OFF) & BIT_MASK(DPPI_CH_BITS))

/* Extract number of DPPI instances in the connection. */
#define HANDLE_GET_DPPI_CNT(_handle) \
	(((_handle) >> DPPI_INST_CNT_OFF) & BIT_MASK(DPPI_INST_CNT_BITS))

/* Extract nth DPPIC instance ID. */
#define HANDLE_GET_DPPI_ID(_handle, _i)						\
	(((_handle) >> (DPPI_SINGLE_INST_BITS * (_i) + DPPI_INST_OFF)) &	\
	 BIT_MASK(DPPI_SINGLE_INST_BITS))

#define HANDLE_INST(_i, _id) ((_id) << (DPPI_SINGLE_INST_BITS * (_i) + DPPI_INST_OFF))

/* Extract route ID from handle. */
#define HANDLE_GET_ROUTE_ID(handle) (((handle) >> DPPI_ROUTE_OFF) & BIT_MASK(DPPI_ROUTE_BITS))

/* Determine if handle has reversed route. */
#define HANDLE_IS_REVERSED(handle) (handle & BIT(DPPI_REV_OFF))

#define HANDLE_INIT(route_id, rev, dppi_cnt, fixed_ch)				\
	((route_id) << DPPI_ROUTE_OFF) |					\
	(rev ? BIT(DPPI_REV_OFF) : 0) |						\
	(dppi_cnt << DPPI_INST_CNT_OFF) |					\
	(fixed_ch << DPPI_CH_OFF) | BIT(DPPI_EXT_OFF)

#else /* CONFIG_SOC_NRF54H20 */

#define DPPI_CH_MAX_CNT 5
#define DPPI_CH_OFF 0
#define DPPI_CH_BITS 5

#define DPPI_REV_OFF (DPPI_CH_OFF + DPPI_CH_MAX_CNT * DPPI_CH_BITS)
#define DPPI_REV_BITS 1

#define DPPI_ROUTE_OFF (DPPI_REV_OFF + DPPI_REV_BITS)
#define DPPI_ROUTE_BITS 6

#define DPPI_TOTAL_BITS (DPPI_CH_MAX_CNT * DPPI_CH_BITS + DPPI_REV_BITS + DPPI_ROUTE_BITS)
BUILD_ASSERT(DPPI_TOTAL_BITS == 32);

/* Extract channel from handle. */
#define HANDLE_GET_CHAN(_handle, _i) (((_handle) >> ((_i) * DPPI_CH_BITS)) & BIT_MASK(DPPI_CH_BITS))

/* Extract route ID from handle. */
#define HANDLE_GET_ROUTE_ID(handle) (((handle) >> DPPI_ROUTE_OFF) & BIT_MASK(DPPI_ROUTE_BITS))

/* Determine if handle has reversed route. */
#define HANDLE_IS_REVERSED(handle) ((handle) & BIT(DPPI_REV_OFF))

#define HANDLE_CHAN(i, ch) ((ch) << (DPPI_CH_BITS * (i) + DPPI_CH_OFF))
/* Not used. */
#define HANDLE_INST(i, id) 0

#define HANDLE_INIT(route_id, rev, dppi_cnt, fixed_ch)				\
	((route_id) << DPPI_ROUTE_OFF) | (rev ? BIT(DPPI_REV_OFF) : 0)

#endif /* CONFIG_SOC_NRF54H20 */

#define GHANDLE_CHAN_OFF 0
#define GHANDLE_CHAN_BITS 8
#define GHANDLE_DOMAIN_OFF 8
#define GHANDLE_DOMAIN_BITS 8
#define GHANDLE_GET_CHAN(handle) ((handle >> GHANDLE_CHAN_OFF) & BIT_MASK(GHANDLE_CHAN_BITS))
#define GHANDLE_GET_DOMAIN(handle) ((handle >> GHANDLE_DOMAIN_OFF) & BIT_MASK(GHANDLE_DOMAIN_BITS))

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
 * | Route ID 6b | Reversed 1b | ch 6b | dppi_cnt 3b | dppi2 5b | dppi1 5b | ddpi0 5b | ext 1b |
 * ---------------------------------------------------------------------------------------------
 *
 * sec bit indicates whether handle is managed by SDFW
 */
#if defined(CONFIG_NORDIC_DPPI_MULTI_DOMAIN)
extern const struct nrf_dppi_route dppi_routes[];
extern const struct nrf_dppi_route **dppi_route_map[];
#else
static atomic_t ch_mask =
	COND_CODE_1(IS_EQ(DPPIC_CH_NUM, 32), (0xFFFFFFFF), (BIT_MASK(DPPIC_CH_NUM))) &
	~BIT_MASK(NRFX_DPPI0_CHANNELS_USED);
static atomic_t group_mask = BIT_MASK(DPPIC_GROUP_NUM) & ~BIT_MASK(NRFX_DPPI0_GROUPS_USED);
#endif /* CONFIG_NORDIC_DPPI_MULTI_DOMAIN */

#define D_ID_ADJUST(_d) (_d - (IS_ENABLED(CONFIG_SOC_NRF54H20_CPURAD) ? 8 : 0))

static int alloc_bit_locked(atomic_t *mask)
{
	int rv = 31 - __builtin_clz(*mask);

	*mask &= ~BIT(rv);

	return rv;
}

#if defined(CONFIG_NORDIC_DPPI_MULTI_DOMAIN)

uint32_t nrf_dppi_group_domain_id(nrf_dppi_group_handle_t handle)
{
	return GHANDLE_GET_DOMAIN(handle);
}

static int alloc_channels(uint8_t *channels, const struct nrf_dppi_route *route)
{
	uint32_t key;
	int rv = 0;

	key = irq_lock();

	if (IS_ENABLED(CONFIG_SOC_NRF54H20)) {
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
	} else {
		/* Lumus support flexible setup so any channel can be allocated in each node. */
		for (size_t i = 0; i < route->len; i++) {
			const struct nrf_dppi_node *node = route->nodes[i];

			if (*node->generic.channels == 0) {
				rv = -ENOMEM;
				goto unlock;
			}
		}
		for (size_t i = 0; i < route->len; i++) {
			channels[i] = alloc_bit_locked(route->nodes[i]->generic.channels);
		}
	}
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

static inline uint32_t get_ppi_ch(bool pub, uint8_t *channels, size_t i, bool rev)
{
	if (IS_ENABLED(CONFIG_SOC_NRF54H20)) {
		return channels[0];
	}

	if (pub) {
		return rev ? channels[i - 1] : channels[i + 1];
	}
	return rev ? channels[i + 1] : channels[i - 1];
}

static inline uint32_t get_bridge_ch(bool pub, uint8_t channel, size_t i,
					const struct nrf_dppi_node_bridge *bridge, bool rev)
{
	if (IS_ENABLED(CONFIG_SOC_NRF54H20)) {
#ifdef DPPI_USE_PPIB_CH_OFF
		return channel + bridge->ch_off[(pub ^ rev) ? 1 : 0];
#else
		return channel;
#endif
	}

	return channel;
}

static inline int ppib_write(volatile uint32_t *addr, uint32_t val)
{
#if CONFIG_SOC_NRF54H20_CPUAPP
	return ironside_krch_memory_write((uint32_t)addr, val);
#else
	*addr = val;
	return 0;
#endif
}

#ifdef CONFIG_SOC_NRF54H20_CPURAD
int nrf_dppi_domain_local_alloc(uint32_t src_d, uint32_t dst_d, nrf_dppi_handle_t *handle)
#else
int nrf_dppi_domain_conn_alloc(uint32_t src_d, uint32_t dst_d, nrf_dppi_handle_t *handle)
#endif
{
	uint8_t channels[DPPI_CH_MAX_CNT];
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

	h = HANDLE_INIT(route_idx, rev, DIV_ROUND_UP(route->len, 2), channels[0]);
	for (size_t i = 0; i < route->len; i++) {
		if (route->nodes[i]->type == NRF_DPPI_NODE_BRIDGE) {
			const struct nrf_dppi_node_bridge *bridge = &route->nodes[i]->bridge;
			uint32_t ch = channels[IS_ENABLED(CONFIG_SOC_NRF54H20) ? 0 : i];
			uint32_t sub_ppi_ch = get_ppi_ch(false, channels, i, rev);
			uint32_t pub_ppi_ch = get_ppi_ch(true, channels, i, rev);
			uint32_t sub_bridge_ch = get_bridge_ch(false, ch, i, bridge, rev);
			uint32_t pub_bridge_ch = get_bridge_ch(true, ch, i, bridge, rev);
			NRF_PPIB_Type *pub_reg = bridge->reg[rev ? 0 : 1];
			NRF_PPIB_Type *sub_reg = bridge->reg[rev ? 1 : 0];
			int rv;

			rv = ppib_write(&sub_reg->SUBSCRIBE_SEND[sub_bridge_ch],
					sub_ppi_ch | NRF_SUBSCRIBE_PUBLISH_ENABLE);
			if (rv < 0) {
				return rv;
			}

			rv = ppib_write(&pub_reg->PUBLISH_RECEIVE[pub_bridge_ch],
					pub_ppi_ch | NRF_SUBSCRIBE_PUBLISH_ENABLE);
			if (rv < 0) {
				return rv;
			}

			LOG_INF("Setup %s subscribe PPIB(%p) ch %d to DPPI ch:%d, "
					"publish PPIB(%p) ch:%d to DPPI ch:%d",
				route->nodes[i]->name, sub_reg, sub_bridge_ch, sub_ppi_ch,
				pub_reg, pub_bridge_ch, pub_ppi_ch);
		} else if (IS_ENABLED(CONFIG_SOC_NRF54H20)) {
			h |= HANDLE_INST(i / 2, route->nodes[i]->domain_id);
		}

		if (!IS_ENABLED(CONFIG_SOC_NRF54H20)) {
			h |= HANDLE_CHAN(i, channels[i]);
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
	bool rev = HANDLE_IS_REVERSED(handle);

	LOG_INF("Freeing connection handle:%08x (route %d)", handle, route_id);
	for (size_t i = 0; i < route->len; i++) {
		uint32_t chan = HANDLE_GET_CHAN(handle, i);
		const struct nrf_dppi_node *node = route->nodes[i];

		if (node->type == NRF_DPPI_NODE_BRIDGE) {
			/* Go over every second node which will be PPIB. */
			const struct nrf_dppi_node_bridge *bridge = &route->nodes[i]->bridge;
			NRF_PPIB_Type *pub_reg = bridge->reg[rev ? 0 : 1];
			NRF_PPIB_Type *sub_reg = bridge->reg[rev ? 1 : 0];
			uint32_t sub_ch = get_bridge_ch(false, chan, i, bridge, rev);
			uint32_t pub_ch = get_bridge_ch(true, chan, i, bridge, rev);
			int rv;

			LOG_INF("Reset PPIB(%p) sub ch:%d, PPIB(%p) pub ch:%d",
					sub_reg, sub_ch, pub_reg, pub_ch);
			rv = ppib_write(&sub_reg->SUBSCRIBE_SEND[sub_ch], 0);
			__ASSERT_NO_MSG(rv == 0);

			rv = ppib_write(&pub_reg->PUBLISH_RECEIVE[pub_ch], 0);
			__ASSERT_NO_MSG(rv == 0);
		}
		LOG_INF("%s Freeing chan %d", node->name, chan);
		atomic_or(node->generic.channels, BIT(chan));
	}
}
#else
int nrf_dppi_domain_conn_alloc(uint32_t producer, uint32_t consumer, nrf_dppi_handle_t *handle)
{
	ARG_UNUSED(producer);
	ARG_UNUSED(consumer);
	int key = irq_lock();
	int rv = alloc_bit_locked(&ch_mask);

	irq_unlock(key);
	if (rv < 0) {
		return rv;
	}

	*handle = (nrf_dppi_handle_t)rv;
	return 0;
}

void nrf_dppi_domain_conn_free(nrf_dppi_handle_t handle)
{
	atomic_or(&ch_mask, (uint32_t)handle);
}
#endif /* CONFIG_NORDIC_DPPI_MULTI_DOMAIN  */

static NRF_DPPIC_Type *dppi_reg_get(uint32_t domain_id)
{
#if defined(CONFIG_NORDIC_DPPI_MULTI_DOMAIN)
	return dppi_routes[domain_id].nodes[0]->domain.reg;
#else
	return NRF_DPPIC;
#endif
}

static atomic_t *get_group_chan_mask(uint32_t domain)
{
#if defined(CONFIG_NORDIC_DPPI_MULTI_DOMAIN)
	return dppi_routes[domain].nodes[0]->domain.group_channels;
#else
	return &group_mask;
#endif
}

#if defined(CONFIG_SOC_NRF54H20)
#define FOR_EACH_DPPI(_handle, _ch, _reg, _d_id)						\
	uint32_t cnt = HANDLE_GET_DPPI_CNT(_handle);						\
	size_t i;										\
	_ch = HANDLE_GET_CHAN(_handle, 0);							\
	for (i = 0, _d_id = HANDLE_GET_DPPI_ID(_handle, 0),_reg = nrfx_dppi_get_reg(_d_id);	\
	     i < cnt;										\
	     i++, _d_id = HANDLE_GET_DPPI_ID(_handle, i), _reg = nrfx_dppi_get_reg(_d_id))
#elif defined(CONFIG_NORDIC_DPPI_MULTI_DOMAIN)
#define FOR_EACH_DPPI(_handle, _ch, _reg, _d_id)						\
	const struct nrf_dppi_route *_route = &dppi_routes[HANDLE_GET_ROUTE_ID(_handle)];	\
	size_t i;										\
	for (i = 0, _ch = HANDLE_GET_CHAN(_handle, i), _d_id = _route->nodes[i]->domain_id,	\
			_reg = _route->nodes[i]->domain.reg;					\
	     i < _route->len;									\
	     i += 2, _ch = HANDLE_GET_CHAN(_handle, i), _d_id = _route->nodes[i]->domain_id,	\
	     _reg = _route->nodes[i]->domain.reg)
#else
#define FOR_EACH_DPPI(_handle, _ch, _reg, _d_id)						\
	_reg = NRF_DPPIC;									\
	_d_id = 0;										\
	(void)_reg;										\
	_ch = _handle;
#endif

void nrf_dppi_conn_enable(nrf_dppi_handle_t handle)
{
	NRF_DPPIC_Type *reg;
	uint32_t ch;
	uint32_t d_id;

	FOR_EACH_DPPI(handle, ch, reg, d_id) {
		nrf_dppi_channels_enable(reg, BIT(ch));
	}
}

void nrf_dppi_conn_disable(nrf_dppi_handle_t handle)
{
	NRF_DPPIC_Type *reg;
	uint32_t ch;
	uint32_t d_id;

	FOR_EACH_DPPI(handle, ch, reg, d_id) {
		nrf_dppi_channels_disable(reg, BIT(ch));
	}
}

void nrf_dppi_chan_enable(uint32_t domain_id, uint32_t ch)
{
	nrf_dppi_channels_enable(dppi_reg_get(domain_id), BIT(ch));
}

void nrf_dppi_chan_disable(uint32_t domain_id, uint32_t ch)
{
	nrf_dppi_channels_disable(dppi_reg_get(domain_id), BIT(ch));
}

void nrf_dppi_ep_clear(uint32_t ep)
{
	NRF_DPPI_ENDPOINT_CLEAR(ep);
}

int nrf_dppi_ep_attach(nrf_dppi_handle_t handle, uint32_t ep)
{
	NRF_DPPIC_Type *reg;
	uint32_t d_id;
	uint32_t ch;
	uint32_t ep_d_id = nrf_dppi_get_domain_id(ep);

	FOR_EACH_DPPI(handle, ch, reg, d_id) {
		if (d_id == ep_d_id) {
			NRF_DPPI_ENDPOINT_SETUP(ep, ch);
			return 0;
		}
	}

	return -EINVAL;
}

int nrf_dppi_group_alloc(uint32_t *ep, size_t ep_cnt, nrf_dppi_group_handle_t *handle)
{
	uint32_t domain_id = nrf_dppi_get_domain_id(ep[0]);
	NRF_DPPIC_Type * reg = dppi_reg_get(domain_id);
	atomic_t *group_mask = get_group_chan_mask(domain_id);
	uint32_t gch;
	uint32_t chan_mask = 0;
	int key;
	int rv;

	for (size_t i = 1; i < ep_cnt; i++) {
		if (nrf_dppi_get_domain_id(ep[0]) != nrf_dppi_get_domain_id(ep[i])) {
			return -EINVAL;
		}
	}

	key = irq_lock();
	if (*group_mask == 0) {
		rv = -ENOMEM;
	} else {
		gch = alloc_bit_locked(group_mask);
		rv = 0;
	}
	irq_unlock(key);

	if (rv < 0) {
		return rv;
	}

	for (size_t i = 0; i < ep_cnt; i++) {
		int ch = nrf_dppi_ep_channel(ep[i]);

		if (ch >= 0) {
			chan_mask |= BIT(ch);
		}
	}

	nrf_dppi_channels_group_set(reg, chan_mask, gch);
	*handle = (domain_id << 8) | gch;

	return 0;
}

void nrf_dppi_group_free(nrf_dppi_group_handle_t handle)
{
	uint32_t domain_id = GHANDLE_GET_DOMAIN(handle);
	uint32_t ch = GHANDLE_GET_CHAN(handle);
	atomic_t *group_mask = get_group_chan_mask(domain_id);
	NRF_DPPIC_Type *reg = dppi_reg_get(domain_id);

	nrf_dppi_group_clear(reg, ch);
	atomic_or(group_mask, BIT(ch));
}

void nrf_dppi_group_ch_add(nrf_dppi_group_handle_t handle, uint32_t ch)
{
	NRF_DPPIC_Type *reg = dppi_reg_get(GHANDLE_GET_DOMAIN(handle));
	uint32_t group = GHANDLE_GET_CHAN(handle);

	nrf_dppi_channels_include_in_group(reg, BIT(ch), group);
}

void nrf_dppi_group_ch_remove(nrf_dppi_group_handle_t handle, uint32_t ch)
{
	NRF_DPPIC_Type *reg = dppi_reg_get(GHANDLE_GET_DOMAIN(handle));
	uint32_t group = GHANDLE_GET_CHAN(handle);

	nrf_dppi_channels_remove_from_group(reg, BIT(ch), group);
}

void nrf_dppi_group_en(nrf_dppi_group_handle_t handle)
{
	NRF_DPPIC_Type *reg = dppi_reg_get(GHANDLE_GET_DOMAIN(handle));
	uint32_t group = GHANDLE_GET_CHAN(handle);

	nrf_dppi_group_enable(reg, group);
}

void nrf_dppi_group_dis(nrf_dppi_group_handle_t handle)
{
	NRF_DPPIC_Type *reg = dppi_reg_get(GHANDLE_GET_DOMAIN(handle));
	uint32_t group = GHANDLE_GET_CHAN(handle);

	nrf_dppi_group_disable(reg, group);
}

uint32_t nrf_dppi_group_task_en_addr(nrf_dppi_group_handle_t handle)
{
	NRF_DPPIC_Type *reg = dppi_reg_get(GHANDLE_GET_DOMAIN(handle));
	uint32_t group = GHANDLE_GET_CHAN(handle);

	return nrf_dppi_task_address_get(reg, nrf_dppi_group_enable_task_get(group));
}

uint32_t nrf_dppi_group_task_dis_addr(nrf_dppi_group_handle_t handle)
{
	NRF_DPPIC_Type *reg = dppi_reg_get(GHANDLE_GET_DOMAIN(handle));
	uint32_t group = GHANDLE_GET_CHAN(handle);

	return nrf_dppi_task_address_get(reg, nrf_dppi_group_disable_task_get(group));
}

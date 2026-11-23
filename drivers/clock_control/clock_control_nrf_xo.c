/*
 * Copyright (c) 2016-2025 Nordic Semiconductor ASA
 * Copyright (c) 2016 Vinayak Kariappa Chettimada
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <soc.h>
#include <zephyr/sys/onoff.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include "nrf_clock_calibration.h"
#include <nrfx_clock_xo.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/irq.h>
#include "clock_control_nrf_common.h"

LOG_MODULE_REGISTER(clock_control_xo, CONFIG_CLOCK_CONTROL_LOG_LEVEL);

#define DT_DRV_COMPAT nordic_nrf_clock_xo

#define CLOCK_DEVICE_XO DEVICE_DT_GET_ONE(nordic_nrf_clock_xo)

/* Used only by HF clock */
#define XO_USER_BT      BIT(0)
#define XO_USER_GENERIC BIT(1)

/* This is an extension to enum clock_control_status */
#define CLOCK_CONTROL_STATUS_STARTED (CLOCK_CONTROL_STATUS_UNKNOWN + 1)

typedef struct {
	common_clock_data_t data;
#if CONFIG_CLOCK_CONTROL_NRF_ONOFF
	struct onoff_manager mgr_started;
	clock_control_cb_t cb_started;
	uint8_t active_mgrs;
#endif
} xo_clock_data_t;

static atomic_t xo_users;
static atomic_t zli_clock_status;

#ifdef CONFIG_CLOCK_CONTROL_NRF_HFINT_CALIBRATION

static void nrf54l_errata_30_workaround(void)
{
	while (FIELD_GET(CLOCK_XO_STAT_STATE_Msk, NRF_CLOCK->XO.STAT) !=
	       CLOCK_XO_STAT_STATE_Running) {
	}
	const uint32_t higher_bits = *((volatile uint32_t *)0x50120820UL) & 0xFFFFFFC0;
	*((volatile uint32_t *)0x50120864UL) = 1 | BIT(31);
	*((volatile uint32_t *)0x50120848UL) = 1;
	uint32_t off_abs = 24;

	while (off_abs >= 24) {
		*((volatile uint32_t *)0x50120844UL) = 1;
		while (((*((volatile uint32_t *)0x50120840UL)) & (1 << 16)) != 0) {
		}
		const uint32_t current_cal = *((volatile uint32_t *)0x50120820UL) & 0x3F;
		const uint32_t cal_result = *((volatile uint32_t *)0x50120840UL) & 0x7FF;
		int32_t off = 1024 - cal_result;

		off_abs = (off < 0) ? -off : off;

		if (off >= 24 && current_cal < 0x3F) {
			*((volatile uint32_t *)0x50120820UL) = higher_bits | (current_cal + 1);
		} else if (off <= -24 && current_cal > 0) {
			*((volatile uint32_t *)0x50120820UL) = higher_bits | (current_cal - 1);
		}
	}

	*((volatile uint32_t *)0x50120848UL) = 0;
	*((volatile uint32_t *)0x50120864UL) = 0;
}

#if CONFIG_CLOCK_CONTROL_NRF_HFINT_CALIBRATION_PERIOD

static struct onoff_client hf_cal_cli;

static void calibration_finished_callback(struct onoff_manager *mgr, struct onoff_client *cli,
					  uint32_t state, int res)
{
	(void)onoff_cancel_or_release(mgr, cli);
}

static void calibration_handler(struct k_timer *timer)
{
	nrf_clock_hfclk_t clk_src;

	bool ret = nrfx_clock_xo_running_check(&clk_src);

	if (ret && (clk_src == NRF_CLOCK_HFCLK_HIGH_ACCURACY)) {
		return;
	}

	sys_notify_init_callback(&hf_cal_cli.notify, calibration_finished_callback);
	(void)onoff_request(&((common_clock_data_t *)CLOCK_DEVICE_XO->data)->mgr, &hf_cal_cli);
}

static K_TIMER_DEFINE(calibration_timer, calibration_handler, NULL);

static int calibration_init(void)
{
	k_timer_start(&calibration_timer, K_NO_WAIT,
		      K_MSEC(CONFIG_CLOCK_CONTROL_NRF_HFINT_CALIBRATION_PERIOD));

	return 0;
}

SYS_INIT(calibration_init, APPLICATION, 0);

#endif /* CONFIG_CLOCK_CONTROL_NRF_HFINT_CALIBRATION_PERIOD */
#endif /* CONFIG_CLOCK_CONTROL_NRF_HFINT_CALIBRATION */

void set_started_state(uint32_t *flags)
{
	unsigned int key = irq_lock();

	*flags = CLOCK_CONTROL_STATUS_STARTED | COMMON_GET_CTX(*flags);
	irq_unlock(key);
}

void clk_started_handle(const struct device *dev)
{
	clock_control_cb_t callback = ((xo_clock_data_t *)dev->data)->cb_started;

	((common_clock_data_t *)dev->data)->cb = NULL;
	set_started_state(&((common_clock_data_t *)dev->data)->flags);

	if (callback) {
		callback(dev, NULL, ((common_clock_data_t *)dev->data)->user_data);
	}
}

static void clk_ready_event_handle(void)
{
#if CONFIG_CLOCK_CONTROL_NRF_HFINT_CALIBRATION
		if (NRF_ERRATA_DYNAMIC_CHECK(54L, 30)) {
			nrf54l_errata_30_workaround();
		}
#endif
		common_clk_ready_handle(CLOCK_DEVICE_XO);
}

static void generic_xo_start(void)
{
	nrf_clock_hfclk_t type;
	bool clk_started = false;
	bool clk_ready = false;
	unsigned int key = irq_lock();
	
	if (atomic_or(&xo_users, XO_USER_GENERIC) & XO_USER_BT) {

		((common_clock_data_t *)CLOCK_DEVICE_XO->data)->flags =
			zli_clock_status | COMMON_GET_CTX(*flags);

		switch(((common_clock_data_t *)CLOCK_DEVICE_XO->data)->flags & COMMON_STATUS_MASK) {
			case CLOCK_CONTROL_STATUS_STARTED:
				clk_started = true;
				break;
			case CLOCK_CONTROL_STATUS_ON:
				clk_ready = true;
				break;
		}
	}

	irq_unlock(key);

	if (clk_started) {
		/* Clock already started by z_nrf_clock_bt_ctlr_hf_request */
		clk_started_event_handle();
		return;
	}

	if (clk_ready) {
		/* Clock already started by z_nrf_clock_bt_ctlr_hf_request */
		clk_ready_event_handle();
		return;
	}

	nrfx_clock_xo_start();
}

static void generic_xo_stop(void)
{
	/* It's not enough to use only atomic_and() here for synchronization,
	 * as the thread could be preempted right after that function but
	 * before nrfx_clock_xo_stop() is called and the preempting code could request
	 * the XO again. Then, the XO would be stopped inappropriately
	 * and xo_user would be left with an incorrect value.
	 */
	unsigned int key = irq_lock();

	xo_users &= ~XO_USER_GENERIC;
	/* Skip stopping if BT is still requesting the clock. */
	if (!(xo_users & XO_USER_BT)) {
		nrfx_clock_xo_stop();
	}

	irq_unlock(key);
}

/* XO STARTED state cannot be read from register like XO TUNED can,
 * so to keep track of clock state in ZLI separate status system has to be added.
 */
static void clock_event_zli_handler(nrfx_clock_xo_event_type_t event)
{
	unsigned int key = irq_lock();
	switch (event) {
#if NRF_CLOCK_HAS_XO_TUNE
	case NRFX_CLOCK_XO_EVT_XO_TUNED:
		atomic_set(&zli_clock_status, CLOCK_CONTROL_STATUS_ON)
		break;
	case NRFX_CLOCK_XO_EVT_HFCLK_STARTED:
		atomic_set(&zli_clock_status, CLOCK_CONTROL_STATUS_STARTED)
		break;
#else
	/* HFCLK started should be used only if tune operation is done implicitly. */
	case NRFX_CLOCK_XO_EVT_HFCLK_STARTED:
		atomic_set(&zli_clock_status, CLOCK_CONTROL_STATUS_ON)
		break;
#endif
	}

	if(!(xo_users & XO_USER_BT)) {
		atomic_set(&zli_clock_status, CLOCK_CONTROL_STATUS_OFF);
	}

	irq_unlock(key);
}

static void clock_event_handler(nrfx_clock_xo_event_type_t event)
{
	if(xo_users & XO_USER_BT) {
		clock_event_zli_handler(event);
	}

	if(!(xo_users & XO_USER_GENERIC)) {
		return;
	}

	switch (event) {
#if NRF_CLOCK_HAS_XO_TUNE
	case NRFX_CLOCK_XO_EVT_XO_TUNED:
		clk_ready_event_handle();
		break;
	case NRFX_CLOCK_XO_EVT_XO_TUNE_ERROR:
	case NRFX_CLOCK_XO_EVT_XO_TUNE_FAILED:
		/* No processing needed. */
		break;
	case NRFX_CLOCK_XO_EVT_HFCLK_STARTED:
		/* HFCLK is stable after XOTUNED event.
		 * HFCLK_STARTED means only that clock has been started.
		 */
		clk_started_handle();
		break;
#else
	/* HFCLK started should be used only if tune operation is done implicitly. */
	case NRFX_CLOCK_XO_EVT_HFCLK_STARTED: {
		clk_ready_event_handle();
		break;
	}
#endif

#if NRF_CLOCK_HAS_PLL
	case NRFX_CLOCK_XO_EVT_PLL_STARTED:
		/* No processing needed. */
		break;
#endif
	default:
		__ASSERT_NO_MSG(0);
		break;
	}
}

void z_nrf_clock_bt_ctlr_hf_request(void)
{
	if (atomic_or(&xo_users, XO_USER_BT) & XO_USER_GENERIC) {
		/* generic request already activated clock. */
		return;
	}

	nrfx_clock_xo_start();
}

void z_nrf_clock_bt_ctlr_hf_release(void)
{
	/* It's not enough to use only atomic_and() here for synchronization,
	 * see the explanation in generic_hfclk_stop().
	 */
	unsigned int key = irq_lock();

	xo_users &= ~XO_USER_BT;
	/* Skip stopping if generic is still requesting the clock. */
	if (!(xo_users & XO_USER_GENERIC)) {
		nrfx_clock_xo_stop();
	}

	irq_unlock(key);
}

#if DT_NODE_EXISTS(DT_NODELABEL(hfxo))
uint32_t z_nrf_clock_bt_ctlr_hf_get_startup_time_us(void)
{
	return DT_PROP(DT_NODELABEL(hfxo), startup_time_us);
}
#endif

#if CONFIG_CLOCK_CONTROL_NRF_ONOFF

static void onoff_started_callback(const struct device *dev, clock_control_subsys_t sys,
				   void *user_data)
{
	ARG_UNUSED(sys);

	onoff_notify_fn notify = user_data;

	notify(&((xo_clock_data_t *)dev->data)->mgr_started, 0);
}

static struct onoff_manager *get_manager_from_spec(const struct device *dev,
						      const struct nrf_clock_spec *spec)
{
	if (spec == NULL || spec->precission == NRF_CLOCK_CONTROL_PRECISION_HIGH) {
		return &((common_clock_data_t *)dev->data)->mgr;
	} else {
		return &((xo_clock_data_t *)dev->data)->mgr_started;
	}
}

/* There are two set of onoff_managers, internal clients and counter counting active clients for each.
 * First manager (lower quality) has start and stop implemented to start and stop xo and completion callback when HFCLKSTARTED event is received.
 * Second manager (higher quality) has empty start and stop functions and on callback when XO_TUNED event is received.
 * User always requests one (primary set) and secondary set is handled only if it is the first request or last release.
*/
static bool is_hi_precision(const struct nrf_clock_spec *spec)
{
	return spec == NULL || spec->precission == NRF_CLOCK_CONTROL_PRECISION_HIGH;
}

static int api_request(const struct device *dev, const struct nrf_clock_spec *spec,
		       struct onoff_client *cli)
{
	common_clock_data_t *common_dev_data = CONTAINER_OF(dev, common_clock_data_t, dev);
	xo_clock_data_t *xo_dev_data = CONTAINER_OF(common_dev_data, xo_clock_data_t, data);
	int rv;
	uint32_t primary, secondary;

	if (is_hi_precision(spec)) {
		primary = 0;
		secondary = 1;
	} else {
		primary = 1;
		secondary = 0;
	}

	if (atomic_inc(&xo_dev_data->active_mgrs[secondary]) == 0) {
		/* if it is the first hi precision request that request low precision mgr. */
		sys_notify_init_spinlock(&xo_dev_data->internal_cli[secondary]);
		/* That will trigger the start of hclk*/
		rv = onoff_request(&xo_dev_data->mgr[secondary], &xo_dev_data->internal_cli[secondary]);
		if (rv < 0) {
			atomic_dec(&xo_dev_data->active_mgrs[secondary]);
			return rv;
		}
	}
	return onoff_request(&xo_dev_data->mgr[primary], cli);
}

static int api_release(const struct device *dev, const struct nrf_clock_spec *spec)
{
	common_clock_data_t *common_dev_data = CONTAINER_OF(dev, common_clock_data_t, dev);
	xo_clock_data_t *xo_dev_data = CONTAINER_OF(common_dev_data, xo_clock_data_t, data);
	uint32_t primary, secondary;

	if (is_hi_precision(spec)) {
		primary = 0;
		secondary = 1;
	} else {
		primary = 1;
		secondary = 0;
	}

	if (atomic_dec(&xo_dev_data->active_mgrs[secondary]) == 1) {
		onoff_release(&xo_dev_data->mgr[secondary]);
	}
	return onoff_release(&xo_dev_data->mgr[primary]);
}

static int api_cancel_or_release(const struct device *dev, const struct nrf_clock_spec *spec,
				 struct onoff_client *cli)
{
	common_clock_data_t *common_dev_data = CONTAINER_OF(dev, common_clock_data_t, dev);
	xo_clock_data_t *xo_dev_data = CONTAINER_OF(common_dev_data, xo_clock_data_t, data);
	uint32_t primary, secondary;

	if (is_hi_precision(spec)) {
		primary = 0;
		secondary = 1;
	} else {
		primary = 1;
		secondary = 0;
	}

	if (atomic_dec(&xo_dev_data->active_mgrs[secondary]) == 1) {
		onoff_cancel_or_release(&xo_dev_data->mgr[secondary], &xo_dev_data->internal_cli[secondary]);
	}

	return onoff_cancel_or_release(&xo_dev_data->mgr[primary], cli);
}

static void onoff_start(struct onoff_manager *manager, onoff_notify_fn notify)
{
	int err;
	bool start = false;
	bool call_cb = false;
	clock_control_cb_t* cb_ptr;
	clock_control_cb_t cb;
	common_clock_data_t *common_dev_data = CONTAINER_OF(manager, common_clock_data_t, mgr);
	xo_clock_data_t *xo_dev_data = CONTAINER_OF(common_dev_data, xo_clock_data_t, data);
	const struct device *dev = common_dev_data->dev;
	bool started_mgr = (manager == &xo_dev_data->mgr_started);

	if(started_mgr) {
		cb = onoff_started_callback;
		cb_ptr = &xo_dev_data->cb_started;
	} else {
		cb = common_onoff_ready_callback;
		cb_ptr = &common_dev_data->cb;
	}

	unsigned int key = irq_lock();
	uint32_t current_ctx = COMMON_GET_CTX(*flags);

	xo_dev_data->active_mgrs++;

	if ((*flags & (COMMON_STATUS_MASK)) == CLOCK_CONTROL_STATUS_OFF) {
		*flags = CLOCK_CONTROL_STATUS_STARTING | ctx;
		*cb_ptr = cb;
		common_dev_data->user_data = notify;
		start = true;
	} else if (current_ctx != ctx) {
		err = -EPERM;
	} else if((*flags & (COMMON_STATUS_MASK)) == CLOCK_CONTROL_STATUS_STARTING) {
		*cb_ptr = cb;
		common_dev_data->user_data = notify;
	} else if((*flags & (COMMON_STATUS_MASK)) == CLOCK_CONTROL_STATUS_STARTED) {
		common_dev_data->user_data = notify;

		if(started_mgr) {
			call_cb = true;
		} else {
			*cb_ptr = cb;
		}
	} else { /* CLOCK_CONTROL_STATUS_ON */
		common_dev_data->user_data = notify;
		call_cb = true;
	}

	irq_unlock(key);

	if(err < 0) {
		notify(manager, err);
		return;
	}

	if(start) {
		dev->config->start();
	}

	if(call_cb) {
		cb(dev, NULL, common_dev_data->user_data);
	}
}

static void onoff_stop(struct onoff_manager *manager, onoff_notify_fn notify)
{
	int res;
	bool stop = false;
	clock_control_cb_t* cb_ptr;
	common_clock_data_t *common_dev_data = CONTAINER_OF(manager, common_clock_data_t, mgr);
	xo_clock_data_t *xo_dev_data = CONTAINER_OF(common_dev_data, xo_clock_data_t, data);
	bool started_mgr = (manager == &xo_dev_data->mgr_started);

	if(started_mgr) {
		cb_ptr = &xo_dev_data->cb_started;
	} else {
		cb_ptr = &common_dev_data->cb;
	}

	unsigned int key = irq_lock();

	xo_dev_data->active_mgrs--;
	*cb_ptr = NULLl;

	if(xo_dev_data->active_mgrs == 0) {
		stop = true;
	}

	irq_unlock(key);

	if(stop) {
		res = common_stop(common_dev_data->dev, COMMON_CTX_ONOFF);
	}
	notify(manager, res);
}

#endif /* CONFIG_CLOCK_CONTROL_NRF_ONOFF */

static int clk_init(const struct device *dev)
{
#if CONFIG_CLOCK_CONTROL_NRF_ONOFF
	int err;
	static const struct onoff_transitions transitions = {.start = onoff_start,
							     .stop = onoff_stop};
#endif

	if (nrfx_clock_xo_init(clock_event_handler) != 0) {
		return -EIO;
	}

	((common_clock_data_t *)dev->data)->dev = dev;

	common_connect_irq();
#if CONFIG_CLOCK_CONTROL_NRF_ONOFF
	err = onoff_manager_init(&((common_clock_data_t *)dev->data)->mgr,
				 &transitions);
	if (err < 0) {
		return err;
	}

	err = onoff_manager_init(&((xo_clock_data_t *)dev->data)->mgr_started,
				 &transitions);
	if (err < 0) {
		return err;
	}
#endif

	((common_clock_data_t *)dev->data)->flags = CLOCK_CONTROL_STATUS_OFF;

	return 0;
}

CLOCK_CONTROL_NRF_IRQ_HANDLERS_ITERABLE(clock_control_nrf_xo, &nrfx_clock_xo_irq_handler);

extern struct nrf_clock_control_driver_api common_clock_control_api;

DEVICE_API(nrf_clock_control_xo, xo_clock_control_api) = {
	.std_api = {
		.on = common_api_blocking_start,
		.off = common_api_stop,
		.async_on = common_api_start,
		.get_status = common_api_get_status,
	},
#if CONFIG_CLOCK_CONTROL_NRF_ONOFF
	.request = api_request,
	.release = api_release,
	.cancel_or_release = api_cancel_or_release,
#endif
};

static xo_clock_data_t data;

static const common_clock_config_t config = {
	.start = generic_xo_start,
	.stop = generic_xo_stop,
};

DEVICE_DT_DEFINE(DT_COMPAT_GET_ANY_STATUS_OKAY(nordic_nrf_clock_xo), clk_init, NULL, &data, &config,
		 PRE_KERNEL_1, CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &common_clock_control_api);

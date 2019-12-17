/*
 * Copyright (c) 2016-2019 Nordic Semiconductor ASA
 * Copyright (c) 2016 Vinayak Kariappa Chettimada
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <soc.h>
#include <drivers/clock_control.h>
#include <drivers/clock_control/nrf_clock_control.h>
#include "nrf_clock_calibration.h"
#include <logging/log.h>
#include <hal/nrf_power.h>
#include <helpers/nrfx_gppi.h>
#ifdef DPPI_PRESENT
#include <nrfx_dppi.h>
#else
#include <nrfx_ppi.h>
#endif

LOG_MODULE_REGISTER(clock_control, CONFIG_CLOCK_CONTROL_LOG_LEVEL);

/* Helper logging macros which prepends device name to the log. */
#define CLOCK_LOG(lvl, dev, ...) \
	LOG_##lvl("%s: " GET_ARG1(__VA_ARGS__), dev->config->name \
			COND_CODE_0(NUM_VA_ARGS_LESS_1(__VA_ARGS__),\
					(), (, GET_ARGS_LESS_1(__VA_ARGS__))))
#define ERR(dev, ...) CLOCK_LOG(ERR, dev, __VA_ARGS__)
#define WRN(dev, ...) CLOCK_LOG(WRN, dev, __VA_ARGS__)
#define INF(dev, ...) CLOCK_LOG(INF, dev, __VA_ARGS__)
#define DBG(dev, ...) CLOCK_LOG(DBG, dev, __VA_ARGS__)

/* returns true if clock stopping or starting can be performed. If false then
 * start/stop will be deferred and performed later on by handler owner.
 */
typedef bool (*nrf_clock_handler_t)(struct device *dev);

/* Clock instance structure */
struct nrf_clock_control {
	sys_slist_t list;	/* List of users requesting callback */
	s8_t ref;		/* Users counter */
	bool started;		/* Indicated that clock is started */
};

/* Clock instance static configuration */
struct nrf_clock_control_config {
	nrf_clock_handler_t start_handler; /* Called before start */
	nrf_clock_handler_t stop_handler; /* Called before stop */
	nrf_clock_event_t started_evt;	/* Clock started event */
	nrf_clock_task_t start_tsk;	/* Clock start task */
	nrf_clock_task_t stop_tsk;	/* Clock stop task */
};

struct clock_ctrl_service {
	struct onoff_service service;
	onoff_service_notify_fn started_cb;
	nrf_clock_task_t start_tsk;	/* Clock start task */
	nrf_clock_task_t stop_tsk;	/* Clock stop task */
	nrf_clock_event_t started_evt;	/* Clock started event */
	bool started;
	bool srv_req;
	bool ppi_req;
	u8_t ppi_ch;
};

/** @brief Allocate (D)PPI channel. */
static nrfx_err_t ppi_alloc(u8_t *ch)
{
#ifdef DPPI_PRESENT
	return nrfx_dppi_channel_alloc(ch);
#else
	return nrfx_ppi_channel_alloc(ch);
#endif
}

static void started(struct clock_ctrl_service *clk_srv)
{
	clk_srv->started = true;
	if (clk_srv->srv_req) {
		clk_srv->started_cb(&clk_srv->service, 0);
	}
}

int z_clock_control_nrf_ppi_request(struct device *dev,
				   clock_control_subsys_t sys, u32_t evt)
{
	struct clock_ctrl_service *clk_srv = dev->driver_data;
	int err = 0;
	int key;

	key = irq_lock();
	if (clk_srv->ppi_req) {
		err = -EBUSY;
		goto out;
	}
	clk_srv->ppi_req = true;
	nrfx_gppi_event_endpoint_setup(clk_srv->ppi_ch, evt);
	nrfx_gppi_channels_enable(BIT(clk_srv->ppi_ch));
out:
	irq_unlock(key);
	DBG(dev, "ppi req ch:%d, evt:%x, err:%d", clk_srv->ppi_ch, evt, err);

	return err;
}

int z_clock_control_nrf_ppi_release(struct device *dev,
				   clock_control_subsys_t sys)
{
	struct clock_ctrl_service *clk_srv = dev->driver_data;
	int key = irq_lock();

	if (!clk_srv->ppi_req) {
		return -EALREADY;
	}

	nrfx_gppi_channels_disable(BIT(clk_srv->ppi_ch));
	clk_srv->ppi_req = false;
	if (clk_srv->srv_req == false) {
		nrf_clock_task_trigger(NRF_CLOCK, clk_srv->stop_tsk);
		clk_srv->started = false;
	}

	irq_unlock(key);
	return 0;
}

static void clk_start(struct onoff_service *srv,
			onoff_service_notify_fn notify)
{
	struct clock_ctrl_service *clk_srv =
			CONTAINER_OF(srv, struct clock_ctrl_service, service);
	int key = irq_lock();

	clk_srv->srv_req = true;
	if (clk_srv->started == false) {
		irq_unlock(key);
		clk_srv->started_cb = notify;
		nrf_clock_task_trigger(NRF_CLOCK, clk_srv->start_tsk);
		return;
	}
	irq_unlock(key);

	notify(srv, 0);

}

static void clk_stop(struct onoff_service *srv,
			onoff_service_notify_fn notify)
{
	struct clock_ctrl_service *clk_srv =
			CONTAINER_OF(srv, struct clock_ctrl_service, service);

	clk_srv->srv_req = false;
	if (clk_srv->ppi_req == false) {
		nrf_clock_task_trigger(NRF_CLOCK, clk_srv->stop_tsk);
		clk_srv->started = false;
	}

	if (notify) {
		notify(srv, 0);
	}
}

static enum clock_control_status get_status(struct device *dev,
					    clock_control_subsys_t sys)
{
	struct clock_ctrl_service *clk_srv = dev->driver_data;

	if (clk_srv->started) {
		return CLOCK_CONTROL_STATUS_ON;
	}

	if (clk_srv->service.refs > 0) {
		return CLOCK_CONTROL_STATUS_STARTING;
	}

	return CLOCK_CONTROL_STATUS_OFF;
}

static int request(struct device *dev, clock_control_subsys_t sys,
		   struct onoff_client *client)
{
	struct clock_ctrl_service *clk_srv = dev->driver_data;

	return onoff_request(&clk_srv->service, client);
}

static int release(struct device *dev, clock_control_subsys_t sys,
		   struct onoff_client *client)
{
	struct clock_ctrl_service *clk_srv = dev->driver_data;

	return onoff_release(&clk_srv->service, client);
}

/* Note: this function has public linkage, and MUST have this
 * particular name.  The platform architecture itself doesn't care,
 * but there is a test (tests/kernel/arm_irq_vector_table) that needs
 * to find it to it can set it in a custom vector table.  Should
 * probably better abstract that at some point (e.g. query and reset
 * it by pointer at runtime, maybe?) so we don't have this leaky
 * symbol.
 */
void nrf_power_clock_isr(void *arg);

static int srv_init(struct onoff_service *srv)
{
	return onoff_service_init(srv, clk_start, clk_stop, NULL, 0);
}

static int hfclk_init(struct device *dev)
{
	struct clock_ctrl_service *clk_srv = dev->driver_data;
	nrfx_err_t err;

	IRQ_CONNECT(DT_INST_0_NORDIC_NRF_CLOCK_IRQ_0,
		    DT_INST_0_NORDIC_NRF_CLOCK_IRQ_0_PRIORITY,
		    nrf_power_clock_isr, 0, 0);

	irq_enable(DT_INST_0_NORDIC_NRF_CLOCK_IRQ_0);

	nrf_clock_lf_src_set(NRF_CLOCK, CLOCK_CONTROL_NRF_K32SRC);

	if (IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION)) {
		z_nrf_clock_calibration_init(dev);
	}

	nrf_clock_int_enable(NRF_CLOCK,
		(NRF_CLOCK_INT_HF_STARTED_MASK |
		 NRF_CLOCK_INT_LF_STARTED_MASK |
		 COND_CODE_1(CONFIG_USB_NRFX,
			(NRF_POWER_INT_USBDETECTED_MASK |
			 NRF_POWER_INT_USBREMOVED_MASK |
			 NRF_POWER_INT_USBPWRRDY_MASK),
			(0))));

	clk_srv->start_tsk = NRF_CLOCK_TASK_HFCLKSTART;
	clk_srv->stop_tsk = NRF_CLOCK_TASK_HFCLKSTOP;
	clk_srv->started_evt = NRF_CLOCK_EVENT_HFCLKSTARTED;
	err = ppi_alloc(&clk_srv->ppi_ch);
	if (err != NRFX_SUCCESS) {
		return -ENODEV;
	}

	nrfx_gppi_task_endpoint_setup(clk_srv->ppi_ch,
		nrf_clock_task_address_get(NRF_CLOCK, clk_srv->start_tsk));

	return srv_init(&clk_srv->service);
}

static int lfclk_init(struct device *dev)
{
	struct clock_ctrl_service *clk_srv = dev->driver_data;
	nrfx_err_t err;

	clk_srv->start_tsk = NRF_CLOCK_TASK_LFCLKSTART;
	clk_srv->stop_tsk = NRF_CLOCK_TASK_LFCLKSTOP;
	clk_srv->started_evt = NRF_CLOCK_EVENT_LFCLKSTARTED;
	err = ppi_alloc(&clk_srv->ppi_ch);
	if (err != NRFX_SUCCESS) {
		return -ENODEV;
	}
	nrfx_gppi_task_endpoint_setup(clk_srv->ppi_ch, clk_srv->start_tsk);
	return srv_init(&clk_srv->service);
}

static const struct clock_control_driver_api clock_control_api = {
	.request = request,
	.release = release,
	.get_status = get_status,
};

static struct clock_ctrl_service lfclk_ctrl_service;
static struct clock_ctrl_service hfclk_ctrl_service;

DEVICE_AND_API_INIT(clock_nrf5_m16src,
		    DT_INST_0_NORDIC_NRF_CLOCK_LABEL  "_16M",
		    hfclk_init, &hfclk_ctrl_service, NULL, PRE_KERNEL_1,
		    CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &clock_control_api);

DEVICE_AND_API_INIT(clock_nrf5_k32src,
		    DT_INST_0_NORDIC_NRF_CLOCK_LABEL  "_32k",
		    lfclk_init, &lfclk_ctrl_service, NULL, PRE_KERNEL_1,
		    CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &clock_control_api);



#if defined(CONFIG_USB_NRFX)
static bool power_event_check_and_clean(nrf_power_event_t evt, u32_t intmask)
{
	bool ret = nrf_power_event_check(NRF_POWER, evt) &&
			nrf_power_int_enable_check(NRF_POWER, intmask);

	if (ret) {
		nrf_power_event_clear(NRF_POWER, evt);
	}

	return ret;
}
#endif

static void usb_power_isr(void)
{
#if defined(CONFIG_USB_NRFX)
	extern void usb_dc_nrfx_power_event_callback(nrf_power_event_t event);

	if (power_event_check_and_clean(NRF_POWER_EVENT_USBDETECTED,
					NRF_POWER_INT_USBDETECTED_MASK)) {
		usb_dc_nrfx_power_event_callback(NRF_POWER_EVENT_USBDETECTED);
	}

	if (power_event_check_and_clean(NRF_POWER_EVENT_USBPWRRDY,
					NRF_POWER_INT_USBPWRRDY_MASK)) {
		usb_dc_nrfx_power_event_callback(NRF_POWER_EVENT_USBPWRRDY);
	}

	if (power_event_check_and_clean(NRF_POWER_EVENT_USBREMOVED,
					NRF_POWER_INT_USBREMOVED_MASK)) {
		usb_dc_nrfx_power_event_callback(NRF_POWER_EVENT_USBREMOVED);
	}
#endif
}

/* Return true if given event has enabled interrupt and is triggered. Event
 * is cleared.
 */
static bool clock_event_check_and_clean(nrf_clock_event_t evt, u32_t intmask)
{
	bool ret = nrf_clock_event_check(NRF_CLOCK, evt) &&
			nrf_clock_int_enable_check(NRF_CLOCK, intmask);

	if (ret) {
		nrf_clock_event_clear(NRF_CLOCK, evt);
	}

	return ret;
}

void nrf_power_clock_isr(void *arg)
{
	ARG_UNUSED(arg);

	if (clock_event_check_and_clean(NRF_CLOCK_EVENT_HFCLKSTARTED,
					NRF_CLOCK_INT_HF_STARTED_MASK)) {
		struct device *dev = DEVICE_GET(clock_nrf5_m16src);
		struct clock_ctrl_service *clk_srv = dev->driver_data;

		DBG(dev, "started");
		started(clk_srv);
	}

	if (clock_event_check_and_clean(NRF_CLOCK_EVENT_LFCLKSTARTED,
					NRF_CLOCK_INT_LF_STARTED_MASK)) {
		struct device *dev = DEVICE_GET(clock_nrf5_k32src);
		struct clock_ctrl_service *clk_srv = dev->driver_data;

		if (IS_ENABLED(
			CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION)) {
			z_nrf_clock_calibration_lfclk_started(dev);
		}

		DBG(dev, "started");
		started(clk_srv);
	}

	usb_power_isr();

	if (IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION)) {
		z_nrf_clock_calibration_isr();
	}
}

#ifdef CONFIG_USB_NRFX
void nrf5_power_usb_power_int_enable(bool enable)
{
	u32_t mask;

	mask = NRF_POWER_INT_USBDETECTED_MASK |
	       NRF_POWER_INT_USBREMOVED_MASK |
	       NRF_POWER_INT_USBPWRRDY_MASK;

	if (enable) {
		nrf_power_int_enable(NRF_POWER, mask);
		irq_enable(DT_INST_0_NORDIC_NRF_CLOCK_IRQ_0);
	} else {
		nrf_power_int_disable(NRF_POWER, mask);
	}
}
#endif

/*
 * Copyright (c) 2016-2019 Nordic Semiconductor ASA
 * Copyright (c) 2016 Vinayak Kariappa Chettimada
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <soc.h>
#include <sys/onoff.h>
#include <drivers/clock_control.h>
#include <drivers/clock_control/nrf_clock_control.h>
#include "nrf_clock_calibration.h"
#include <logging/log.h>
#include <hal/nrf_power.h>

LOG_MODULE_REGISTER(clock_control, CONFIG_CLOCK_CONTROL_LOG_LEVEL);

#define NRF_CLOCK_CONTROL_FLAG_ONOFF_USED BIT(1)
#define NRF_CLOCK_CONTROL_FLAG_DIRECT_USED BIT(2)

/* Helper logging macros which prepends subsys name to the log. */
#ifdef CONFIG_LOG
#define CLOCK_LOG(lvl, dev, subsys, ...) \
	LOG_##lvl("%s: " GET_ARG1(__VA_ARGS__), \
		get_sub_config(dev, (enum clock_control_nrf_type)subsys)->name \
		COND_CODE_0(NUM_VA_ARGS_LESS_1(__VA_ARGS__),\
				(), (, GET_ARGS_LESS_1(__VA_ARGS__))))
#else
#define CLOCK_LOG(...)
#endif

#define ERR(dev, subsys, ...) CLOCK_LOG(ERR, dev, subsys, __VA_ARGS__)
#define WRN(dev, subsys, ...) CLOCK_LOG(WRN, dev, subsys, __VA_ARGS__)
#define INF(dev, subsys, ...) CLOCK_LOG(INF, dev, subsys, __VA_ARGS__)
#define DBG(dev, subsys, ...) CLOCK_LOG(DBG, dev, subsys, __VA_ARGS__)

/* returns true if clock stopping or starting can be performed. If false then
 * start/stop will be deferred and performed later on by handler owner.
 */
typedef bool (*nrf_clock_handler_t)(struct device *dev);

/* Clock subsys structure */
struct nrf_clock_control_sub_data {
	clock_control_cb_t cb;
	void *user_data;
	enum clock_control_status status;
	u8_t flags;
};

/* Clock subsys static configuration */
struct nrf_clock_control_sub_config {
	nrf_clock_handler_t start_handler; /* Called before start */
	nrf_clock_handler_t stop_handler; /* Called before stop */
	nrf_clock_event_t started_evt;	/* Clock started event */
	nrf_clock_task_t start_tsk;	/* Clock start task */
	nrf_clock_task_t stop_tsk;	/* Clock stop task */
#ifdef CONFIG_LOG
	const char *name;
#endif
};

struct nrf_clock_control_data {
	struct onoff_service onoff_service[CLOCK_CONTROL_NRF_TYPE_COUNT];
	struct nrf_clock_control_sub_data subsys[CLOCK_CONTROL_NRF_TYPE_COUNT];
};

struct nrf_clock_control_config {
	struct nrf_clock_control_sub_config
					subsys[CLOCK_CONTROL_NRF_TYPE_COUNT];
};

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

static void clock_irqs_enable(void)
{
	nrf_clock_int_enable(NRF_CLOCK,
			(NRF_CLOCK_INT_HF_STARTED_MASK |
			 NRF_CLOCK_INT_LF_STARTED_MASK |
			 COND_CODE_1(CONFIG_USB_NRFX,
				(NRF_POWER_INT_USBDETECTED_MASK |
				 NRF_POWER_INT_USBREMOVED_MASK |
				 NRF_POWER_INT_USBPWRRDY_MASK),
				(0))));
}

static struct nrf_clock_control_sub_data *get_sub_data(struct device *dev,
					      enum clock_control_nrf_type type)
{
	struct nrf_clock_control_data *data = dev->driver_data;

	return &data->subsys[type];
}

static const struct nrf_clock_control_sub_config *get_sub_config(
					struct device *dev,
					enum clock_control_nrf_type type)
{
	const struct nrf_clock_control_config *config =
						dev->config->config_info;

	return &config->subsys[type];
}

static struct onoff_service *get_onoff_service(struct device *dev,
					enum clock_control_nrf_type type)
{
	struct nrf_clock_control_data *data = dev->driver_data;

	return &data->onoff_service[type];
}

DEVICE_DECLARE(clock_nrf);

struct onoff_service *z_nrf_clock_control_get_onoff(clock_control_subsys_t sys)
{
	return get_onoff_service(DEVICE_GET(clock_nrf),
	 			(enum clock_control_nrf_type)sys);
}

static enum clock_control_status get_status(struct device *dev,
					    clock_control_subsys_t subsys)
{
	enum clock_control_nrf_type type = (enum clock_control_nrf_type)subsys;

	__ASSERT_NO_MSG(type < CLOCK_CONTROL_NRF_TYPE_COUNT);

	return get_sub_data(dev, type)->status;
}


static int clock_stop(struct device *dev, clock_control_subsys_t subsys)
{
	enum clock_control_nrf_type type = (enum clock_control_nrf_type)subsys;

	__ASSERT_NO_MSG(type < CLOCK_CONTROL_NRF_TYPE_COUNT);
	get_sub_data(dev, type)->status = CLOCK_CONTROL_STATUS_OFF;
	nrf_clock_task_trigger(NRF_CLOCK, get_sub_config(dev, type)->stop_tsk);

	return 0;
}

static inline void anomaly_132_workaround(void)
{
#if (CONFIG_NRF52_ANOMALY_132_DELAY_US - 0)
	static bool once;

	if (!once) {
		k_busy_wait(CONFIG_NRF52_ANOMALY_132_DELAY_US);
		once = true;
	}
#endif
}

static int async_start(struct device *dev, clock_control_subsys_t subsys,
			struct clock_control_async_data *data)
{
	enum clock_control_nrf_type type = (enum clock_control_nrf_type)subsys;
	struct nrf_clock_control_sub_data *subdata = get_sub_data(dev, type);

	subdata->cb = data->cb;
	subdata->user_data = data->user_data;

	if (IS_ENABLED(CONFIG_NRF52_ANOMALY_132_WORKAROUND) &&
		(subsys == CLOCK_CONTROL_NRF_SUBSYS_LF)) {
		anomaly_132_workaround();
	}

	get_sub_data(dev, type)->status = CLOCK_CONTROL_STATUS_STARTING;
	nrf_clock_task_trigger(NRF_CLOCK, get_sub_config(dev, type)->start_tsk);

	return 0;
}

static int clock_async_start(struct device *dev, clock_control_subsys_t subsys,
			     struct clock_control_async_data *data)
{
	enum clock_control_nrf_type type = (enum clock_control_nrf_type)subsys;
	struct nrf_clock_control_sub_data *subdata = get_sub_data(dev, type);

	if (subdata->flags & NRF_CLOCK_CONTROL_FLAG_ONOFF_USED) {
		ERR(DEVICE_GET(clock_nrf), "Direct API used when onoff in use");
		return -EINVAL;
	}
	subdata->flags |= NRF_CLOCK_CONTROL_FLAG_DIRECT_USED;

	return async_start(dev, subsys, data);
}

static void blocking_start_callback(struct device *dev,
				    clock_control_subsys_t subsys,
				    void *user_data)
{
	struct k_sem *sem = user_data;

	k_sem_give(sem);
}

static int clock_start(struct device *dev, clock_control_subsys_t subsys)
{
	struct k_sem sem = Z_SEM_INITIALIZER(sem, 0, 1);
	struct clock_control_async_data data = {
		.cb = blocking_start_callback,
		.user_data = &sem
	};
	int err;
	int key;

	key = irq_lock();

	if (get_status(dev, subsys) != CLOCK_CONTROL_STATUS_OFF) {
		err = -EALREADY;
	} else {
		err = clock_async_start(dev, subsys, &data);
	}

	irq_unlock(key);

	if (err < 0) {
		return err;
	}

	k_sem_take(&sem, K_FOREVER);

	return 0;
}

static clock_control_subsys_t get_subsys(struct onoff_service *srv)
{
	struct nrf_clock_control_data *data = DEVICE_GET(clock_nrf)->driver_data;
	size_t offset = (size_t)(srv - data->onoff_service);

	return (clock_control_subsys_t)offset;
}

static void onoff_stop(struct onoff_service *srv,
			onoff_service_notify_fn notify)
{
	(void)clock_stop(DEVICE_GET(clock_nrf), get_subsys(srv));
	notify(srv, 0);
}

static void onoff_started_callback(struct device *dev,
				   clock_control_subsys_t sys,
				   void *user_data)
{
	enum clock_control_nrf_type type = (enum clock_control_nrf_type)sys;
	struct onoff_service *srv = get_onoff_service(DEVICE_GET(clock_nrf),
							type);
	onoff_service_notify_fn notify = user_data;

	notify(srv, 0);
}

static void onoff_start(struct onoff_service *srv,
			onoff_service_notify_fn notify)
{
	enum clock_control_nrf_type type =
					(enum clock_control_nrf_type)get_subsys(srv);
	struct nrf_clock_control_sub_data *subdata =
				get_sub_data(DEVICE_GET(clock_nrf), type);
	struct clock_control_async_data data = {
		.cb = onoff_started_callback,
		.user_data = notify
	};

	if (subdata->flags & NRF_CLOCK_CONTROL_FLAG_DIRECT_USED) {
		ERR(DEVICE_GET(clock_nrf), "Onoff API used when direct in use");
		__ASSERT_NO_MSG(0);
	}
	subdata->flags |= NRF_CLOCK_CONTROL_FLAG_DIRECT_USED;

	(void)async_start(DEVICE_GET(clock_nrf), (clock_control_subsys_t)type,
			&data);
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

static int clk_init(struct device *dev)
{
	int err;

	IRQ_CONNECT(DT_INST_0_NORDIC_NRF_CLOCK_IRQ_0,
		    DT_INST_0_NORDIC_NRF_CLOCK_IRQ_0_PRIORITY,
		    nrf_power_clock_isr, 0, 0);

	irq_enable(DT_INST_0_NORDIC_NRF_CLOCK_IRQ_0);

	nrf_clock_lf_src_set(NRF_CLOCK, CLOCK_CONTROL_NRF_K32SRC);

	if (IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION)) {
		z_nrf_clock_calibration_init(dev);
	}

	clock_irqs_enable();

	for (enum clock_control_nrf_type i = 0;
		i < CLOCK_CONTROL_NRF_TYPE_COUNT; i++) {
		err = onoff_service_init(get_onoff_service(dev, i),
					 onoff_start, onoff_stop, NULL, 0);
		if (err < 0) {
			return err;
		}
	}

	return 0;
}
static const struct clock_control_driver_api clock_control_api = {
	.on = clock_start,
	.off = clock_stop,
	.async_on = clock_async_start,
	.get_status = get_status,
};

static struct nrf_clock_control_data data;

static const struct nrf_clock_control_config config = {
	.subsys = {
		[CLOCK_CONTROL_NRF_TYPE_HFCLK] = {
			.start_tsk = NRF_CLOCK_TASK_HFCLKSTART,
			.started_evt = NRF_CLOCK_EVENT_HFCLKSTARTED,
			.stop_tsk = NRF_CLOCK_TASK_HFCLKSTOP,
			IF_ENABLED(CONFIG_LOG, (.name = "hfclk",))
		},
		[CLOCK_CONTROL_NRF_TYPE_LFCLK] = {
			.start_tsk = NRF_CLOCK_TASK_LFCLKSTART,
			.started_evt = NRF_CLOCK_EVENT_LFCLKSTARTED,
			.stop_tsk = NRF_CLOCK_TASK_LFCLKSTOP,
			IF_ENABLED(CONFIG_LOG, (.name = "lfclk",))
			IF_ENABLED(
				CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION,
				(
				.start_handler = z_nrf_clock_calibration_start,
				.stop_handler = z_nrf_clock_calibration_stop,
				)
			)
		}
	}
};

DEVICE_AND_API_INIT(clock_nrf,
		    DT_INST_0_NORDIC_NRF_CLOCK_LABEL,
		    clk_init, &data, &config, PRE_KERNEL_1,
		    CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &clock_control_api);

static void clkstarted_handle(struct device *dev,
			      enum clock_control_nrf_type type)
{
	struct nrf_clock_control_sub_data *sub_data = get_sub_data(dev, type);
	clock_control_cb_t callback = sub_data->cb;
	void *user_data = sub_data->user_data;

	sub_data->cb = NULL;
	sub_data->status = CLOCK_CONTROL_STATUS_ON;
	DBG(dev, type, "Clock started");

	if (callback) {
		callback(dev, (clock_control_subsys_t)type, user_data);
	}
}

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

void nrf_power_clock_isr(void *arg)
{
	ARG_UNUSED(arg);
	struct device *dev = DEVICE_GET(clock_nrf);

	if (clock_event_check_and_clean(NRF_CLOCK_EVENT_HFCLKSTARTED,
					NRF_CLOCK_INT_HF_STARTED_MASK)) {
		struct nrf_clock_control_sub_data *data =
				get_sub_data(dev, CLOCK_CONTROL_NRF_TYPE_HFCLK);

		/* Check needed due to anomaly 201:
		 * HFCLKSTARTED may be generated twice.
		 */
		if (data->status == CLOCK_CONTROL_STATUS_STARTING) {
			clkstarted_handle(dev, CLOCK_CONTROL_NRF_TYPE_HFCLK);
		}
	}

	if (clock_event_check_and_clean(NRF_CLOCK_EVENT_LFCLKSTARTED,
					NRF_CLOCK_INT_LF_STARTED_MASK)) {
		if (IS_ENABLED(
			CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION)) {
			z_nrf_clock_calibration_lfclk_started(dev);
		}
		clkstarted_handle(dev, CLOCK_CONTROL_NRF_TYPE_LFCLK);
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

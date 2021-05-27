/*
 * Copyright (c) 2021, Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <nrfx_gpiote.h>
#include <drivers/gpio.h>
#include "gpio_utils.h"

#define GPIO(id) DT_NODELABEL(gpio##id)

/* Need to validate that gpiote flag matches zephyr flags. */
BUILD_ASSERT(NRFX_GPIOTE_PULL_UP == GPIO_PULL_UP);
BUILD_ASSERT(NRFX_GPIOTE_PULL_DOWN == GPIO_PULL_DOWN);
BUILD_ASSERT(NRFX_GPIOTE_INPUT == GPIO_INPUT);
BUILD_ASSERT(NRFX_GPIOTE_OUTPUT == GPIO_OUTPUT);
BUILD_ASSERT(NRFX_GPIOTE_INIT_HIGH == GPIO_OUTPUT_INIT_HIGH);
BUILD_ASSERT(NRFX_GPIOTE_INIT_LOW == GPIO_OUTPUT_INIT_LOW);

struct gpio_nrfx_data {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common;
	sys_slist_t callbacks;
	uint32_t evt_inuse_msk;
};

struct gpio_nrfx_cfg {
	/* gpio_driver_config needs to be first */
	struct gpio_driver_config common;
	NRF_GPIO_Type *port;
	uint32_t edge_sense;
	uint8_t port_num;
};

static inline struct gpio_nrfx_data *get_port_data(const struct device *port)
{
	return port->data;
}

static inline const struct gpio_nrfx_cfg *get_port_cfg(const struct device *port)
{
	return port->config;
}

static int get_drive_flags(gpio_flags_t flags, uint32_t *nrfx_flags)
{
	uint32_t drive;

	switch (flags & (GPIO_DS_LOW_MASK | GPIO_DS_HIGH_MASK |
			 GPIO_OPEN_DRAIN)) {
	case GPIO_DS_DFLT_LOW | GPIO_DS_DFLT_HIGH:
		drive = NRFX_GPIOTE_PIN_DRIVE_S0S1;
		break;
	case GPIO_DS_DFLT_LOW | GPIO_DS_ALT_HIGH:
		drive = NRFX_GPIOTE_PIN_DRIVE_S0H1;
		break;
	case GPIO_DS_DFLT_LOW | GPIO_OPEN_DRAIN:
		drive = NRFX_GPIOTE_PIN_DRIVE_S0D1;
		break;

	case GPIO_DS_ALT_LOW | GPIO_DS_DFLT_HIGH:
		drive = NRFX_GPIOTE_PIN_DRIVE_H0S1;
		break;
	case GPIO_DS_ALT_LOW | GPIO_DS_ALT_HIGH:
		drive = NRFX_GPIOTE_PIN_DRIVE_H0H1;
		break;
	case GPIO_DS_ALT_LOW | GPIO_OPEN_DRAIN:
		drive = NRFX_GPIOTE_PIN_DRIVE_H0D1;
		break;

	case GPIO_DS_DFLT_HIGH | GPIO_OPEN_SOURCE:
		drive = NRFX_GPIOTE_PIN_DRIVE_D0S1;
		break;
	case GPIO_DS_ALT_HIGH | GPIO_OPEN_SOURCE:
		drive = NRFX_GPIOTE_PIN_DRIVE_D0H1;
		break;

	default:
		return -EINVAL;
	}

	*nrfx_flags |= drive;

	return 0;
}

static uint32_t get_nrfx_flags(gpio_flags_t flags)
{
	uint32_t nrfx_flags = 0; /* 0 means pin is disconnected */

	if (flags & GPIO_OUTPUT) {
		int err;

		nrfx_flags |= NRFX_GPIOTE_OUTPUT;
		nrfx_flags |= (flags & GPIO_OUTPUT_INIT_LOW) ? NRFX_GPIOTE_INIT_LOW : 0;
		nrfx_flags |= (flags & GPIO_OUTPUT_INIT_HIGH) ? NRFX_GPIOTE_INIT_HIGH : 0;

		err = get_drive_flags(flags, &nrfx_flags);
		if (err) {
			return err;
		}
		/* todo get drive */
	}

	if (flags & GPIO_INPUT) {
		nrfx_flags |= NRFX_GPIOTE_INPUT;
		nrfx_flags |= flags & GPIO_PULL_UP ? NRFX_GPIOTE_PULL_UP : 0;
		nrfx_flags |= flags & GPIO_PULL_DOWN ? NRFX_GPIOTE_PULL_DOWN : 0;
	}

	return nrfx_flags;
}

static int gpio_nrfx_pin_configure(const struct device *port, gpio_pin_t pin,
				   gpio_flags_t flags)
{
	uint32_t nrfx_flags = get_nrfx_flags(flags);

	if ((nrfx_flags & (NRFX_GPIOTE_INPUT | NRFX_GPIOTE_OUTPUT)) ==
	     NRFX_GPIOTE_DISCONNECTED) {
		get_port_data(port)->evt_inuse_msk &= ~BIT(pin);
	}

	nrfx_gpiote_pin_config(NRF_GPIO_PIN_MAP(get_port_cfg(port)->port_num, pin),
			       nrfx_flags);

	return 0;
}

static int gpio_nrfx_port_get_raw(const struct device *port,
				  gpio_port_value_t *value)
{
	NRF_GPIO_Type *reg = get_port_cfg(port)->port;

	*value = nrf_gpio_port_in_read(reg);

	return 0;
}

static int gpio_nrfx_port_set_masked_raw(const struct device *port,
					 uint32_t mask,
					 uint32_t value)
{
	NRF_GPIO_Type *reg = get_port_cfg(port)->port;
	uint32_t value_tmp;

	value_tmp = nrf_gpio_port_out_read(reg) & ~mask;
	nrf_gpio_port_out_write(reg, value_tmp | (mask & value));

	return 0;
}

static int gpio_nrfx_port_set_bits_raw(const struct device *port,
				       uint32_t mask)
{
	NRF_GPIO_Type *reg = get_port_cfg(port)->port;

	nrf_gpio_port_out_set(reg, mask);

	return 0;
}

static int gpio_nrfx_port_clear_bits_raw(const struct device *port,
					 uint32_t mask)
{
	NRF_GPIO_Type *reg = get_port_cfg(port)->port;

	nrf_gpio_port_out_clear(reg, mask);

	return 0;
}

static int gpio_nrfx_port_toggle_bits(const struct device *port,
				      uint32_t mask)
{
	NRF_GPIO_Type *reg = get_port_cfg(port)->port;
	uint32_t value;

	value = nrf_gpio_port_out_read(reg);
	nrf_gpio_port_out_write(reg, value ^ mask);

	return 0;
}

/* Convert gpio interrupt details to nrfx_gpiote interrupt flags. */
static uint32_t get_nrfx_int_flags(enum gpio_int_mode mode,
				   enum gpio_int_trig trig)
{
	uint32_t flags = 0;

	switch (mode) {
	case GPIO_INT_MODE_EDGE:
		flags |= NRFX_GPIOTE_INT_ENABLE | NRFX_GPIOTE_INT_EDGE;
		break;
	case GPIO_INT_MODE_DISABLED:
		return NRFX_GPIOTE_INT_DISABLE;
	default:
		flags |= NRFX_GPIOTE_INT_ENABLE;
		break;
	}

	switch (trig) {
	case GPIO_INT_TRIG_LOW:
		flags |= NRFX_GPIOTE_INT_LOW | NRFX_GPIOTE_INT_CFG_PRESENT;
		break;
	case GPIO_INT_TRIG_HIGH:
		flags |= NRFX_GPIOTE_INT_HIGH | NRFX_GPIOTE_INT_CFG_PRESENT;
		break;
	default:
		flags |= NRFX_GPIOTE_INT_LOW |
			 NRFX_GPIOTE_INT_HIGH |
			 NRFX_GPIOTE_INT_CFG_PRESENT;
		break;
	}

	return flags;
}

static int gpio_nrfx_pin_interrupt_configure(const struct device *port,
				       	     gpio_pin_t pin,
					     enum gpio_int_mode mode,
					     enum gpio_int_trig trig)
{
	uint32_t abs_pin = NRF_GPIO_PIN_MAP(get_port_cfg(port)->port_num, pin);
	struct gpio_nrfx_data *data = get_port_data(port);
	nrfx_err_t err;
	uint32_t flags = get_nrfx_int_flags(mode, trig);

	/* If edge mode is to be used and pin is not configured to use sense for
	 * edge use IN event.
	 */
	if ((nrf_gpio_pin_dir_get(abs_pin) == NRF_GPIO_PIN_DIR_INPUT) &&
	    (mode == GPIO_INT_MODE_EDGE) &&
	    !(BIT(pin) & get_port_cfg(port)->edge_sense)) {
		if (data->evt_inuse_msk & BIT(pin)) {
			flags |= NRFX_GPIOTE_INT_USE_IN_EVT;
		} else {
			/* Allocate if not allocated previously */
			uint8_t ch;

			err = nrfx_gpiote_channel_alloc(&ch);
			if (err != NRFX_SUCCESS) {
				return -ENOMEM;
			}

			flags |= NRFX_GPIOTE_INT_CHAN(ch);
			data->evt_inuse_msk |= BIT(pin);
		}
	}

	err = nrfx_gpiote_pin_int_config(abs_pin, flags, NULL, NULL);
	if (err != NRFX_SUCCESS) {
		return -EIO;
	}

	return 0;
}

static int gpio_nrfx_manage_callback(const struct device *port,
				     struct gpio_callback *callback,
				     bool set)
{
	return gpio_manage_callback(&get_port_data(port)->callbacks,
				     callback, set);
}

static const struct device *pin2dev(nrfx_gpiote_pin_t abs_pin)
{
	return abs_pin >= 32 ?
		COND_CODE_1(DT_NODE_EXISTS(GPIO(1)), (DEVICE_DT_GET(GPIO(1))), (NULL)) :
		DEVICE_DT_GET(GPIO(0));
}

static uint8_t abs2pin(nrfx_gpiote_pin_t abs_pin)
{
	return abs_pin & 0x1F;
}

static void nrfx_gpio_handler(nrfx_gpiote_pin_t abs_pin,
			      nrf_gpiote_polarity_t action,
			      void *context)
{
	const struct device *port = pin2dev(abs_pin);
	uint32_t pin_mask = BIT(abs2pin(abs_pin));
	struct gpio_nrfx_data *data = get_port_data(port);
	sys_slist_t *list = &data->callbacks;

	gpio_fire_callbacks(list, port, pin_mask);
}

#define GPIOTE_NODE DT_INST(0, nordic_nrf_gpiote)

static int gpio_nrfx_init(const struct device *port)
{
	static bool gpio_initialized;
	nrfx_err_t err;

	if (gpio_initialized) {
		return 0;
	}

	err = nrfx_gpiote_init(0xFF);
	if (err != NRFX_SUCCESS) {
		return -EIO;
	}

	gpio_initialized = true;
	nrfx_gpiote_global_callback_set(nrfx_gpio_handler, NULL);
	IRQ_CONNECT(DT_IRQN(GPIOTE_NODE), DT_IRQ(GPIOTE_NODE, priority),
		    nrfx_gpiote_irq_handler, NULL, 0);

	irq_enable(DT_IRQN(GPIOTE_NODE));

	return 0;
}

static const struct gpio_driver_api gpio_nrfx_drv_api_funcs = {
	.pin_configure = gpio_nrfx_pin_configure,
	.port_get_raw = gpio_nrfx_port_get_raw,
	.port_set_masked_raw = gpio_nrfx_port_set_masked_raw,
	.port_set_bits_raw = gpio_nrfx_port_set_bits_raw,
	.port_clear_bits_raw = gpio_nrfx_port_clear_bits_raw,
	.port_toggle_bits = gpio_nrfx_port_toggle_bits,
	.pin_interrupt_configure = gpio_nrfx_pin_interrupt_configure,
	.manage_callback = gpio_nrfx_manage_callback,
};

/* Device instantiation is done with node labels because 'port_num' is
 * the peripheral number by SoC numbering. We therefore cannot use
 * DT_INST APIs here without wider changes.
 */

#define GPIO_HAS_PROP(idx, prop) DT_NODE_HAS_PROP(GPIO(idx), prop)
#define GPIO_PROP(idx, prop) DT_PROP(GPIO(idx), prop)

#define GPIO_NRF_DEVICE(id)							\
	static const struct gpio_nrfx_cfg gpio_nrfx_p##id##_cfg = {		\
		.common = {							\
			.port_pin_mask =					\
			GPIO_PORT_PIN_MASK_FROM_DT_NODE(GPIO(id)),		\
		},								\
		.port = NRF_P##id,						\
		.port_num = id,							\
		.edge_sense = COND_CODE_1(GPIO_HAS_PROP(id, sense_edge_mask),	\
					  (GPIO_PROP(id, sense_edge_mask)), (0))\
	};									\
										\
	static struct gpio_nrfx_data gpio_nrfx_p##id##_data;			\
										\
	DEVICE_DT_DEFINE(GPIO(id), gpio_nrfx_init,				\
			 NULL,							\
			 &gpio_nrfx_p##id##_data,				\
			 &gpio_nrfx_p##id##_cfg,				\
			 POST_KERNEL,						\
			 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,			\
			 &gpio_nrfx_drv_api_funcs)

#ifdef CONFIG_GPIO_NRF_P0
GPIO_NRF_DEVICE(0);
#endif

#ifdef CONFIG_GPIO_NRF_P1
GPIO_NRF_DEVICE(1);
#endif

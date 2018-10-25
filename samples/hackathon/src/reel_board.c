/*
 * Copyright (c) 2018 Phytec Messtechnik GmbH
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <device.h>
#include <gpio.h>
#include <display/cfb.h>
#include <misc/printk.h>
#include <flash.h>

#include <string.h>


#include "board.h"

#define EDGE (GPIO_INT_EDGE | GPIO_INT_DOUBLE_EDGE)

#ifdef SW0_GPIO_FLAGS
#define PULL_UP SW0_GPIO_FLAGS
#else
#define PULL_UP 0
#endif

#define LINE_MAX 12

static struct device *epd_dev;
static bool pressed;
static struct device *gpio;
static struct k_delayed_work epd_work;

static struct {
	struct device *dev;
	const char *name;
	u32_t pin;
} leds[] = {
	{ .name = LED0_GPIO_CONTROLLER, .pin = LED0_GPIO_PIN, },
	{ .name = LED1_GPIO_CONTROLLER, .pin = LED1_GPIO_PIN, },
	{ .name = LED2_GPIO_CONTROLLER, .pin = LED2_GPIO_PIN, },
	{ .name = LED3_GPIO_CONTROLLER, .pin = LED3_GPIO_PIN, },
};

struct k_delayed_work led_timer;

static size_t print_line(int row, const char *text, size_t len, bool center)
{
	u8_t font_height, font_width;
	u8_t line[LINE_MAX + 1];
	int pad;

	len = min(len, LINE_MAX);
	memcpy(line, text, len);
	line[len] = '\0';

	if (center) {
		pad = (LINE_MAX - len) / 2;
	} else {
		pad = 0;
	}

	cfb_get_font_size(epd_dev, 0, &font_width, &font_height);

	if (cfb_print(epd_dev, line, font_width * pad, font_height * row)) {
		printk("Failed to print a string\n");
	}

	return len;
}

static size_t get_len(const char *text)
{
	const char *space = NULL;
	size_t i;

	for (i = 0; i <= LINE_MAX; i++) {
		switch (text[i]) {
		case '\n':
		case '\0':
			return i;
		case ' ':
			space = &text[i];
			break;
		default:
			continue;
		}
	}

	/* If we got more characters than fits a line, and a space was
	 * encountered, fall back to the last space.
	 */
	if (space) {
		return space - text;
	}

	return LINE_MAX;
}

void board_blink_leds(void)
{
	k_delayed_work_submit(&led_timer, K_MSEC(100));
}

void board_show_text(const char *text, bool center, s32_t duration)
{
	int i;

	cfb_framebuffer_set_font(epd_dev, 0);
	cfb_framebuffer_clear(epd_dev, false);

	for (i = 0; i < 3; i++) {
		size_t len;

		while (*text == ' ' || *text == '\n') {
			text++;
		}

		len = get_len(text);
		if (!len) {
			break;
		}

		text += print_line(i, text, len, center);
		if (!*text) {
			break;
		}
	}

	cfb_framebuffer_finalize(epd_dev);

	if (duration != K_FOREVER) {
		k_delayed_work_submit(&epd_work, duration);
	}
}

static void epd_update(struct k_work *work)
{
	board_show_text("kuba", true, K_FOREVER);
}

static bool button_is_pressed(void)
{
	u32_t val;

	gpio_pin_read(gpio, SW0_GPIO_PIN, &val);

	return !val;
}

static void button_interrupt(struct device *dev, struct gpio_callback *cb,
			     u32_t pins)
{
	if (button_is_pressed() == pressed) {
		return;
	}

	pressed = !pressed;
	printk("Button %s\n", pressed ? "pressed" : "released");

	/* We only care about button release for now */
	if (pressed) {
		return;
	}
}

static int configure_button(void)
{
	static struct gpio_callback button_cb;

	gpio = device_get_binding(GPIO_KEYS_BUTTON_0_GPIO_CONTROLLER);
	if (!gpio) {
		return -ENODEV;
	}

	gpio_pin_configure(gpio, SW0_GPIO_PIN,
			   (GPIO_DIR_IN | GPIO_INT |  PULL_UP | EDGE));

	gpio_init_callback(&button_cb, button_interrupt, BIT(SW0_GPIO_PIN));
	gpio_add_callback(gpio, &button_cb);

	gpio_pin_enable_callback(gpio, SW0_GPIO_PIN);

	return 0;
}

static void led_timeout(struct k_work *work)
{
	static int led_cntr;
	int i;

	/* Disable all LEDs */
	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		gpio_pin_write(leds[i].dev, leds[i].pin, 1);
	}

	/* Stop after 5 iterations */
	if (led_cntr > (ARRAY_SIZE(leds) * 5)) {
		led_cntr = 0;
		return;
	}

	/* Select and enable current LED */
	i = led_cntr++ % ARRAY_SIZE(leds);
	gpio_pin_write(leds[i].dev, leds[i].pin, 0);

	k_delayed_work_submit(&led_timer, K_MSEC(100));
}

static int configure_leds(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		leds[i].dev = device_get_binding(leds[i].name);
		if (!leds[i].dev) {
			printk("Failed to get %s device\n", leds[i].name);
			return -ENODEV;
		}

		gpio_pin_configure(leds[i].dev, leds[i].pin, GPIO_DIR_OUT);
		gpio_pin_write(leds[i].dev, leds[i].pin, 1);

	}

	k_delayed_work_init(&led_timer, led_timeout);
	return 0;
}

static int erase_storage(void)
{
	return 0;
}

void board_refresh_display(void)
{
	k_delayed_work_submit(&epd_work, K_NO_WAIT);
}

int board_init(void)
{
	epd_dev = device_get_binding(CONFIG_SSD1673_DEV_NAME);
	if (epd_dev == NULL) {
		printk("SSD1673 device not found\n");
		return -ENODEV;
	}

	if (cfb_framebuffer_init(epd_dev)) {
		printk("Framebuffer initialization failed\n");
		return -EIO;
	}

	cfb_framebuffer_clear(epd_dev, true);

	if (configure_button()) {
		printk("Failed to configure button\n");
		return -EIO;
	}

	if (configure_leds()) {
		printk("LED init failed\n");
		return -EIO;
	}

	k_delayed_work_init(&epd_work, epd_update);

	pressed = button_is_pressed();
	if (pressed) {
		printk("Erasing storage\n");
		board_show_text("Resetting Device", false, K_SECONDS(4));
		erase_storage();
	}

	return 0;
}

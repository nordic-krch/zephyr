/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/uart.h>
#include <device.h>
#include <pm/device.h>
#include <ztest.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(test);

#if defined(CONFIG_BOARD_NRF52840DK_NRF52840)
#define LABEL uart0
#endif

#define UART_DEVICE_NAME DT_LABEL(DT_NODELABEL(LABEL))
#define HAS_RX DT_NODE_HAS_PROP(DT_NODELABEL(LABEL), rx_pin)

static void polling_verify(const struct device *dev, bool is_async, bool active)
{
	char c;
	char outs[] = "abc";
	int err;

	if (!HAS_RX || is_async) {
		/* If no RX pin just run few poll outs to check that it does
		 * not hang.
		 */
		for (int i = 0; i < ARRAY_SIZE(outs); i++) {
			uart_poll_out(dev, outs[i]);
		}

		return;
	}

	err = uart_poll_in(dev, &c);
	zassert_equal(err, -1, NULL);

	for (int i = 0; i < ARRAY_SIZE(outs); i++) {
		uart_poll_out(dev, outs[i]);
		k_busy_wait(1000);

		if (active) {
			err = uart_poll_in(dev, &c);
			zassert_equal(err, 0, "Unexpected err: %d", err);
			zassert_equal(c, outs[i], NULL);
		}

		err = uart_poll_in(dev, &c);
		zassert_equal(err, -1, NULL);
	}
}

static void async_callback(const struct device *dev, struct uart_event *evt, void *ctx)
{
	bool *done = ctx;

	switch (evt->type) {
	case UART_TX_DONE:
		*done = true;
		break;
	default:
		break;
	}
}

static void int_driven_callback(const struct device *dev, void *user_data)
{
	bool *test_ok = (bool *)user_data;
	static const uint8_t test_byte = 0xAA;

	while (uart_irq_is_pending(dev)) {
		int len;

		if (uart_irq_rx_ready(dev)) {
			uint8_t x;

			len = uart_fifo_read(dev, &x, 1);
			if (len == 1 && x == test_byte) {
				*test_ok = true;
			} else {
				LOG_ERR("Received data: %02x (%c) (exp:%02x)",
					x, (char)x, test_byte);
			}

			uart_irq_rx_disable(dev);
		}

		if (uart_irq_tx_ready(dev)) {
			len = uart_fifo_fill(dev, &test_byte, 1);
			if (len != 1) {
				LOG_ERR("Failed to fill fifo");
			}

			uart_irq_tx_disable(dev);
		}
	}
}

/* Verify that interrupt driven functionality is working.
 * Enable rx and tx interrupt. Send 1 byte and verify that same byte is received.
 */
static void int_driven_verify(const struct device *dev, bool active)
{
	/* Check if api is supported. */
	if (uart_irq_is_pending(dev) < 0) {
		return;
	}

	/* int driven api shall not be used when uart is suspended. */
	if (!active) {
		return;
	}

	bool test_ok = false;

	uart_irq_callback_user_data_set(dev, int_driven_callback, &test_ok);

	LOG_INF("Starting int driven verification");
	uart_irq_rx_enable(dev);
	uart_irq_tx_enable(dev);

	k_msleep(10);
	zassert_true(test_ok, NULL);
}


static bool async_verify(const struct device *dev, bool active)
{
	char txbuf[] = "test";
	uint8_t rxbuf[32];
	volatile bool tx_done = false;
	int err;

	err = uart_callback_set(dev, async_callback, (void *)&tx_done);
	if (err == -ENOTSUP) {
		return false;
	}

	if (!active) {
		return true;
	}

	zassert_equal(err, 0, "Unexpected err: %d", err);

	if (HAS_RX) {
		err = uart_rx_enable(dev, rxbuf, sizeof(rxbuf), 1 * USEC_PER_MSEC);
		zassert_equal(err, 0, "Unexpected err: %d", err);
	}

	err = uart_tx(dev, txbuf, sizeof(txbuf), 10 * USEC_PER_MSEC);
	zassert_equal(err, 0, "Unexpected err: %d", err);

	k_busy_wait(10000);

	if (HAS_RX) {
		err = uart_rx_disable(dev);
		zassert_equal(err, 0, "Unexpected err: %d", err);

		k_busy_wait(10000);

		err = memcmp(txbuf, rxbuf, sizeof(txbuf));
		zassert_equal(err, 0, "Unexpected err: %d", err);
	}

	zassert_true(tx_done, NULL);

	return true;
}

static void communication_verify(const struct device *dev, bool active)
{
	int_driven_verify(dev, active);

	bool is_async = async_verify(dev, active);

	polling_verify(dev, is_async, active);
}

#define state_verify(dev, exp_state) do {\
	enum pm_device_state power_state; \
	int err = pm_device_state_get(dev, &power_state); \
	zassert_equal(err, 0, "Unexpected err: %d", err); \
	zassert_equal(power_state, exp_state, NULL); \
} while (0)

static void action_run(const struct device *dev, enum pm_device_action action,
		      int exp_err)
{
	int err;
	enum pm_device_state prev_state, exp_state;

	err = pm_device_state_get(dev, &prev_state);
	zassert_equal(err, 0, "Unexpected err: %d", err);

	err = pm_device_action_run(dev, action);
	zassert_equal(err, exp_err, "Unexpected err: %d", err);

	if (err == 0) {
		switch (action) {
		case PM_DEVICE_ACTION_SUSPEND:
			exp_state = PM_DEVICE_STATE_SUSPENDED;
			break;
		case PM_DEVICE_ACTION_RESUME:
			exp_state = PM_DEVICE_STATE_ACTIVE;
			break;
		default:
			exp_state = prev_state;
			break;
		}
	} else {
		exp_state = prev_state;
	}

	state_verify(dev, exp_state);
}

static void test_uart_pm_in_idle(void)
{
	const struct device *dev;

	dev = device_get_binding(UART_DEVICE_NAME);
	zassert_true(dev != NULL, NULL);

	state_verify(dev, PM_DEVICE_STATE_ACTIVE);
	communication_verify(dev, true);

	action_run(dev, PM_DEVICE_ACTION_SUSPEND, 0);
	communication_verify(dev, false);

	action_run(dev, PM_DEVICE_ACTION_RESUME, 0);
	communication_verify(dev, true);

	action_run(dev, PM_DEVICE_ACTION_SUSPEND, 0);
	communication_verify(dev, false);

	action_run(dev, PM_DEVICE_ACTION_RESUME, 0);
	communication_verify(dev, true);
}

static void test_uart_pm_poll_tx(void)
{
	const struct device *dev;

	dev = device_get_binding(UART_DEVICE_NAME);
	zassert_true(dev != NULL, NULL);

	communication_verify(dev, true);

	uart_poll_out(dev, 'a');
	action_run(dev, PM_DEVICE_ACTION_SUSPEND, 0);

	communication_verify(dev, false);

	action_run(dev, PM_DEVICE_ACTION_RESUME, 0);

	communication_verify(dev, true);

	/* Now same thing but with callback */
	uart_poll_out(dev, 'a');
	action_run(dev, PM_DEVICE_ACTION_SUSPEND, 0);

	communication_verify(dev, false);

	action_run(dev, PM_DEVICE_ACTION_RESUME, 0);

	communication_verify(dev, true);
}

static void timeout(struct k_timer *timer)
{
	const struct device *uart = k_timer_user_data_get(timer);

	action_run(uart, PM_DEVICE_ACTION_SUSPEND, 0);
}

static K_TIMER_DEFINE(pm_timer, timeout, NULL);

/* Test going into low power state after interrupting poll out. Use various
 * delays to test interruption at multiple places.
 */
static void test_uart_pm_poll_tx_interrupted(void)
{
	const struct device *dev;
	char str[] = "test";

	dev = device_get_binding(UART_DEVICE_NAME);
	zassert_true(dev != NULL, NULL);

	k_timer_user_data_set(&pm_timer, (void *)dev);

	for (int i = 1; i < 100; i++) {
		k_timer_start(&pm_timer, K_USEC(i * 10), K_NO_WAIT);

		for (int j = 0; j < sizeof(str); j++) {
			uart_poll_out(dev, str[j]);
		}

		k_timer_status_sync(&pm_timer);

		action_run(dev, PM_DEVICE_ACTION_RESUME, 0);

		communication_verify(dev, true);
	}
}

void test_main(void)
{
	if (!HAS_RX) {
		PRINT("No RX pin\n");
	}

	ztest_test_suite(uart_pm,
			 ztest_unit_test(test_uart_pm_in_idle),
			 ztest_unit_test(test_uart_pm_poll_tx),
			 ztest_unit_test(test_uart_pm_poll_tx_interrupted)
			);
	ztest_run_test_suite(uart_pm);
}

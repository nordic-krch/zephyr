/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ztest.h>
#include <zephyr/ipc/ipc_service.h>
#if defined(CONFIG_SOC_NRF5340_CPUAPP)
#include <nrf53_cpunet_mgmt.h>
#endif

#include <api_test.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ipc_service_api, LOG_LEVEL_INF);

struct rx_entry {
	uint8_t data[sizeof(struct api_test_msg)];
	size_t len;
};

K_MSGQ_DEFINE(rx_msgq, sizeof(struct rx_entry), 8, 4);

static const struct device *ipc0_instance = DEVICE_DT_GET(DT_ALIAS(dut_ipc));
static struct ipc_ept ep;
static struct ipc_ept_cfg ep_cfg;
static volatile bool bound_flag;
K_SEM_DEFINE(bound_sem, 0, 1);
K_SEM_DEFINE(unbound_sem, 0, 1);

static void (*recv_override)(const void *data, size_t len, void *priv);

static void ep_bound(void *priv)
{
	bound_flag = true;
	k_sem_give(&bound_sem);
	LOG_INF("Endpoint bound");
}

static void ep_unbound(void *priv)
{
	bound_flag = false;
	k_sem_reset(&bound_sem);
	k_sem_give(&unbound_sem);
	LOG_INF("Endpoint unbound");
}

static void ep_error(const char *message, void *priv)
{
	LOG_ERR("Endpoint error: %s", message);
}

static void ep_received(const void *data, size_t len, void *priv)
{
	if (recv_override != NULL) {
		recv_override(data, len, priv);
		return;
	}

	struct rx_entry entry;
	int ret;

	if (len > sizeof(entry.data)) {
		zassert_unreachable("Unexpected RX length: %u", len);
	}

	memcpy(entry.data, data, len);
	entry.len = len;

	ret = k_msgq_put(&rx_msgq, &entry, K_NO_WAIT);
	zassert_ok(ret, "RX queue full");
}

static int wait_for_msg(uint32_t cmd, struct api_test_msg *out, k_timeout_t timeout)
{
	struct rx_entry entry;
	int ret;

	while (1) {
		ret = k_msgq_get(&rx_msgq, &entry, timeout);
		if (ret != 0) {
			return ret;
		}

		struct api_test_msg *msg = (struct api_test_msg *)entry.data;

		if (msg->cmd == cmd) {
			memcpy(out, msg, entry.len);
			return 0;
		}
	}
}

static bool nocopy_supported(void)
{
	void *buf;
	uint32_t size = 0;
	int ret;

	ret = ipc_service_get_tx_buffer_size(&ep);
	if (ret == -ENOTSUP || ret == -EIO) {
		return false;
	}

	ret = ipc_service_get_tx_buffer(&ep, &buf, &size, K_NO_WAIT);
	if (ret == 0) {
		ipc_service_drop_tx_buffer(&ep, buf);
		return true;
	}

	return false;
}

static bool nocopy_with_timeout_supported(void)
{
	void *buf;
	uint32_t size = 0;
	int ret;

	if (!nocopy_supported()) {
		return false;
	}

	ret = ipc_service_get_tx_buffer(&ep, &buf, &size, K_MSEC(100));
	if (ret == 0) {
		ipc_service_drop_tx_buffer(&ep, buf);
		return true;
	}

	return false;
}

static bool hold_rx_supported(void)
{
	return IS_ENABLED(CONFIG_IPC_SERVICE_BACKEND_ICBMSG) ||
	       IS_ENABLED(CONFIG_IPC_SERVICE_BACKEND_RPMSG);
}

static void wait_for_bound(void)
{
	int ret;

	do {
		ret = k_sem_take(&bound_sem, K_MSEC(1000));
		zassert_ok(ret, "Timed out waiting for endpoint bound");
	} while (!bound_flag);
}

static void wait_for_remote_ready(void)
{
	int ret;
	struct api_test_msg cmd = { .cmd = API_TEST_CMD_PING };
	struct api_test_msg rsp;

	ret = ipc_service_send(&ep, &cmd, sizeof(cmd));
	zassert_equal(ret, sizeof(cmd), "Initial PING failed: %d", ret);

	ret = wait_for_msg(API_TEST_CMD_PONG, &rsp, K_MSEC(5000));
	zassert_ok(ret, "Remote not ready");
}

static void register_endpoint(void)
{
	int ret;

	ep_cfg.name = "ep0";
	ep_cfg.priv = &ep;
	ep_cfg.cb.bound = ep_bound;
	ep_cfg.cb.unbound = ep_unbound;
	ep_cfg.cb.received = ep_received;
	ep_cfg.cb.error = ep_error;

	ret = ipc_service_register_endpoint(ipc0_instance, &ep, &ep_cfg);
	zassert_ok(ret, "ipc_service_register_endpoint failed: %d", ret);

	wait_for_bound();
}

static void *suite_setup(void)
{
	int ret;

#if defined(CONFIG_SOC_NRF5340_CPUAPP)
	nrf53_cpunet_enable(true);
#endif

	ret = ipc_service_open_instance(ipc0_instance);
	zassert_true(ret == 0 || ret == -EALREADY,
		     "ipc_service_open_instance failed: %d", ret);

	ret = ipc_service_open_instance(ipc0_instance);
	zassert_true(ret == 0 || ret == -EALREADY,
		     "Second open should return 0 or -EALREADY, got %d", ret);

	k_msleep(100);
	register_endpoint();
	wait_for_remote_ready();

	return NULL;
}

static void suite_before(void *fixture)
{
	recv_override = NULL;
	k_msgq_purge(&rx_msgq);

	if (!bound_flag) {
		k_sem_reset(&bound_sem);
		register_endpoint();
	}
}

ZTEST(ipc_service_api, test_send)
{
	int ret;
	struct api_test_msg cmd = { .cmd = API_TEST_CMD_PING };
	struct api_test_msg rsp;

	ret = ipc_service_send(&ep, &cmd, sizeof(cmd));
	zassert_equal(ret, sizeof(cmd), "ipc_service_send failed: %d", ret);

	ret = wait_for_msg(API_TEST_CMD_PONG, &rsp, K_MSEC(1000));
	zassert_ok(ret, "No PONG response received");
}

ZTEST(ipc_service_api, test_echo)
{
	int ret;
	struct api_test_msg cmd = {
		.cmd = API_TEST_CMD_ECHO,
		.data = { 't', 'e', 's', 't' },
	};
	struct api_test_msg rsp;

	ret = ipc_service_send(&ep, &cmd, sizeof(cmd));
	zassert_equal(ret, sizeof(cmd), "ipc_service_send failed: %d", ret);

	ret = wait_for_msg(API_TEST_CMD_ECHO_RSP, &rsp, K_MSEC(1000));
	zassert_ok(ret, "No ECHO_RSP response received");
	zassert_mem_equal(rsp.data, cmd.data, sizeof(cmd.data),
			  "ECHO response data mismatch");
}

ZTEST(ipc_service_api, test_deregister_endpoint)
{
	int ret;
	struct api_test_msg cmd = { .cmd = API_TEST_CMD_PING };

	if (!IS_ENABLED(CONFIG_IPC_SERVICE_API_TEST_DEREGISTER)) {
		ztest_test_skip();
	}

	ret = ipc_service_deregister_endpoint(&ep);
	if (ret == -ENOTSUP) {
		ztest_test_skip();
	}
	zassert_ok(ret, "ipc_service_deregister_endpoint failed: %d", ret);
	bound_flag = false;

	ret = ipc_service_send(&ep, &cmd, sizeof(cmd));
	zassert_equal(ret, -ENOENT,
		      "Send after deregister should return -ENOENT, got %d", ret);
}

ZTEST(ipc_service_api, test_remote_deregister_endpoint)
{
	int ret;
	struct api_test_msg cmd = { .cmd = API_TEST_CMD_DEREGISTER };
	struct api_test_msg ping = { .cmd = API_TEST_CMD_PING };
	struct api_test_msg rsp;

	if (!IS_ENABLED(CONFIG_IPC_SERVICE_API_TEST_DEREGISTER)) {
		ztest_test_skip();
	}

	k_sem_reset(&unbound_sem);
	k_msgq_purge(&rx_msgq);

	ret = ipc_service_send(&ep, &cmd, sizeof(cmd));
	zassert_equal(ret, sizeof(cmd), "DEREGISTER request send failed: %d", ret);

	ret = k_sem_take(&unbound_sem, K_MSEC(100));
	zassert_ok(ret, "Timed out waiting for unbound after remote deregister");
	zassert_false(bound_flag, "Endpoint should be unbound");

	k_sem_reset(&bound_sem);
	register_endpoint();

	ret = ipc_service_send(&ep, &ping, sizeof(ping));
	zassert_equal(ret, sizeof(ping), "PING after re-register failed: %d", ret);

	ret = wait_for_msg(API_TEST_CMD_PONG, &rsp, K_MSEC(5000));
	zassert_ok(ret, "No PONG after remote-initiated deregister and re-bind");
}

ZTEST(ipc_service_api, test_close_backend)
{
	int ret;
	struct api_test_msg cmd = { .cmd = API_TEST_CMD_CLOSE_AFTER_UNBOUND };

	if (!IS_ENABLED(CONFIG_IPC_SERVICE_API_TEST_CLOSE_BACKEND)) {
		ztest_test_skip();
	}

	ret = ipc_service_send(&ep, &cmd, sizeof(cmd));
	zassert_equal(ret, sizeof(cmd),
		      "CLOSE_AFTER_UNBOUND send failed: %d", ret);

	ret = ipc_service_deregister_endpoint(&ep);
	zassert_ok(ret, "deregister before close failed: %d", ret);
	bound_flag = false;

	/* Wait for the remote to handle unbound, close, and reopen its instance. */
	k_msleep(1);

	ret = ipc_service_close_instance(ipc0_instance);
	if (ret == -ENOTSUP) {
		ztest_test_skip();
	}
	zassert_ok(ret, "ipc_service_close_instance failed: %d", ret);

	k_msleep(10);

	cmd.cmd = API_TEST_CMD_PING;
	ret = ipc_service_send(&ep, &cmd, sizeof(cmd));
	zassert_true(ret < 0, "Send after close should fail, got %d", ret);

	ret = ipc_service_open_instance(ipc0_instance);
	zassert_ok(ret);

	k_msgq_purge(&rx_msgq);
	k_sem_reset(&bound_sem);
	register_endpoint();
	wait_for_remote_ready();
}

ZTEST(ipc_service_api, test_get_tx_buffer_size)
{
	int ret;

	ret = ipc_service_get_tx_buffer_size(&ep);
	if (ret == -ENOTSUP || ret == -EIO) {
		ztest_test_skip();
	}

	zassert_true(ret > 0, "Unexpected TX buffer size: %d", ret);
}

static void test_nocopy_send(bool with_timeout)
{
	int ret;
	void *tx_buf;
	uint32_t tx_size = 0;
	struct api_test_msg rsp;
	struct api_test_msg *msg;
	uint8_t expected[API_TEST_MSG_DATA_SIZE];

	if (with_timeout) {	
		if (!nocopy_with_timeout_supported()) {
			ztest_test_skip();
		}
		ret = ipc_service_get_tx_buffer(&ep, &tx_buf, &tx_size, K_MSEC(100));
	} else {
		if (!nocopy_supported()) {
			ztest_test_skip();
		}
		ret = ipc_service_get_tx_buffer(&ep, &tx_buf, &tx_size, K_NO_WAIT);
	}
	zassert_ok(ret, "ipc_service_get_tx_buffer failed: %d", ret);
	zassert_true(tx_size >= sizeof(struct api_test_msg),
		     "TX buffer too small: %u", tx_size);

	msg = tx_buf;
	msg->cmd = API_TEST_CMD_ECHO;
	memset(msg->data, 0xab, sizeof(msg->data));
	memcpy(expected, msg->data, sizeof(expected));

	ret = ipc_service_send_nocopy(&ep, tx_buf, sizeof(*msg));
	zassert_equal(ret, sizeof(*msg), "ipc_service_send_nocopy failed: %d", ret);

	ret = wait_for_msg(API_TEST_CMD_ECHO_RSP, &rsp, K_MSEC(1000));
	zassert_ok(ret, "No ECHO_RSP for nocopy send");
	zassert_mem_equal(rsp.data, expected, sizeof(expected),
			  "Nocopy ECHO response mismatch");
}

ZTEST(ipc_service_api, test_nocopy_send_with_timeout)
{
	test_nocopy_send(true);
}

ZTEST(ipc_service_api, test_nocopy_send_without_timeout)
{
	test_nocopy_send(false);
}

ZTEST(ipc_service_api, test_drop_tx_buffer)
{
	int ret;
	void *tx_buf;
	uint32_t tx_size = 32;

	if (!nocopy_supported()) {
		ztest_test_skip();
	}

	ret = ipc_service_get_tx_buffer(&ep, &tx_buf, &tx_size, K_MSEC(1000));
	if (ret == -ENOTSUP || ret == -EOPNOTSUPP) {
		ztest_test_skip();
	}
	zassert_ok(ret, "ipc_service_get_tx_buffer failed: %d", ret);

	ret = ipc_service_drop_tx_buffer(&ep, tx_buf);
	zassert_ok(ret, "ipc_service_drop_tx_buffer failed: %d", ret);

	ret = ipc_service_drop_tx_buffer(&ep, tx_buf);
	zassert_equal(ret, -EALREADY,
		      "Second drop should return -EALREADY, got %d", ret);
}

#define HELD_TX_BUF_MAX 32

static void *held_tx_bufs[HELD_TX_BUF_MAX];
static void *tx_buf_to_release;

static void release_tx_buf_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	int ret;

	ret = ipc_service_drop_tx_buffer(&ep, tx_buf_to_release);
	zassert_ok(ret, "release timer drop failed: %d", ret);
}

static K_TIMER_DEFINE(release_tx_buf_timer, release_tx_buf_timer_handler, NULL);

static bool tx_buffer_timeout_supported(void)
{
	void *buf;
	uint32_t size = sizeof(struct api_test_msg);
	int ret;

	if (!nocopy_supported()) {
		return false;
	}

	ret = ipc_service_get_tx_buffer(&ep, &buf, &size, K_MSEC(1));
	if (ret == -ENOTSUP || ret == -EOPNOTSUPP) {
		return false;
	}
	if (ret == 0) {
		ipc_service_drop_tx_buffer(&ep, buf);
	}

	return true;
}

ZTEST(ipc_service_api, test_get_tx_buffer_timeout)
{
	int ret;
	size_t held_count = 0;
	void *tx_buf;
	uint32_t tx_size = sizeof(struct api_test_msg);
	struct api_test_msg *msg;
	struct api_test_msg rsp;
	uint8_t expected[API_TEST_MSG_DATA_SIZE];

	if (!tx_buffer_timeout_supported()) {
		ztest_test_skip();
	}

	while (held_count < ARRAY_SIZE(held_tx_bufs)) {
		void *buf;
		uint32_t size = sizeof(struct api_test_msg);

		ret = ipc_service_get_tx_buffer(&ep, &buf, &size, K_NO_WAIT);
		if (ret != 0) {
			zassert_true(ret == -ENOMEM || ret == -ENOBUFS,
				     "Expected no TX buffers, got %d", ret);
			break;
		}

		held_tx_bufs[held_count++] = buf;
	}

	zassert_true(held_count > 0, "Failed to allocate any TX buffers");

	tx_buf_to_release = held_tx_bufs[0];
	k_timer_start(&release_tx_buf_timer, K_MSEC(1), K_NO_WAIT);

	ret = ipc_service_get_tx_buffer(&ep, &tx_buf, &tx_size, K_MSEC(100));
	zassert_ok(ret, "Timed ipc_service_get_tx_buffer failed: %d", ret);
	zassert_true(tx_size >= sizeof(struct api_test_msg),
		     "TX buffer too small: %u", tx_size);

	msg = tx_buf;
	msg->cmd = API_TEST_CMD_ECHO;
	memset(msg->data, 0xcd, sizeof(msg->data));
	memcpy(expected, msg->data, sizeof(expected));

	ret = ipc_service_send_nocopy(&ep, tx_buf, sizeof(*msg));
	zassert_equal(ret, sizeof(*msg), "ipc_service_send_nocopy failed: %d", ret);

	ret = wait_for_msg(API_TEST_CMD_ECHO_RSP, &rsp, K_MSEC(1000));
	zassert_ok(ret, "No ECHO_RSP for timed nocopy send");
	zassert_mem_equal(rsp.data, expected, sizeof(expected),
			  "Timed nocopy ECHO response mismatch");

	for (size_t i = 1; i < held_count; i++) {
		ret = ipc_service_drop_tx_buffer(&ep, held_tx_bufs[i]);
		zassert_ok(ret, "drop held TX buffer %u failed: %d", i, ret);
	}
}

#define HELD_RX_BUF_MAX 32

static void *held_rx_bufs[HELD_RX_BUF_MAX];
static size_t held_rx_count;
static K_SEM_DEFINE(hold_sem, 0, HELD_RX_BUF_MAX);
static K_MUTEX_DEFINE(hold_rx_lock);

static void hold_rx_fill_expected(uint8_t *expected, uint8_t seq)
{
	memset(expected, 0xa5, API_TEST_MSG_DATA_SIZE);
	expected[0] = seq;
}

static void hold_rx_verify_payload(const struct api_test_msg *msg, uint8_t seq)
{
	uint8_t expected[API_TEST_MSG_DATA_SIZE];

	hold_rx_fill_expected(expected, seq);
	zassert_mem_equal(msg->data, expected, sizeof(expected),
			  "HOLD_RX payload mismatch for seq %u", seq);
}

static void hold_rx_exhaust_cb(const void *data, size_t len, void *priv)
{
	const struct api_test_msg *msg = data;
	uint8_t seq;
	int ret;

	if (held_rx_count >= ARRAY_SIZE(held_rx_bufs)) {
		return;
	}

	zassert_equal(len, sizeof(*msg), "Unexpected HOLD_RX length: %u", len);
	zassert_equal(msg->cmd, API_TEST_CMD_HOLD_RX_RSP,
		      "Unexpected HOLD_RX response command");

	seq = msg->data[0];
	zassert_equal(seq, held_rx_count, "Unexpected HOLD_RX seq %u vs %u",
		      seq, (unsigned int)held_rx_count);

	hold_rx_verify_payload(msg, seq);

	ret = ipc_service_hold_rx_buffer(&ep, (void *)data);
	zassert_ok(ret, "ipc_service_hold_rx_buffer failed: %d", ret);

	held_rx_bufs[held_rx_count++] = (void *)data;

	k_sem_give(&hold_sem);
}

static K_SEM_DEFINE(hold_rx_final_sem, 0, 1);

static void hold_rx_final_cb(const void *data, size_t len, void *priv)
{
	const struct api_test_msg *msg = data;

	zassert_equal(len, sizeof(*msg), "Unexpected HOLD_RX length: %u", len);
	zassert_equal(msg->cmd, API_TEST_CMD_HOLD_RX_RSP,
		      "Unexpected HOLD_RX response command");

	if (msg->data[0] != 0x42) {
		return;
	}

	hold_rx_verify_payload(msg, 0x42);

	k_sem_give(&hold_rx_final_sem);
}

static void hold_rx_cb(const void *data, size_t len, void *priv)
{
	const struct api_test_msg *msg = data;
	uint8_t expected[API_TEST_MSG_DATA_SIZE];
	int ret;

	zassert_equal(len, sizeof(*msg), "Unexpected PUSH length: %u", len);
	zassert_equal(msg->cmd, API_TEST_CMD_PUSH, "Unexpected PUSH command");

	ret = ipc_service_hold_rx_buffer(&ep, (void *)data);
	zassert_ok(ret, "ipc_service_hold_rx_buffer failed: %d", ret);

	memset(expected, 0x5a, sizeof(expected));
	zassert_mem_equal(msg->data, expected, sizeof(expected), "PUSH payload mismatch");

	ret = ipc_service_release_rx_buffer(&ep, (void *)data);
	zassert_ok(ret, "ipc_service_release_rx_buffer failed: %d", ret);

	k_sem_give(&hold_sem);
}

ZTEST(ipc_service_api, test_hold_release_rx_buffer)
{
	int ret;
	struct api_test_msg cmd = { .cmd = API_TEST_CMD_PUSH };

	if (!hold_rx_supported()) {
		ztest_test_skip();
	}

	memset(cmd.data, 0x5a, sizeof(cmd.data));
	recv_override = hold_rx_cb;

	ret = ipc_service_send(&ep, &cmd, sizeof(cmd));
	zassert_equal(ret, sizeof(cmd), "PUSH send failed: %d", ret);

	ret = k_sem_take(&hold_sem, K_MSEC(1000));
	zassert_ok(ret, "Timed out waiting for hold/release callback");

	recv_override = NULL;
}

ZTEST(ipc_service_api, test_hold_rx_buffer_exhaust)
{
	int ret;
	size_t verify_count;
	struct api_test_msg cmd = { .cmd = API_TEST_CMD_HOLD_RX };

	if (!hold_rx_supported()) {
		ztest_test_skip();
	}

	held_rx_count = 0;
	k_sem_reset(&hold_sem);
	recv_override = hold_rx_exhaust_cb;

	while (true) {
		size_t seq = held_rx_count;

		zassert_true(seq < ARRAY_SIZE(held_rx_bufs),
			     "Held RX buffer tracking array full");

		hold_rx_fill_expected(cmd.data, seq);

		ret = k_mutex_lock(&hold_rx_lock, K_MSEC(1000));
		zassert_ok(ret, "hold_rx_lock failed: %d", ret);

		ret = ipc_service_send(&ep, &cmd, sizeof(cmd));
		zassert_equal(ret, sizeof(cmd), "HOLD_RX send failed: %d", ret);

		ret = k_sem_take(&hold_sem, K_MSEC(1000));
		k_mutex_unlock(&hold_rx_lock);
		if (ret != 0) {
			recv_override = NULL;
			break;
		}

		zassert_equal(held_rx_count, seq + 1,
			      "Expected one held RX buffer per round");
	}

	recv_override = NULL;
	verify_count = held_rx_count;

	zassert_not_equal(ret, 0, "Expected timeout after exhausting RX buffers");
	zassert_true(verify_count > 0, "Failed to hold any RX buffers");

	for (size_t i = 0; i < verify_count; i++) {
		const struct api_test_msg *msg = held_rx_bufs[i];

		hold_rx_verify_payload(msg, i);
	}

	for (size_t i = 0; i < verify_count; i++) {
		ret = ipc_service_release_rx_buffer(&ep, held_rx_bufs[i]);
		zassert_ok(ret, "ipc_service_release_rx_buffer %u failed: %d", i, ret);
	}

	k_msgq_purge(&rx_msgq);
	k_sem_reset(&hold_rx_final_sem);
	hold_rx_fill_expected(cmd.data, 0x42);
	recv_override = hold_rx_final_cb;
	ret = ipc_service_send(&ep, &cmd, sizeof(cmd));
	zassert_equal(ret, sizeof(cmd), "HOLD_RX send after release failed: %d", ret);

	ret = k_sem_take(&hold_rx_final_sem, K_MSEC(1000));
	zassert_ok(ret, "No HOLD_RX_RSP after releasing held RX buffers");
	recv_override = NULL;
}

static volatile int isr_send_ret;
static K_SEM_DEFINE(isr_send_sem, 0, 1);

static void isr_send_handler(struct k_timer *timer)
{
	struct api_test_msg cmd = { .cmd = API_TEST_CMD_PING };

	ARG_UNUSED(timer);

	isr_send_ret = ipc_service_send_critical(&ep, &cmd, sizeof(cmd));
	k_sem_give(&isr_send_sem);
}

static K_TIMER_DEFINE(isr_send_timer, isr_send_handler, NULL);

ZTEST(ipc_service_api, test_send_from_isr)
{
	int ret;
	struct api_test_msg rsp;

	if (!IS_ENABLED(CONFIG_IPC_SERVICE_API_TEST_SEND_FROM_ISR)) {
		ztest_test_skip();
	}

	k_sem_reset(&isr_send_sem);
	k_timer_start(&isr_send_timer, K_NO_WAIT, K_NO_WAIT);

	ret = k_sem_take(&isr_send_sem, K_MSEC(1000));
	zassert_ok(ret, "Timed out waiting for ISR send");
	zassert_equal(isr_send_ret, sizeof(struct api_test_msg),
		      "ipc_service_send_critical failed: %d", isr_send_ret);

	ret = wait_for_msg(API_TEST_CMD_PONG, &rsp, K_MSEC(1000));
	zassert_ok(ret, "No PONG response after ISR send");
}

ZTEST(ipc_service_api, test_send_critical)
{
	int ret;
	struct api_test_msg cmd = { .cmd = API_TEST_CMD_PING };
	struct api_test_msg rsp;

	ret = ipc_service_send_critical(&ep, &cmd, sizeof(cmd));
	if (ret < 0) {
		zassert_true(ret == -ENOTSUP,
		     "send_critical should be unsupported, got %d", ret);
	} else {
		ret = wait_for_msg(API_TEST_CMD_PONG, &rsp, K_MSEC(1000));
		zassert_ok(ret, "No PONG response after ISR send");
	}
}

ZTEST_SUITE(ipc_service_api, NULL, suite_setup, suite_before, NULL, NULL);

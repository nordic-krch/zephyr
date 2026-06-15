/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ipc/ipc_service.h>

#include <api_test.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(remote, LOG_LEVEL_INF);

static const struct device *ipc0_instance = DEVICE_DT_GET(DT_ALIAS(dut_ipc));
static volatile bool ipc0_bounded;
static volatile bool close_after_unbound;
K_SEM_DEFINE(bound_sem, 0, 1);

static struct ipc_ept ep;

static struct ipc_ept_cfg ep_cfg;

static void remote_deregister_work_handler(struct k_work *work)
{
	int ret;

	ARG_UNUSED(work);

	ret = ipc_service_deregister_endpoint(&ep);
	if (ret < 0 && ret != -ENOENT) {
		LOG_ERR("ipc_service_deregister_endpoint() failed: %d", ret);
	}

	k_msleep(10);

	ret = ipc_service_register_endpoint(ipc0_instance, &ep, &ep_cfg);
	if (ret < 0) {
		LOG_ERR("ipc_service_register_endpoint() after deregister failed: %d", ret);
	}
}

static K_WORK_DELAYABLE_DEFINE(remote_deregister_work, remote_deregister_work_handler);

static void reopen_ipc_work_handler(struct k_work *work)
{
	int ret;

	ARG_UNUSED(work);


	ret = ipc_service_close_instance(ipc0_instance);
	if (ret < 0) {
		LOG_ERR("ipc_service_close_instance() after unbound failed: %d", ret);
	}

	k_msleep(1);

	ret = ipc_service_open_instance(ipc0_instance);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("ipc_service_open_instance() after unbound failed: %d", ret);
		return;
	}

	ret = ipc_service_register_endpoint(ipc0_instance, &ep, &ep_cfg);
	if (ret < 0) {
		LOG_ERR("ipc_service_register_endpoint() after unbound failed: %d", ret);
	}
}

static K_WORK_DELAYABLE_DEFINE(reopen_ipc_work, reopen_ipc_work_handler);

static void ep_bound(void *priv)
{
	ipc0_bounded = true;
	k_sem_give(&bound_sem);
	LOG_INF("Endpoint bound");
}

static void ep_unbound(void *priv)
{
	int ret;

	ipc0_bounded = false;
	k_sem_reset(&bound_sem);
	LOG_INF("Endpoint unbound");

	if (close_after_unbound) {
		close_after_unbound = false;

		ret = ipc_service_deregister_endpoint(&ep);
		LOG_ERR("ipc_service_deregister_endpoint() after unbound failed: %d", ret);
		if (ret < 0 && ret != -ENOENT) {
			LOG_ERR("ipc_service_deregister_endpoint() after unbound failed: %d", ret);
		}

		ret = k_work_schedule(&reopen_ipc_work, K_MSEC(1));
		if (ret < 0) {
			LOG_ERR("reopen_ipc_work schedule failed: %d", ret);
		}
		return;
	}

	/* By default attempt to reregister the endpoint. */
	ret = k_work_schedule(&remote_deregister_work, K_NO_WAIT);
	if (ret < 0) {
		LOG_ERR("remote_deregister_work schedule failed: %d", ret);
	}
}

static void ep_error(const char *message, void *priv)
{
	LOG_ERR("Endpoint error: %s", message);
}

static void ep_recv(const void *data, size_t len, void *priv)
{
	const struct api_test_msg *msg = data;
	struct ipc_ept *ept = priv;
	struct api_test_msg rsp;
	int ret;

	if (len < sizeof(msg->cmd)) {
		LOG_ERR("Unexpected message size: %u", len);
		return;
	}

	switch (msg->cmd) {
	case API_TEST_CMD_PING:
		rsp.cmd = API_TEST_CMD_PONG;
		memset(rsp.data, 0, sizeof(rsp.data));
		ret = ipc_service_send(ept, &rsp, sizeof(rsp));
		if (ret < 0) {
			LOG_ERR("PONG send failed: %d", ret);
		}
		break;
	case API_TEST_CMD_ECHO:
		if (len < sizeof(struct api_test_msg)) {
			LOG_ERR("ECHO message too short: %u", len);
			break;
		}
		rsp.cmd = API_TEST_CMD_ECHO_RSP;
		memcpy(rsp.data, msg->data, sizeof(rsp.data));
		ret = ipc_service_send(ept, &rsp, sizeof(rsp));
		if (ret < 0) {
			LOG_ERR("ECHO_RSP send failed: %d", ret);
		}
		break;
	case API_TEST_CMD_PUSH:
		if (len < sizeof(struct api_test_msg)) {
			LOG_ERR("PUSH message too short: %u", len);
			break;
		}
		ret = ipc_service_send(ept, msg, sizeof(*msg));
		if (ret < 0) {
			LOG_ERR("PUSH response send failed: %d", ret);
		}
		break;
	case API_TEST_CMD_HOLD_RX:
		if (len < sizeof(struct api_test_msg)) {
			LOG_ERR("HOLD_RX message too short: %u", len);
			break;
		}
		rsp.cmd = API_TEST_CMD_HOLD_RX_RSP;
		memcpy(rsp.data, msg->data, sizeof(rsp.data));
		ret = ipc_service_send(ept, &rsp, sizeof(rsp));
		if (ret < 0) {
			LOG_ERR("HOLD_RX_RSP send failed: %d", ret);
		}
		break;
	case API_TEST_CMD_CLOSE_AFTER_UNBOUND:
		close_after_unbound = true;
		break;
	case API_TEST_CMD_DEREGISTER:
		ret = k_work_schedule(&remote_deregister_work, K_NO_WAIT);
		if (ret < 0) {
			LOG_ERR("remote_deregister_work schedule failed: %d", ret);
		}
		break;
	default:
		LOG_ERR("Unhandled command: %u", msg->cmd);
		break;
	}
}

int main(void)
{
	int ret;

	ep_cfg.name = "ep0";
	ep_cfg.cb.bound = ep_bound;
	ep_cfg.cb.unbound = ep_unbound;
	ep_cfg.cb.received = ep_recv;
	ep_cfg.cb.error = ep_error;
	ep_cfg.priv = &ep;

	ret = ipc_service_open_instance(ipc0_instance);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("ipc_service_open_instance() failed: %d", ret);
		return ret;
	}

	ret = ipc_service_register_endpoint(ipc0_instance, &ep, &ep_cfg);
	if (ret < 0) {
		LOG_ERR("ipc_service_register_endpoint() failed: %d", ret);
		return ret;
	}

	k_sem_take(&bound_sem, K_FOREVER);

	LOG_INF("IPC service API remote ready");

	k_sleep(K_FOREVER);
	return 0;
}

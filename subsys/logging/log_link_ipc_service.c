/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <ipc/ipc_service.h>
#include <logging/log_link.h>
#include <logging/log_ipc_service.h>
#include <logging/log_core.h>

struct log_link_ipc_service {
	struct ipc_ept *ept;
	struct log_link_remote link_remote;
};

static void bound_cb(void *priv)
{
	struct log_link_remote *link_remote = priv;

	log_link_remote_on_started(link_remote, 0);
}

static void error_cb(const char *message, void *priv)
{
	struct log_link_remote *link_remote = priv;

	log_link_remote_on_error(link_remote, -EIO);
}

static void recv_cb(const void *data, size_t len, void *priv)
{
	struct log_link_remote *link_remote = priv;

	log_link_remote_on_recv_cb(link_remote, data, len);
}

static int link_ipc_service_send(struct log_link_remote *link_remote,
				 void *data, size_t len)
{
	struct log_link_ipc_service *link_ipc_service =
		CONTAINER_OF(link_remote, struct log_link_ipc_service, link_remote);

	return ipc_service_send(link_ipc_service->ept, data, len);
}

static int link_ipc_service_init(struct log_link_remote *link_remote)
{
	struct log_link_ipc_service *link_ipc_service =
		CONTAINER_OF(link_remote, struct log_link_ipc_service, link_remote);
	struct ipc_ept_cfg ept_cfg = {
		.name = "logging",
		.prio = 0,
		.priv = (void *)link_remote,
		.cb = {
			.bound    = bound_cb,
			.received = recv_cb,
			.error    = error_cb,
		},
	};

	return ipc_service_register_endpoint(&link_ipc_service->ept, &ept_cfg);
}

struct log_link_remote_transport_api log_link_ipc_service_transport_api = {
	.init = link_ipc_service_init,
	.send = link_ipc_service_send
};

static struct log_link_ipc_service link_ipc_service_data = {
	.link_remote = {
		.transport_api = &log_link_ipc_service_transport_api
	}
};

LOG_LINK_DEF(link_ipc_service, log_link_remote_api, &link_ipc_service_data.link_remote);

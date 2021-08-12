/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <ipc/ipc_service.h>
#include <logging/log_ipc_service.h>
#include <logging/log_backend.h>
#include <logging/log_ctrl.h>

struct log_backend_ipc_service {
	struct ipc_ept *ept;
	struct log_backend_remote backend_remote;
};

static void bound_cb(void *priv)
{
	struct log_backend_remote *backend_remote = priv;

	log_backend_remote_on_started(backend_remote, 0);
}

static void error_cb(const char *message, void *priv)
{
	struct log_backend_remote *backend_remote = priv;

	log_backend_remote_on_error(backend_remote, -EIO);
}

static void recv_cb(const void *data, size_t len, void *priv)
{
	struct log_backend_remote *backend_remote = priv;

	log_backend_remote_on_recv_cb(backend_remote, data, len);
}

static int backend_ipc_service_send(struct log_backend_remote *backend_remote,
				 void *data, size_t len)
{
	struct log_backend_ipc_service *backend_ipc_service =
		CONTAINER_OF(backend_remote, struct log_backend_ipc_service, backend_remote);

	return ipc_service_send(backend_ipc_service->ept, data, len);
}

static int backend_ipc_service_init(struct log_backend_remote *backend_remote)
{
	struct log_backend_ipc_service *backend_ipc_service =
		CONTAINER_OF(backend_remote, struct log_backend_ipc_service, backend_remote);
	struct ipc_ept_cfg ept_cfg = {
		.name = "logging",
		.prio = 0,
		.cb = {
			.bound    = bound_cb,
			.received = recv_cb,
			.error    = error_cb,
		},
		.priv = (void *)backend_remote
	};

	return ipc_service_register_endpoint(&backend_ipc_service->ept, &ept_cfg);
}

struct log_backend_remote_transport_api log_backend_ipc_service_transport_api = {
	.init = backend_ipc_service_init,
	.send = backend_ipc_service_send
};

static struct log_backend_ipc_service backend_ipc_service_data = {
	.backend_remote = {
		.transport_api = &log_backend_ipc_service_transport_api
	}
};

LOG_BACKEND_DEFINE(backend_ipc_service,
		   log_backend_remote_api,
		   true,
		   &backend_ipc_service_data.backend_remote);

/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_LOGGING_LOG_IPC_SERVICE_H_
#define ZEPHYR_INCLUDE_LOGGING_LOG_IPC_SERVICE_H_

#define Z_LOG_MULTIDOMAIN_ID_MSG 0
#define Z_LOG_MULTIDOMAIN_ID_GET_DOMAIN_CNT 1
#define Z_LOG_MULTIDOMAIN_ID_GET_SOURCE_CNT 2
#define Z_LOG_MULTIDOMAIN_ID_GET_DOMAIN_NAME 3
#define Z_LOG_MULTIDOMAIN_ID_GET_SOURCE_NAME 4
#define Z_LOG_MULTIDOMAIN_ID_GET_LEVELS 5
#define Z_LOG_MULTIDOMAIN_ID_SET_RUNTIME_LEVEL 6
#define Z_LOG_MULTIDOMAIN_ID_GET_TIMESTAMP_FREQ 7
#define Z_LOG_MULTIDOMAIN_ID_DROPPED 8

#define Z_LOG_MULTIDOMAIN_STATUS_OK 0
#define Z_LOG_MULTIDOMAIN_STATUS_ERR 1

struct log_multidomain_log_msg {
	uint8_t data[0];
} __packed;

struct log_multidomain_domain_cnt {
	uint16_t count;
} __packed;

struct log_multidomain_source_cnt {
	uint8_t domain_id;
	uint16_t count;
} __packed;

struct log_multidomain_domain_name {
	uint8_t domain_id;
	char name[0];
} __packed;

struct log_multidomain_source_name {
	uint8_t domain_id;
	uint16_t source_id;
	char name[0];
} __packed;

struct log_multidomain_levels {
	uint8_t domain_id;
	uint16_t source_id;
	uint8_t level;
	uint8_t runtime_level;
} __packed;

struct log_multidomain_set_runtime_level {
	uint8_t domain_id;
	uint16_t source_id;
	uint8_t runtime_level;
} __packed;

struct log_multidomain_dropped {
	uint32_t dropped;
} __packed;

union log_multidomain_msg_data {
	struct log_multidomain_log_msg log_msg;
	struct log_multidomain_domain_cnt domain_cnt;
	struct log_multidomain_source_cnt source_cnt;
	struct log_multidomain_domain_name domain_name;
	struct log_multidomain_source_name source_name;
	struct log_multidomain_levels levels;
	struct log_multidomain_set_runtime_level set_rt_level;
	struct log_multidomain_dropped dropped;
};

struct log_multidomain_msg {
	uint8_t id;
	uint8_t status;
	union log_multidomain_msg_data data;
} __packed;

struct log_link_remote;

struct log_link_remote_transport_api {
	int (*init)(struct log_link_remote *link);
	int (*send)(struct log_link_remote *link, void *data, size_t len);
};

union log_link_remote_dst {
	uint16_t count;

	struct {
		char *dst;
		uint32_t *len;
	} name;

	struct {
		uint8_t level;
		uint8_t runtime_level;
	} levels;

	struct {
		uint8_t level;
	} set_runtime_level;

	uint32_t timestamp_freq;
};

extern struct log_link_api log_link_remote_api;

struct log_link_remote {
	const struct log_link_remote_transport_api *transport_api;
	struct k_sem rdy_sem;
	const struct log_link *link;
	union log_link_remote_dst dst;
	int status;
	bool ready;
};

struct log_backend_remote;

struct log_backend_remote_transport_api {
	int (*init)(struct log_backend_remote *remote_backend);
	int (*send)(struct log_backend_remote *remote_backend, void *data, size_t len);
};

extern const struct log_backend_api log_backend_remote_api;

struct log_backend_remote {
	const struct log_backend_remote_transport_api *transport_api;
	const struct log_backend *log_backend;
	struct k_sem rdy_sem;
	bool panic;
	int status;
};

void log_link_remote_on_recv_cb(struct log_link_remote *link_remote,
				const void *data, size_t len);
void log_link_remote_on_error(struct log_link_remote *link_remote, int err);
void log_link_remote_on_started(struct log_link_remote *link_remote, int err);

void log_backend_remote_on_recv_cb(struct log_backend_remote *backend_remote,
				   const void *data, size_t len);
void log_backend_remote_on_error(struct log_backend_remote *backend_remote, int err);
void log_backend_remote_on_started(struct log_backend_remote *backend_remote, int err);

#endif /* ZEPHYR_INCLUDE_LOGGING_LOG_IPC_SERVICE_H_ */

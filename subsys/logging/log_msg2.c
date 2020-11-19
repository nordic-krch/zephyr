/*
 * Copyright (c) 2020 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <logging/log_msg2.h>
#include <logging/log_core2.h>
#include <logging/log_ctrl.h>

void z_log_msg2_static_finalize(struct log_msg2 *msg, void *source,
				const struct log_msg2_desc *desc)
{
	msg->hdr.desc = *desc;
	msg->hdr.source = source;
	z_log_msg2_commit(msg);
}

static void z_log_msg2_ext_finalize(struct log_msg2 *msg, void *source,
			     const struct log_msg2_desc *desc, uint8_t *data)
{
	if (data) {
		uint8_t *d = msg->data + desc->package_len;

		memcpy(d, data, desc->data_len);
	}

	z_log_msg2_static_finalize(msg, source, desc);
}

void z_log_msg2_static_create(void *source, const struct log_msg2_desc *desc,
			      uint8_t *package, uint8_t *data)
{
	size_t msg_len = log_msg2_get_total_len(desc);
	struct log_msg2 *msg = z_log_msg2_alloc(msg_len);

	if (!msg) {
		return;
	}

	uint8_t *d = msg->data;

	memcpy(d, package, desc->package_len);
	z_log_msg2_ext_finalize(msg, source, desc, data);
}

void z_log_msg2_runtime_vcreate(uint8_t domain_id, void *source,
			       uint8_t level, uint8_t *data, size_t dlen,
			       const char *fmt, va_list ap)
{
	size_t plen;
	int err;

	if (fmt) {
		err = cbvprintf_package(NULL, &plen,
					CBPRINTF_PACKAGE_FMT_NO_INLINE,
					fmt, ap);
		if (err < 0) {
			return;
		}
	} else {
		plen = 0;
	}

	size_t msg_len = plen + sizeof(struct log_msg2_hdr) + dlen;
	struct log_msg2 *msg = z_log_msg2_alloc(msg_len);

	if (!msg) {
		return;
	}

	uint8_t *d = msg->data;

	if (fmt) {
		err = cbvprintf_package(d, &plen,
					CBPRINTF_PACKAGE_FMT_NO_INLINE,
					fmt, ap);
	}

	if (err < 0) {
		msg->hdr.desc.package_len = plen;
		msg->hdr.desc.data_len = dlen;
		z_log_msg2_free((union log_msg2_generic *)msg);
		return;
	}

	struct log_msg2_desc desc =
		Z_LOG_MSG_DESC_INITIALIZER(domain_id, level, plen, dlen);

	z_log_msg2_ext_finalize(msg, source, &desc, data);
}


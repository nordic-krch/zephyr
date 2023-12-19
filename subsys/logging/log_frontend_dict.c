/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/logging/log_frontend.h>
#include <zephyr/logging/log_frontend_dict.h>
#include <zephyr/logging/log_output_dict.h>
#include <zephyr/logging/log_internal.h>
#include <zephyr/sys/cobs.h>

struct log_frontend_pkt_hdr {
	MPSC_PBUF_HDR;
	uint16_t len: 12;
	uint16_t noff: 2;
} __packed;

BUILD_ASSERT(sizeof(struct log_frontend_pkt_hdr) == sizeof(uint16_t));

struct log_frontend_generic_pkt {
	struct log_frontend_pkt_hdr hdr;
	uint8_t padding;
	uint8_t cobs_hdr;
	uint8_t data[0];
} __packed;

struct log_frontend_dropped_pkt {
	struct log_frontend_pkt_hdr hdr;
	struct log_dict_output_dropped_msg_t data;
} __packed;

struct log_frontend_log_pkt {
	struct log_frontend_pkt_hdr hdr;
	uint8_t padding;
	uint8_t cobs_hdr;
	struct log_dict_output_normal_msg_hdr_t data_hdr;
	uint8_t data[0];
} __packed;

/* Union needed to avoid warning when casting to packed structure. */
union log_frontend_pkt {
	struct log_frontend_generic_pkt *generic;
	struct log_frontend_dropped_pkt *dropped;
	struct log_frontend_log_pkt *log;
	const union mpsc_pbuf_generic *ro_pkt;
	union mpsc_pbuf_generic *rw_pkt;
	void *buf;
};

static uint32_t dbuf[CONFIG_LOG_FRONTEND_DICT_BUF_SIZE / sizeof(uint32_t)];

static uint32_t get_wlen(const union mpsc_pbuf_generic *packet);
static void notify_drop(const struct mpsc_pbuf_buffer *buffer,
			const union mpsc_pbuf_generic *packet);

static const struct mpsc_pbuf_buffer_config config = {
	.buf = dbuf,
	.size = ARRAY_SIZE(dbuf),
	.notify_drop = notify_drop,
	.get_wlen = get_wlen,
	.flags = 0
};

static struct mpsc_pbuf_buffer buf;
static atomic_t dropped;
static atomic_t tx_active;
static struct k_work sink_work;
static bool in_panic;

#define PKT_WSIZE(nargs) \
	(sizeof(struct log_frontend_log_pkt) + (3 + nargs) * sizeof(uint32_t)) / sizeof(uint32_t)

#define PKT_SIZE(nargs) \
	(sizeof(struct log_frontend_log_pkt) + (2 + nargs) * sizeof(uint32_t)) + 2

static uint32_t get_wlen(const union mpsc_pbuf_generic *packet)
{
	struct log_frontend_generic_pkt *hdr = (struct log_frontend_generic_pkt *)packet;

	return (uint32_t)hdr->hdr.len;
}

static void notify_drop(const struct mpsc_pbuf_buffer *buffer,
			const union mpsc_pbuf_generic *packet)
{
}

static inline uint16_t get_source_id(const void * source)
{
	return (source != NULL) ?
				(IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ?
					log_dynamic_source_id((void *)source) :
					log_const_source_id(source)) :
				0U;
}

static inline void hdr_set(struct log_frontend_log_pkt *log, uint32_t level, const void *source,
		uint32_t plen, uint32_t dlen)
{
	size_t mlen = sizeof(struct log_frontend_log_pkt) + plen + dlen +
			(IS_ENABLED(CONFIG_LOG_FRONTEND_DICT_COBS) ? 1 : 0);

	log->hdr.len = DIV_ROUND_UP(mlen, sizeof(uint32_t));
	log->hdr.noff = (log->hdr.len * sizeof(uint32_t)) - mlen;
	log->data_hdr.type = MSG_NORMAL;
	log->data_hdr.domain = Z_LOG_LOCAL_DOMAIN_ID;
	log->data_hdr.level = level;
	log->data_hdr.package_len = plen;
	log->data_hdr.data_len = dlen;
	log->data_hdr.padding = 0;
	log->data_hdr.source = get_source_id(source);
	log->data_hdr.timestamp = z_log_timestamp();
}

static bool pkt_send(void)
{
	union log_frontend_pkt pkt;
	uint8_t *data;
	size_t len;

	do {
		pkt.ro_pkt = mpsc_pbuf_claim(&buf);
		if (pkt.ro_pkt == NULL) {
			atomic_set(&tx_active, 0);
			return false;
		}

		if (IS_ENABLED(CONFIG_LOG_FRONTEND_DICT_COBS)) {
			size_t cobs_len = 0/*TODO*/ + sizeof(uint8_t) +
					  sizeof(struct log_dict_output_normal_msg_hdr_t);

			if (cobs_r_encode((uint8_t *)&pkt.log->cobs_hdr, cobs_len)) {
				pkt.log->hdr.noff--;
			}

			data = (uint8_t *)&pkt.log->cobs_hdr;
			len = 0;
		} else {
			data = (uint8_t *)&pkt.log->data_hdr;
			len = pkt.generic->hdr.len * sizeof(uint32_t) - pkt.generic->hdr.noff -
				offsetof(struct log_frontend_generic_pkt, data);
		}

		if (in_panic || !IS_ENABLED(CONFIG_LOG_FRONTEND_DICT_ASYNC)) {
			(void)log_frontend_dict_tx_blocking(data, len, in_panic);
			mpsc_pbuf_free(&buf, pkt.ro_pkt);
		} else {
			(void)log_frontend_dict_tx_async(data, len);
		}
	} while (in_panic);

	return true;
}

void log_frontend_dict_tx_from_cb(void)
{
	if (!pkt_send()) {
		atomic_set(&tx_active, 0);
	}
}

static void work_handler(struct k_work *work)
{
	if (pkt_send()) {
		k_work_submit(work);
	}
}

static void pkt_try_send(void)
{
	if (in_panic) {
		pkt_send();
		return;
	}

	if (k_is_pre_kernel()) {
		return;
	}

	if (IS_ENABLED(CONFIG_LOG_FRONTEND_DICT_ASYNC)) {
		if ((atomic_cas(&tx_active, 0, 1) == false)) {
			return;
		}

		pkt_send();
	} else {
		(void)k_work_submit(&sink_work);
	}
}

static void *pkt_alloc(size_t len)
{
	size_t wlen = DIV_ROUND_UP(len, sizeof(uint32_t));

	return (void *)mpsc_pbuf_alloc(&buf, wlen, K_NO_WAIT);
}

static void pkt_write(union log_frontend_pkt pkt, size_t len)
{
	pkt.rw_pkt->hdr.valid = 1;
	mpsc_pbuf_put_data(&buf, (const uint32_t *)pkt.rw_pkt, len);
}

static void pkt_commit(union log_frontend_pkt pkt)
{
	mpsc_pbuf_commit(&buf, pkt.rw_pkt);
}

static void pkt_free(union log_frontend_pkt pkt)
{
	mpsc_pbuf_free(&buf, pkt.ro_pkt);
}

void package_process(union log_frontend_pkt pkt, size_t plen)
{
	pkt_write(pkt, plen);
	pkt_try_send();
}

void log_frontend_msg(const void *source,
		      const struct log_msg_desc desc,
		      uint8_t *package, const void *data)
{
	void *buf;
	uint16_t strl[4];
	int plen = cbprintf_package_copy(package, desc.package_len, NULL, 0,
					 CBPRINTF_PACKAGE_CONVERT_RW_STR,
					 strl, ARRAY_SIZE(strl));
	size_t dlen = desc.data_len;
	size_t total_len = plen + dlen + sizeof(struct log_frontend_log_pkt);

	buf = pkt_alloc(total_len);
	if (buf == NULL) {
		atomic_inc(&dropped);
		return;
	}

	union log_frontend_pkt pkt = { .buf = buf };
	uint32_t *dst_package = (uint32_t *)pkt.log->data;

	hdr_set(pkt.log, desc.level, source, plen, 0);
	int err = cbprintf_package_copy(package, desc.package_len, dst_package, plen,
					 CBPRINTF_PACKAGE_CONVERT_RW_STR,
					 strl, ARRAY_SIZE(strl));
	if (err < 0) {
		pkt_free(pkt);
		atomic_inc(&dropped);
		return;
	}

	if (dlen != 0) {
		memcpy(&pkt.log->data[plen], data, dlen);
	}

	pkt_commit(pkt);
	pkt_try_send();
}


void log_frontend_simple_0(const void *source, uint32_t level, const char *fmt)
{
	static const uint32_t arg_cnt = 0;
	static const size_t len32 = PKT_WSIZE(arg_cnt);
	uint32_t pkt_buf[len32];
	union log_frontend_pkt pkt = { .log = (struct log_frontend_log_pkt *)pkt_buf };
	uint32_t *package = (uint32_t *)pkt.log->data;
	static const uint8_t package_wlen = 2 + arg_cnt;
	static const size_t plen = package_wlen * sizeof(uint32_t);
	static const union cbprintf_package_hdr package_hdr = {
		.desc = { .len = package_wlen }
	};

	hdr_set(pkt.log, level, source, plen, 0);
	package[0] = (uint32_t)(uintptr_t)package_hdr.raw;
	package[1] = (uintptr_t)fmt;

	package_process(pkt, len32);
}

void log_frontend_simple_1(const void *source, uint32_t level, const char *fmt, uint32_t arg)
{
	static const uint32_t arg_cnt = 1;
	static const size_t len32 = PKT_WSIZE(arg_cnt);
	uint32_t pkt_buf[len32];
	union log_frontend_pkt pkt = { .log = (struct log_frontend_log_pkt *)pkt_buf };
	uint32_t *package = (uint32_t *)pkt.log->data;
	static const uint8_t package_wlen = arg_cnt + 2;
	static const size_t plen = package_wlen * sizeof(uint32_t);
	static const union cbprintf_package_hdr package_hdr = {
		.desc = { .len = package_wlen }
	};

	hdr_set(pkt.log, level, source, plen, 0);
	package[0] = (uint32_t)(uintptr_t)package_hdr.raw;
	package[1] = (uintptr_t)fmt;
	package[2] = arg;

	package_process(pkt, len32);
}

void log_frontend_simple_2(const void *source, uint32_t level,
			   const char *fmt, uint32_t arg0, uint32_t arg1)
{
	static const uint32_t arg_cnt = 2;
	static const size_t len32 = PKT_WSIZE(arg_cnt);
	uint32_t pkt_buf[len32];
	union log_frontend_pkt pkt = { .log = (struct log_frontend_log_pkt *)pkt_buf };
	uint32_t *package = (uint32_t *)pkt.log->data;
	static const uint8_t package_wlen = arg_cnt + 2;
	static const size_t plen = package_wlen * sizeof(uint32_t);
	static const union cbprintf_package_hdr package_hdr = {
		.desc = { .len = package_wlen }
	};

	hdr_set(pkt.log, level, source, plen, 0);
	package[0] = (uint32_t)(uintptr_t)package_hdr.raw;
	package[1] = (uintptr_t)fmt;
	package[2] = arg0;
	package[3] = arg1;

	package_process(pkt, len32);
}

void log_frontend_init(void)
{
	mpsc_pbuf_init(&buf, &config);
}

void log_frontend_panic(void)
{
	in_panic = true;
}

static int sync_init(void)
{
	if (!IS_ENABLED(CONFIG_LOG_FRONTEND_DICT_ASYNC)) {
		k_work_init(&sink_work, work_handler);
	}

	return log_frontend_dict_init();
}

SYS_INIT(sync_init, POST_KERNEL, 0);

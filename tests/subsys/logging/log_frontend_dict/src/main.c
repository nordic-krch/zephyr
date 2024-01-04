/*
 * Copyright (c) 2024 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <zephyr/ztest.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/cbprintf.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_msg.h>
#include <zephyr/logging/log_frontend.h>
#include <zephyr/logging/log_frontend_dict.h>
#include <zephyr/logging/log_output_dict.h>

#define TEST_MODULE_NAME
LOG_MODULE_REGISTER(TEST_MODULE_NAME);

struct test_data {
	uint8_t *msgs[10];
	size_t len[10];
	bool panic[10];
	int cnt;
	struct k_timer timer;
	atomic_t busy;
};

struct test_data expected;
static int16_t test_sid;
static void *source;
static log_timestamp_t ts;
static int exp_cnt;

/* Have to create copy of cbprintf_package() to allow non string literal arguments.
 * cbprintf_package() has printf_like attribute.
 */
int package(void *packaged, size_t len, uint32_t flags, const char *format, ...)
{
	va_list ap;
	int ret;

	va_start(ap, format);
	ret = cbvprintf_package(packaged, len, flags, format, ap);
	va_end(ap);
	return ret;
}

static void async_timeout(struct k_timer *timer)
{
	log_frontend_dict_tx_from_cb();
	atomic_set(&expected.busy, 0);
}

int log_frontend_dict_init(void)
{
	/* mock frontend */
	return 0;
}

int log_frontend_dict_tx_blocking(const uint8_t *buf, size_t len, bool panic)
{
	printk("buf len:%d\n", len);
	for (int i = 0; i < len; i++) {
		printk("%02x ", buf[i]);
	}
	printk("\n");
	printk("exp_buf:%p len:%d\n", expected.msgs[expected.cnt], expected.len[expected.cnt]);
	for (int i = 0; i < len; i++) {
		printk("%02x ", expected.msgs[expected.cnt][i]);
	}
	printk("\n");
	zassert_equal(len, expected.len[expected.cnt]);
	zassert_equal(panic, expected.panic[expected.cnt]);

	zassert_equal(memcmp(buf, expected.msgs[expected.cnt], len), 0);

	expected.cnt++;

	return 0;
}

int log_frontend_dict_tx_async(const uint8_t *buf, size_t len)
{
	log_frontend_dict_tx_blocking(buf, len, false);
	printk("ac\n");
	k_timer_start(&expected.timer, K_MSEC(1), K_NO_WAIT);

	return 0;
}

static int build_msg(uint8_t *buf, size_t len,
		     void *source, uint32_t level, log_timestamp_t ts,
		     uint8_t *package, size_t plen, uint8_t *data, size_t dlen, bool cobs)
{
	printk("src:%d\n", log_const_source_id(source));

	size_t out_len = 0;
	struct log_dict_output_normal_msg_hdr_t hdr = {
		.type = MSG_NORMAL,
		.domain = Z_LOG_LOCAL_DOMAIN_ID,
		.level = level,
		.package_len = plen,
		.data_len = dlen,
		.source = log_const_source_id(source),
		.timestamp = ts,
	};

	if (len <= (out_len + sizeof(hdr))) {
		return -1;
	}

	memcpy(&buf[out_len], (void *)&hdr, sizeof(hdr));
	out_len += sizeof(hdr);

	if (len <= (out_len + plen)) {
		return -1;
	}
	memcpy(&buf[out_len], package, plen);
	out_len += plen;

	if (len <= (out_len + dlen)) {
		return -1;
	}
	memcpy(&buf[out_len], data, dlen);
	out_len += dlen;

	if (cobs) {

	}

	return (int)out_len;
}


static int log_vregister(void *source, uint32_t level, struct log_msg_desc *desc,
			 log_timestamp_t timestamp, uint8_t *pbuf, size_t pbuf_len,
			 uint8_t *data, size_t dlen, const char *fmt, va_list ap)
{
	static uint8_t buf_tmp[128] __aligned(8);
	int plen;
	int msg_len;
	int rv;

	rv = cbvprintf_package(buf_tmp, sizeof(buf_tmp), CBPRINTF_PACKAGE_ADD_RW_STR_POS, fmt, ap);
	zassert_true(rv > 0);

	plen = cbprintf_package_copy(buf_tmp, rv, pbuf, pbuf_len,
				   CBPRINTF_PACKAGE_CONVERT_RW_STR, NULL, 0);
	zassert_true(rv > 0);

	size_t buf_len = plen + dlen + sizeof(struct log_dict_output_normal_msg_hdr_t) + 8;
	uint8_t *exp_buf;

       	exp_buf = malloc(buf_len);
	if (exp_buf == NULL) {
		return -1;
	}

	msg_len = build_msg(exp_buf, buf_len, source, level, timestamp, pbuf, plen, NULL, 0, false);
	zassert_true(msg_len > 0);

	printk("exp:%p\n", exp_buf);
	for (int i = 0; i < msg_len; i++) printk("%02x ", exp_buf[i]);
	printk("\n");

	expected.msgs[exp_cnt] = exp_buf;
	expected.len[exp_cnt] = msg_len;
	exp_cnt++;

	if (desc) {
		desc->domain = Z_LOG_LOCAL_DOMAIN_ID;
		desc->level = level;
		desc->package_len = plen;
		desc->data_len = dlen;
	}

	return 0;
}

static int log_register(void *source, uint32_t level, struct log_msg_desc *desc,
			log_timestamp_t timestamp, uint8_t *pbuf, size_t pbuf_len,
			uint8_t *data, size_t dlen, const char *fmt, ...)
{
	va_list ap;
	int rv;

	va_start(ap, fmt);
	rv = log_vregister(source, level, desc, timestamp, pbuf, pbuf_len, data, dlen, fmt, ap);
	va_end(ap);

	return rv;
}

ZTEST(test_log_frontend_dict, test_log_msg)
{
	static uint8_t pbuf[256] __aligned(8);
	static const char *fmt0 = "test";
	static const char *fmt1 = "test %d";
	static const char *fmt2 = "test %d %d";
	static const char *fmt3 = "test %s";
	char tmp_str[] = "temp string";
	uint8_t hexdump[] = {1, 2, 3, 4, 5, 6, 7};
	struct log_msg_desc desc;
	int arg0 = 100;
	int arg1 = 1000;
	uint32_t level = LOG_LEVEL_INF;
	int rv;

	/* Test message with raw string. */
	ts = 0x11223344;
	rv = log_register(source, level, NULL, ts, pbuf, sizeof(pbuf), NULL, 0, fmt0);
	zassert_equal(rv, 0);

	z_log_msg_simple_create_0(source, level, fmt0);

	/* Test message with string with 1 argument. */
	ts++;
	rv = log_register(source, level, NULL, ts, pbuf, sizeof(pbuf), NULL, 0, fmt1, arg0);
	zassert_equal(rv, 0);

	z_log_msg_simple_create_1(source, level, fmt1, arg0);

	/* Test message with string with 2 arguments. */
	ts++;
	rv = log_register(source, level, NULL, ts, pbuf, sizeof(pbuf), NULL, 0, fmt2, arg0, arg1);
	zassert_equal(rv, 0);

	z_log_msg_simple_create_2(source, level, fmt2, arg0, arg1);

	/* Test message with string with RW string argument. */
	ts++;
	rv = log_register(source, level, &desc, ts, pbuf, sizeof(pbuf), NULL, 0, fmt3, tmp_str);
	zassert_equal(rv, 0);

	z_log_msg_static_create(source, desc, pbuf, NULL);

	k_msleep(150);

	zassert_equal(exp_cnt, expected.cnt);

	/* Test message with string with RW string argument and hexdump. */
	ts++;
	rv = log_register(source, level, &desc, ts, pbuf, sizeof(pbuf),
			  hexdump, sizeof(hexdump), fmt3, tmp_str);
	zassert_equal(rv, 0);

	z_log_msg_static_create(source, desc, pbuf, hexdump);

	k_msleep(50);

	zassert_equal(exp_cnt, expected.cnt);

	for (int i = 0; i < exp_cnt; i++) {
		free(expected.msgs[i]);
	}
}

log_timestamp_t test_timestamp(void)
{
	return ts;
}

static void *setup(void)
{
	test_sid = log_source_id_get(STRINGIFY(TEST_MODULE_NAME));
	source = (void *)__log_current_const_data;

	log_set_timestamp_func(test_timestamp, 10000);
	return NULL;
}

static void before(void *fixture)
{
	exp_cnt = 0;
	memset(&expected, 0, sizeof(expected));
	k_timer_init(&expected.timer, async_timeout, NULL);
}

ZTEST_SUITE(test_log_frontend_dict, NULL, setup, before, NULL, NULL);


/*
 * Copyright (c) 2018 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Test log message
 */

#include <logging/log_msg2.h>
#include <logging/log_ctrl.h>
#include <logging/log_instance.h>

#include <tc_util.h>
#include <stdbool.h>
#include <zephyr.h>
#include <ztest.h>
#include <sys/cbprintf.h>

#define TEST_TIMESTAMP_INIT_VALUE \
	COND_CODE_1(CONFIG_LOG_TIMESTAMP_64BIT, (0x1234123412), (0x11223344))

static log_timestamp_t timestamp;

log_timestamp_t get_timestamp(void)
{
	return timestamp;
}

static void test_init(void)
{
	timestamp = TEST_TIMESTAMP_INIT_VALUE;
	z_log_msg2_init();
	log_set_timestamp_func(get_timestamp, 0);
}

void print_msg(struct log_msg2 *msg)
{
	printk("package len: %d, data len: %d\n",
			msg->hdr.desc.package_len,
			msg->hdr.desc.data_len);
	for (int i = 0; i < msg->hdr.desc.package_len; i++) {
		printk("%02x ", msg->data[i]);
	}
	printk("\n");
	printk("source: %p\n", msg->hdr.source);
	printk("timestamp: %d\n", msg->hdr.timestamp);
}

struct test_buf {
	char *buf;
	int idx;
};

int out(int c, void *ctx)
{
	struct test_buf *t = ctx;

	t->buf[t->idx++] = c;

	return c;
}

static void basic_validate(struct log_msg2 *msg,
			   const struct log_source_const_data *source,
			   uint8_t domain, uint8_t level, log_timestamp_t t,
			   uint8_t *data, size_t data_len, char *str)
{
	int rv;
	uint8_t *d;
	size_t len = 0;
	char buf[256];
	struct test_buf tbuf = { .buf = buf, .idx = 0 };

	zassert_equal(log_msg2_get_source(msg), (void *)source, NULL);
	zassert_equal(log_msg2_get_domain(msg), domain, NULL);
	zassert_equal(log_msg2_get_level(msg), level, NULL);
	zassert_equal(log_msg2_get_timestamp(msg), t, NULL);

	d = log_msg2_get_data(msg, &len);
	zassert_equal(len, data_len, NULL);
	if (len) {
		rv = memcmp(d, data, data_len);
		zassert_equal(rv, 0, NULL);
	}

	data = log_msg2_get_package(msg, &len);
	if (str) {
		rv = cbpprintf(out, &tbuf,
			       CBPRINTF_PACKAGE_FMT_NO_INLINE, data);
		buf[rv] = '\0';
		zassert_true(rv > 0, NULL);

		rv = strncmp(buf, str, sizeof(buf));
		zassert_equal(rv, 0, NULL);
	}
}

void test_log_msg2(void)
{
	static const uint8_t domain = 3;
	static const uint8_t level = 2;
	static const struct log_source_const_data source = {
		.name = "test name"
	};
	union log_msg2_generic *msg0;
	union log_msg2_generic *msg1;
	union log_msg2_generic *msg2;
	size_t len0, len1, len2;
	int mode;
	int rv;

	test_init();
#define TEST_MSG0 "0 args"

	Z_LOG_MSG2_CREATE(1, mode, 0, domain, &source, level,
			  NULL, 0, TEST_MSG0);
	Z_LOG_MSG2_CREATE(0, mode, 0, domain, &source, level,
			  NULL, 0, TEST_MSG0);
	z_log_msg2_runtime_create(domain, (void *)&source,
				  level, NULL, 0, TEST_MSG0);

	msg0 = z_log_msg2_claim();
	len0 = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg0);
	msg1 = z_log_msg2_claim();
	len1 = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg1);
	msg2 = z_log_msg2_claim();
	len2 = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg2);
	zassert_equal(len0, len1, NULL);
	zassert_equal(len1, len2, NULL);

	rv = memcmp(msg0, msg1, len0);
	zassert_equal(rv, 0, "Unxecpted memcmp result: %d", rv);

	rv = memcmp(msg0, msg2, len0);
	zassert_equal(rv, 0, "Unxecpted memcmp result: %d", rv);

	basic_validate(&msg0->log, &source, domain, level,
			TEST_TIMESTAMP_INIT_VALUE, NULL, 0, TEST_MSG0);
	z_log_msg2_free(msg0);
	z_log_msg2_free(msg1);
	z_log_msg2_free(msg2);

#define TEST_MSG1 "%d %d %lld %f %f %p %p"

	uint8_t u = 0x45;
	signed char s8 = -5;
	uint64_t lld = 0x12341234563412;
	float f = 1.234;
	double d = 11.3434;
	char str[256];
	static const int iarray[] = {1, 2, 3, 4};

	Z_LOG_MSG2_CREATE(1, mode, 0, domain, &source, level, NULL, 0,
			TEST_MSG1, s8, u, lld, f, d, (void *)str, iarray);
	Z_LOG_MSG2_CREATE(0, mode, 0, domain, &source, level, NULL, 0,
			TEST_MSG1, s8, u, lld, f, d, (void *)str, iarray);
	z_log_msg2_runtime_create(domain, (void *)&source, level, NULL, 0,
			TEST_MSG1, s8, u, lld, f, d, (void *)str, iarray);
	snprintfcb(str, sizeof(str), TEST_MSG1, s8, u, lld, f, d,
		   (void *)str, iarray);

	msg0 = z_log_msg2_claim();
	len0 = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg0);
	msg1 = z_log_msg2_claim();
	len1 = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg1);
	msg2 = z_log_msg2_claim();
	len2 = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg2);
	zassert_equal(len0, len1, NULL);
	zassert_equal(len1, len2, NULL);

	rv = memcmp(msg0, msg1, len0);
	zassert_equal(rv, 0, "Unxecpted memcmp result: %d", rv);

	print_msg(&msg0->log);
	printk("-----------\n");
	print_msg(&msg2->log);
	rv = memcmp(msg0, msg2, len0);
	zassert_equal(rv, 0, "Unxecpted memcmp result: %d", rv);

	basic_validate(&msg0->log, &source, domain, level,
			TEST_TIMESTAMP_INIT_VALUE, NULL, 0, str);

	z_log_msg2_free(msg0);
	z_log_msg2_free(msg1);
	z_log_msg2_free(msg2);
	/* only data */
	uint8_t testdata[] = {1, 3, 5, 7, 9};

	Z_LOG_MSG2_CREATE(1, mode, 0, domain, &source, level, testdata,
			sizeof(testdata), NULL);
	Z_LOG_MSG2_CREATE(0, mode, 0, domain, &source, level, testdata,
			sizeof(testdata), NULL);
	z_log_msg2_runtime_create(domain, (void *)&source, level, testdata,
			sizeof(testdata), NULL);

	msg0 = z_log_msg2_claim();
	len0 = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg0);
	msg1 = z_log_msg2_claim();
	len1 = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg1);
	msg2 = z_log_msg2_claim();
	len2 = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg2);
	zassert_equal(len0, len1, NULL);
	zassert_equal(len1, len2, NULL);

	rv = memcmp(msg0, msg1, len0);
	zassert_equal(rv, 0, "Unxecpted memcmp result: %d", rv);

	rv = memcmp(msg0, msg2, len0);
	zassert_equal(rv, 0, "Unxecpted memcmp result: %d", rv);

	basic_validate(&msg0->log, &source, domain, level,
			TEST_TIMESTAMP_INIT_VALUE, testdata, sizeof(testdata),
			NULL);

	z_log_msg2_free(msg0);
	z_log_msg2_free(msg1);
	z_log_msg2_free(msg2);

	/* string + data */
	Z_LOG_MSG2_CREATE(1, mode, 0, domain, &source, level, testdata,
			sizeof(testdata), TEST_MSG1, s8, u, lld, f, d,
			(void *)str, iarray);
	Z_LOG_MSG2_CREATE(0, mode, 0, domain, &source, level, testdata,
			sizeof(testdata), TEST_MSG1, s8, u, lld, f, d,
			(void *)str, iarray);
	z_log_msg2_runtime_create(domain, (void *)&source, level, testdata,
			sizeof(testdata), TEST_MSG1, s8, u, lld, f, d,
			(void *)str, iarray);

	msg0 = z_log_msg2_claim();
	len0 = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg0);
	msg1 = z_log_msg2_claim();
	len1 = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg1);
	msg2 = z_log_msg2_claim();
	len2 = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg2);
	zassert_equal(len0, len1, NULL);
	zassert_equal(len1, len2, NULL);

	rv = memcmp(msg0, msg1, len0);
	zassert_equal(rv, 0, "Unxecpted memcmp result: %d", rv);

	rv = memcmp(msg0, msg2, len0);
	zassert_equal(rv, 0, "Unxecpted memcmp result: %d", rv);

	basic_validate(&msg0->log, &source, domain, level,
			TEST_TIMESTAMP_INIT_VALUE, testdata, sizeof(testdata),
			str);

	z_log_msg2_free(msg0);
	z_log_msg2_free(msg1);
	z_log_msg2_free(msg2);
}

void test_msg_create_mode(void)
{
	static const uint8_t domain = 3;
	static const uint8_t level = 2;
	static const struct log_source_const_data source = {
		.name = "test name"
	};
	union log_msg2_generic *msg;
	size_t len;

	test_init();

	int mode;

	timestamp++;
	Z_LOG_MSG2_CREATE(1, mode, 0, domain, &source, level, NULL, 0,
			"test str");

	printk("mode: %d\n", mode);
	zassert_equal(mode, 1, "Unexpected creation mode");
	msg = z_log_msg2_claim();
	len = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg);

	zassert_equal(len, sizeof(struct log_msg2_hdr) +
			 sizeof(const char *),
			"Unexpected message length");
	basic_validate(&msg->log, &source, domain, level,
			TEST_TIMESTAMP_INIT_VALUE + 1, NULL, 0, "test str");
	z_log_msg2_free(msg);

	Z_LOG_MSG2_CREATE(0, mode, 0, domain, &source, level, NULL, 0,
			"test str");

	zassert_equal(mode, 3, "Unexpected creation mode");
	msg = z_log_msg2_claim();
	len = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg);
	zassert_equal(len, sizeof(struct log_msg2_hdr) +
			 sizeof(const char *),
			"Unexpected message length");
	basic_validate(&msg->log, &source, domain, level,
			TEST_TIMESTAMP_INIT_VALUE + 1, NULL, 0, "test str");
	z_log_msg2_free(msg);

	/* If data is present then message is created from stack, even though
	 * _from_stack is 0. */
	uint8_t data[] = {1, 2, 3};

	Z_LOG_MSG2_CREATE(1, mode, 0, domain, &source, level,
			  data, sizeof(data), "test str");

	zassert_equal(mode, 3, "Unexpected creation mode");
	msg = z_log_msg2_claim();
	len = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg);
	zassert_equal(len, sizeof(struct log_msg2_hdr) +
			 sizeof(const char *) + sizeof(data),
			"Unexpected message length");
	basic_validate(&msg->log, &source, domain, level,
			TEST_TIMESTAMP_INIT_VALUE + 1, data, sizeof(data),
			"test str");
	z_log_msg2_free(msg);

	static const char *prefix = "prefix";
	Z_LOG_MSG2_CREATE(1 /*try 0cpy*/, mode,
			  1 /* accept one string pointer*/,
			  domain, &source, level,
			  NULL, 0, "%s test str", prefix);

	zassert_equal(mode, 1, "Unexpected creation mode");
	msg = z_log_msg2_claim();
	len = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg);
	zassert_equal(len, sizeof(struct log_msg2_hdr) +
			 sizeof(const char *) + 1 + sizeof(char *),
			"Unexpected message length");
	basic_validate(&msg->log, &source, domain, level,
			TEST_TIMESTAMP_INIT_VALUE + 1, NULL, 0,
			"prefix test str");
	z_log_msg2_free(msg);

	Z_LOG_MSG2_CREATE(1 /*try 0cpy*/, mode,
			  1 /* accept one string pointer*/,
			  domain, &source, level,
			  NULL, 0, "%s test str %s", prefix, "sufix");

	zassert_equal(mode, 2, "Unexpected creation mode");
	msg = z_log_msg2_claim();
	len = log_msg2_generic_get_len((union mpsc_pbuf_generic *)msg);
	zassert_equal(len, sizeof(struct log_msg2_hdr) +
			 sizeof(const char *) + 1 + sizeof(char *) +
			 1 + sizeof(char *),
			"Unexpected message length");
	basic_validate(&msg->log, &source, domain, level,
			TEST_TIMESTAMP_INIT_VALUE + 1, NULL, 0,
			"prefix test str sufix");
	z_log_msg2_free(msg);
}

/*test case main entry*/
void test_main(void)
{
	ztest_test_suite(test_log_msg2,
		ztest_unit_test(test_log_msg2),
		ztest_unit_test(test_msg_create_mode)
		);
	ztest_run_test_suite(test_log_msg2);
}

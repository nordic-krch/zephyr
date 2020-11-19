/*
 * Copyright (c) 2018 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Test log message
 */

#include <logging/log_msg.h>

#include <tc_util.h>
#include <stdbool.h>
#include <zephyr.h>
#include <ztest.h>

extern struct k_mem_slab log_msg_pool;
static const char my_string[] = "test_string";
void test_log_std_msg(void)
{
	zassert_equal(LOG_MSG_NARGS_SINGLE_CHUNK,
		      IS_ENABLED(CONFIG_64BIT) ? 4 : 3,
		      "test assumes following setting");

	uint32_t used_slabs = k_mem_slab_num_used_get(&log_msg_pool);
	log_arg_t args[] = {1, 2, 3, 4, 5, 6};
	struct log_msg *msg;

	/* Test for expected buffer usage based on number of arguments */
	for (int i = 0; i <= 6; i++) {
		switch (i) {
		case 0:
			msg = log_msg_create_0(my_string);
			break;
		case 1:
			msg = log_msg_create_1(my_string, 1);
			break;
		case 2:
			msg = log_msg_create_2(my_string, 1, 2);
			break;
		case 3:
			msg = log_msg_create_3(my_string, 1, 2, 3);
			break;
		default:
			msg = log_msg_create_n(my_string, args, i);
			break;
		}

		used_slabs += (i > LOG_MSG_NARGS_SINGLE_CHUNK) ? 2 : 1;
		zassert_equal(used_slabs,
			      k_mem_slab_num_used_get(&log_msg_pool),
			      "Expected mem slab allocation.");

		log_msg_put(msg);

		used_slabs -= (i > LOG_MSG_NARGS_SINGLE_CHUNK) ? 2 : 1;
		zassert_equal(used_slabs,
			      k_mem_slab_num_used_get(&log_msg_pool),
			      "Expected mem slab allocation.");
	}
}

void test_log_hexdump_msg(void)
{

	uint32_t used_slabs = k_mem_slab_num_used_get(&log_msg_pool);
	struct log_msg *msg;
	uint8_t data[128];

	for (int i = 0; i < sizeof(data); i++) {
		data[i] = i;
	}

	/* allocation of buffer that fits in single buffer */
	msg = log_msg_hexdump_create("test", data,
				     LOG_MSG_HEXDUMP_BYTES_SINGLE_CHUNK - 4);

	zassert_equal((used_slabs + 1),
		      k_mem_slab_num_used_get(&log_msg_pool),
		      "Expected mem slab allocation.");
	used_slabs++;

	log_msg_put(msg);

	zassert_equal((used_slabs - 1),
		      k_mem_slab_num_used_get(&log_msg_pool),
		      "Expected mem slab allocation.");
	used_slabs--;

	/* allocation of buffer that fits in single buffer */
	msg = log_msg_hexdump_create("test", data,
				     LOG_MSG_HEXDUMP_BYTES_SINGLE_CHUNK);

	zassert_equal((used_slabs + 1),
		      k_mem_slab_num_used_get(&log_msg_pool),
		      "Expected mem slab allocation.");
	used_slabs++;

	log_msg_put(msg);

	zassert_equal((used_slabs - 1),
		      k_mem_slab_num_used_get(&log_msg_pool),
		      "Expected mem slab allocation.");
	used_slabs--;

	/* allocation of buffer that fits in 2 buffers */
	msg = log_msg_hexdump_create("test", data,
				     LOG_MSG_HEXDUMP_BYTES_SINGLE_CHUNK + 1);

	zassert_equal((used_slabs + 2U),
		      k_mem_slab_num_used_get(&log_msg_pool),
		      "Expected mem slab allocation.");
	used_slabs += 2U;

	log_msg_put(msg);

	zassert_equal((used_slabs - 2U),
		      k_mem_slab_num_used_get(&log_msg_pool),
		      "Expected mem slab allocation.");
	used_slabs -= 2U;

	/* allocation of buffer that fits in 3 buffers */
	msg = log_msg_hexdump_create("test", data,
				     LOG_MSG_HEXDUMP_BYTES_SINGLE_CHUNK +
				     HEXDUMP_BYTES_CONT_MSG + 1);

	zassert_equal((used_slabs + 3U),
		      k_mem_slab_num_used_get(&log_msg_pool),
		      "Expected mem slab allocation.");
	used_slabs += 3U;

	log_msg_put(msg);

	zassert_equal((used_slabs - 3U),
		      k_mem_slab_num_used_get(&log_msg_pool),
		      "Expected mem slab allocation.");
	used_slabs -= 3U;
}

void test_log_hexdump_data_get_single_chunk(void)
{
	struct log_msg *msg;
	uint8_t data[128];
	uint8_t read_data[128];
	size_t offset;
	uint32_t wr_length;
	size_t rd_length;
	size_t rd_req_length;

	for (int i = 0; i < sizeof(data); i++) {
		data[i] = i;
	}

	/* allocation of buffer that fits in single buffer */
	wr_length = LOG_MSG_HEXDUMP_BYTES_SINGLE_CHUNK - 4;
	msg = log_msg_hexdump_create("test", data, wr_length);

	offset = 0;
	rd_length = wr_length - 1;
	rd_req_length = rd_length;

	log_msg_hexdump_data_get(msg,
				 read_data,
				 &rd_length,
				 offset);

	zassert_equal(rd_length,
		      rd_req_length,
		      "Expected to read requested amount of data\n");

	zassert_true(memcmp(&data[offset],
		     read_data,
		     rd_length) == 0,
			"Expected data.\n");

	/* Attempt to read more data than present in the message */
	offset = 0;
	rd_length = wr_length + 1; /* requesting read more data */
	rd_req_length = rd_length;

	log_msg_hexdump_data_get(msg,
				 read_data,
				 &rd_length,
				 offset);
	zassert_equal(rd_length,
		      wr_length,
		      "Expected to read requested amount of data\n");

	zassert_true(memcmp(&data[offset],
		     read_data,
		     rd_length) == 0,
		     "Expected data.\n");

	/* Attempt to read with non zero offset, requested length fits in the
	 * buffer.
	 */
	offset = 4;
	rd_length = 1; /* requesting read more data */
	rd_req_length = rd_length;

	log_msg_hexdump_data_get(msg,
				 read_data,
				 &rd_length,
				 offset);

	zassert_equal(rd_length,
		      rd_req_length,
		      "Expected to read requested amount of data\n");

	zassert_true(memcmp(&data[offset],
		     read_data,
		     rd_length) == 0,
		     "Expected data.\n");

	/* Attempt to read with non zero offset, requested length DOES NOT fit
	 * in the buffer.
	 */
	offset = 4;
	rd_length = LOG_MSG_HEXDUMP_BYTES_SINGLE_CHUNK;
	rd_req_length = rd_length;

	log_msg_hexdump_data_get(msg,
				 read_data,
				 &rd_length,
				 offset);

	zassert_equal(rd_length,
		      wr_length - offset,
		      "Expected to read requested amount of data\n");

	zassert_true(memcmp(&data[offset],
		     read_data,
		     rd_length) == 0,
		     "Expected data.\n");

	log_msg_put(msg);
}

void test_log_hexdump_data_get_two_chunks(void)
{
	struct log_msg *msg;
	uint8_t data[128];
	uint8_t read_data[128];
	size_t offset;
	uint32_t wr_length;
	size_t rd_length;
	size_t rd_req_length;

	for (int i = 0; i < sizeof(data); i++) {
		data[i] = i;
	}

	/* allocation of buffer that fits in two chunks. */
	wr_length = LOG_MSG_HEXDUMP_BYTES_SINGLE_CHUNK;
	msg = log_msg_hexdump_create("test", data, wr_length);

	/* Read whole data from offset = 0*/
	offset = 0;
	rd_length = wr_length;
	rd_req_length = rd_length;

	log_msg_hexdump_data_get(msg,
				 read_data,
				 &rd_length,
				 offset);

	zassert_equal(rd_length,
		      rd_req_length,
		      "Expected to read requested amount of data\n");

	zassert_true(memcmp(&data[offset],
		     read_data,
		     rd_length) == 0,
		     "Expected data.\n");

	/* Read data from first and second chunk. */
	offset = 1;
	rd_length = wr_length - 2;
	rd_req_length = rd_length;

	log_msg_hexdump_data_get(msg,
				 read_data,
				 &rd_length,
				 offset);

	zassert_equal(rd_length,
		      rd_req_length,
		      "Expected to read requested amount of data\n");

	zassert_true(memcmp(&data[offset],
		     read_data,
		     rd_length) == 0,
		     "Expected data.\n");

	/* Read data from second chunk. */
	offset = wr_length - 2;
	rd_length = 1;
	rd_req_length = rd_length;

	log_msg_hexdump_data_get(msg,
				 read_data,
				 &rd_length,
				 offset);

	zassert_equal(rd_length,
		      rd_req_length,
		      "Expected to read requested amount of data\n");

	zassert_true(memcmp(&data[offset], read_data, rd_length) == 0,
		     "Expected data.\n");

	/* Read more than available */
	offset = wr_length - 2;
	rd_length = wr_length;
	rd_req_length = rd_length;

	log_msg_hexdump_data_get(msg,
				 read_data,
				 &rd_length,
				 offset);

	zassert_equal(rd_length,
		      wr_length - offset,
		      "Expected to read requested amount of data\n");

	zassert_true(memcmp(&data[offset], read_data, rd_length) == 0,
		     "Expected data.\n");

	log_msg_put(msg);
}

void test_log_hexdump_data_get_multiple_chunks(void)
{
	struct log_msg *msg;
	uint8_t data[128];
	uint8_t read_data[128];
	size_t offset;
	uint32_t wr_length;
	size_t rd_length;
	size_t rd_req_length;

	for (int i = 0; i < sizeof(data); i++) {
		data[i] = i;
	}

	/* allocation of buffer that fits in two chunks. */
	wr_length = 40U;
	msg = log_msg_hexdump_create("test", data, wr_length);

	/* Read whole data from offset = 0*/
	offset = 0;
	rd_length = wr_length;
	rd_req_length = rd_length;

	log_msg_hexdump_data_get(msg,
				 read_data,
				 &rd_length,
				 offset);


	zassert_equal(rd_length,
		      rd_req_length,
		      "Expected to read requested amount of data\n");

	zassert_true(memcmp(&data[offset], read_data, rd_length) == 0,
		     "Expected data.\n");

	/* Read data with offset starting from second chunk. */
	offset = LOG_MSG_HEXDUMP_BYTES_HEAD_CHUNK + 4;
	rd_length = wr_length - offset - 2;
	rd_req_length = rd_length;

	log_msg_hexdump_data_get(msg,
				 read_data,
				 &rd_length,
				 offset);

	zassert_equal(rd_length,
		      rd_req_length,
		      "Expected to read requested amount of data\n");

	zassert_true(memcmp(&data[offset], read_data, rd_length) == 0,
		     "Expected data.\n");

	/* Read data from second chunk with saturation. */
	offset = LOG_MSG_HEXDUMP_BYTES_HEAD_CHUNK + 4;
	rd_length = wr_length - offset + 1;
	rd_req_length = rd_length;

	log_msg_hexdump_data_get(msg,
				 read_data,
				 &rd_length,
				 offset);

	zassert_equal(rd_length,
		      wr_length - offset,
		      "Expected to read requested amount of data\n");

	zassert_true(memcmp(&data[offset], read_data, rd_length) == 0,
		     "Expected data.\n");


	/* Read beyond message */
	offset = wr_length + 1;
	rd_length = 1;
	rd_req_length = rd_length;

	log_msg_hexdump_data_get(msg,
				 read_data,
				 &rd_length,
				 offset);

	zassert_equal(rd_length,
		      0,
		      "Expected to read requested amount of data\n");

	log_msg_put(msg);
}

void test_z_log_arg_wlen(void)
{
	float f;
	double d;
	uint8_t b;
	uint32_t w;

	static const uint32_t len1 = Z_LOG_GET_ARG_WLEN(f);

	zassert_equal(len1, ceiling_fraction(sizeof(double), sizeof(uint32_t)),
		       	NULL);

	static const uint32_t len2 = Z_LOG_GET_ARG_WLEN(d);

	zassert_equal(len2, ceiling_fraction(sizeof(double), sizeof(uint32_t)),
			NULL);

	static const uint32_t len3 = Z_LOG_GET_ARG_WLEN(b);

	zassert_equal(len3, 1, NULL);

	static const uint32_t len4 = Z_LOG_GET_ARG_WLEN(d, f, w, b);

	zassert_equal(len4, ceiling_fraction(2 * sizeof(double) +
				2 * sizeof(uint32_t), sizeof(uint32_t)),
			NULL);
}

void test_z_log_has_pchar(void)
{
	char *x;
	const char *s = "abc";
	char c;
	int i;

	static const uint32_t len1 = Z_LOG_HAS_PCHAR_ARGS(c, i);

	zassert_false(len1, NULL);

	static const uint32_t len2 = Z_LOG_HAS_PCHAR_ARGS(&c, i);

	zassert_true(len2, NULL);

	static const uint32_t len3 = Z_LOG_HAS_PCHAR_ARGS(x);

	zassert_true(len3, NULL);

	static const uint32_t len4 = Z_LOG_HAS_PCHAR_ARGS(s);

	zassert_true(len4, NULL);
}

void test_z_log_get_pchars(void)
{
	static const char *s1 = "test";
	int i = 0;
	char c = 's';

	const char *x0[] = { Z_LOG_GET_PCHARS(s1) };

	zassert_equal(ARRAY_SIZE(x0), 1, NULL);
	zassert_equal(x0[0], s1, NULL);

	const char *x1[] = { Z_LOG_GET_PCHARS(&i, c, s1, NULL) };

	zassert_equal(ARRAY_SIZE(x1), 4, NULL);
	zassert_equal(x1[0], NULL, NULL);
	zassert_equal(x1[1], NULL, NULL);
	zassert_equal(x1[2], s1, NULL);
	zassert_equal(x1[3], NULL, NULL);
}

void test_z_log_strs_len_get(void)
{
	char i = 0;
	const char *ptrs[] = { "abc", NULL, NULL, &i, "aaaa" };
	uint32_t mask = 0x00000011;
	uint32_t len = z_log_strs_len_get(ptrs, mask);

	zassert_equal(len, 9, NULL);
}
void test_Z_LOG_MSG_WLEN(void)
{
#if 0
	uint32_t total_wlen;
	uint32_t args_wlen;
	uint32_t strs_len;
	uint64_t lw;
	float f;
	char *str = "test";

	Z_LOG_MSG_WLEN(total_wlen, args_wlen, strs_len, 0, "plain");
	zassert_equal(total_wlen, 0, NULL);
	zassert_equal(args_wlen, 0, NULL);
	zassert_equal(strs_len, 0, NULL);

	Z_LOG_MSG_WLEN(total_wlen, args_wlen, strs_len, 13, "plain");
	zassert_equal(total_wlen, 4 /* 13 roundup*/, NULL);
	zassert_equal(args_wlen, 0, NULL);
	zassert_equal(strs_len, 0, NULL);

	Z_LOG_MSG_WLEN(total_wlen, args_wlen, strs_len,
			0,"plain %d %lld %f", 10, lw, f);
	zassert_equal(total_wlen, 5, NULL);
	zassert_equal(args_wlen, 5, NULL);
	zassert_equal(strs_len, 0, NULL);

	Z_LOG_MSG_WLEN(total_wlen, args_wlen, strs_len,
			9, "plain %s %p %s","abc", str, str);
	zassert_equal(total_wlen, 8, NULL);
	zassert_equal(args_wlen, 3, NULL);
	zassert_equal(strs_len, 9, NULL);
#endif
}

static uint32_t exp_alloc_words;
static int alloc_cnt;
static uint32_t alloc_buf[100];
static const void *exp_src;
static const char *exp_str;
static uint32_t exp_args[100];
static uint32_t exp_args_wlen;
static int finalize_cnt;
static int dfinalize_cnt;
static void *exp_data;
static uint32_t exp_dlen;

static uint32_t *t_alloc(uint32_t words)
{
	memset(alloc_buf, 0XAA, sizeof(alloc_buf));
	printk("words: %d exp: %d\n", words, exp_alloc_words);
	zassert_equal(words, exp_alloc_words, NULL);
	alloc_cnt++;

	return alloc_buf;
}

static void test_MSG_SIMPLE_basic_finalize(uint32_t *buf, const void *src,
					   const char *str, uint32_t *args)
{
	zassert_equal(buf, alloc_buf, NULL);
	zassert_equal(src, exp_src, NULL);
	zassert_equal(str, exp_str, NULL);
	zassert_equal(memcmp(args, exp_args, sizeof(uint32_t)*exp_args_wlen),
			0, NULL);
}

static void t_finalize(uint32_t *buf, const void *src, const char *str,
			uint32_t *args)
{
	test_MSG_SIMPLE_basic_finalize(buf, src, str, args);
	finalize_cnt++;
}

static void t_dfinalize(uint32_t *buf, const void *src, const char *str,
			uint32_t *args, void *data)
{
	test_MSG_SIMPLE_basic_finalize(buf, src, str, args);
	zassert_equal(data, exp_data, NULL);
	dfinalize_cnt++;
}

#define T_MSG_SIMPLE(...) Z_LOG_MSG_SIMPLE(t_alloc, t_finalize, \
						t_dfinalize, __VA_ARGS__)

static void test_MSG_SIMPLE_prepare(uint32_t domain_id, const void *src,
				uint32_t level, const char *str,
				uint32_t args_wlen, void *data, uint32_t dlen)
{
	alloc_cnt = 0;
	finalize_cnt = 0;
	dfinalize_cnt = 0;
	exp_alloc_words = args_wlen +
		ceiling_fraction(dlen, sizeof(uint32_t)) +
		ceiling_fraction(sizeof(struct log_msg_hdr2), sizeof(uint32_t));
	exp_src = src;
	exp_str = str;
	exp_args_wlen = args_wlen;
	exp_data = data;
	exp_dlen = dlen;
}

static void test_MSG_SIMPLE_validate(uint32_t domain_id, const void *src,
				uint32_t level, void *data, uint32_t dlen,
				uint32_t args_wlen)
{
	struct log_msg_hdr2 *hdr = (struct log_msg_hdr2 *)alloc_buf;

	zassert_equal(hdr->desc.domain, domain_id, NULL);
	zassert_equal(hdr->desc.level, level, NULL);
	zassert_equal(hdr->desc.args_wlen, args_wlen, NULL);
	zassert_equal(hdr->desc.data_len, dlen, NULL);
	zassert_equal(hdr->desc.strs_len, 0, NULL);

	zassert_equal(alloc_cnt, 1, NULL);
	if (dlen == 0) {
		zassert_equal(finalize_cnt, 1, NULL);
		zassert_equal(dfinalize_cnt, 0, NULL);
	} else {
		zassert_equal(dfinalize_cnt, 1, NULL);
		zassert_equal(finalize_cnt, 0, NULL);
	}
}

void test_MSG_SIMPLE(void)
{
	static const uint8_t domain = 3;
	static const uint8_t level = 2;
	static const uint8_t source = 0xfa;
	static const char *str = "plain string";

	test_MSG_SIMPLE_prepare(domain, &source, level, str, 0, NULL, 0);
	T_MSG_SIMPLE(domain, &source, level, NULL, 0, str);
	test_MSG_SIMPLE_validate(domain, &source, level, NULL, 0, 0);

	uint32_t args[] = {11, 12};
	static const char *str1 = "%d %d";

	exp_args[0] = args[0];
	exp_args[1] = args[1];

	test_MSG_SIMPLE_prepare(domain, &source, level, str1, 2, NULL, 0);
	T_MSG_SIMPLE(domain, &source, level, NULL, 0, str1, args[0], args[1]);
	test_MSG_SIMPLE_validate(domain, &source, level, NULL, 0, 2);

//	T_MSG_SIMPLE(domain, &source, level, NULL, 0, "a", &domain);
//	T_MSG_SIMPLE(domain, &source, level, NULL, 0, "b", level);
}

/*test case main entry*/
void test_main(void)
{
	ztest_test_suite(test_log_message,
		ztest_unit_test(test_log_std_msg),
		ztest_unit_test(test_log_hexdump_msg),
		ztest_unit_test(test_log_hexdump_data_get_single_chunk),
		ztest_unit_test(test_log_hexdump_data_get_two_chunks),
		ztest_unit_test(test_log_hexdump_data_get_multiple_chunks),
		ztest_unit_test(test_z_log_arg_wlen),
		ztest_unit_test(test_z_log_has_pchar),
		ztest_unit_test(test_z_log_get_pchars),
		ztest_unit_test(test_z_log_strs_len_get),
		ztest_unit_test(test_MSG_SIMPLE),
		ztest_unit_test(test_Z_LOG_MSG_WLEN)
		);
	ztest_run_test_suite(test_log_message);
}

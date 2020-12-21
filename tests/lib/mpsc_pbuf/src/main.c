/*
 * Copyright (c) 2018 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Test log message
 */

#include <sys/mpsc_pbuf.h>

#include <tc_util.h>
#include <stdbool.h>
#include <zephyr.h>
#include <ztest.h>

#define PUT_EXT_LEN \
	((sizeof(union mpsc_pbuf_generic) + sizeof (void *)) / sizeof (uint32_t))

struct test_data {
	MPSC_PBUF_HDR;
	uint32_t len: 16;
	uint32_t data: 32 - MPSC_PBUF_HDR_BITS - 16;
};

struct test_data_ext {
	struct test_data hdr;
	void *data;
};

struct test_data_var {
	struct test_data hdr;
	uint32_t data[];
};

union test_item {
	struct test_data data;
	struct test_data_ext data_ext;
	union mpsc_pbuf_generic item;
};

static uint32_t get_len(union mpsc_pbuf_generic *item)
{
	union test_item *t_item = (union test_item *)item;

	return t_item->data.len;
}

static uint32_t drop_cnt;
static uint32_t exp_dropped_data[10];
static uint32_t exp_dropped_len[10];

static void drop(struct mpsc_pbuf_buffer *buffer, union mpsc_pbuf_generic *item)
{
	struct test_data_var *packet = (struct test_data_var *)item;

	zassert_equal(packet->hdr.data, exp_dropped_data[drop_cnt], NULL);
	zassert_equal(packet->hdr.len, exp_dropped_len[drop_cnt], NULL);
	for (int i = 0; i < exp_dropped_len[drop_cnt] - 1; i++) {
		zassert_equal(packet->data[i], exp_dropped_data[drop_cnt] + i,
				NULL);
	}

	drop_cnt++;
}

static uint32_t buf32[512];

static struct mpsc_pbuf_buffer_config cfg = {
	.buf = buf32,
	.notify_drop = drop,
	.get_len = get_len
};

static void init(struct mpsc_pbuf_buffer *buffer, bool overflow, bool pow2)
{
	drop_cnt = 0;
	cfg.flags = overflow ? MPSC_PBUF_MODE_OVERFLOW : 0;
	cfg.size = ARRAY_SIZE(buf32) - (pow2 ? 0 : 1);
	mpsc_pbuf_init(buffer, &cfg);

#if CONFIG_SOC_SERIES_NRF52X
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	DWT->CYCCNT = 0;
#endif
}

static inline uint32_t get_cyc(void)
{
#if CONFIG_SOC_SERIES_NRF52X
	return DWT->CYCCNT;
#else
	return k_cycle_get_32();
#endif
}

void _test_item_put_circulate(bool overflow, bool pow2)
{
	struct mpsc_pbuf_buffer buffer;

	init(&buffer, overflow, pow2);

	int repeat = buffer.size*2;
	union test_item test_1word = {.data = {.valid = 1, .len = 1 }};

	for (int i = 0; i < repeat; i++) {
		union test_item *t;

		test_1word.data.data = i;
		mpsc_pbuf_put_word(&buffer, test_1word.item);

		t = (union test_item *)mpsc_pbuf_claim(&buffer);
		zassert_true(t, NULL);
		zassert_equal(t->data.data, i, NULL);
		mpsc_pbuf_free(&buffer, &t->item);

	}

	zassert_equal(mpsc_pbuf_claim(&buffer), NULL, NULL);
}

void test_item_put_circulate(void)
{
	_test_item_put_circulate(true, true);
	_test_item_put_circulate(true, false);
	_test_item_put_circulate(false, true);
	_test_item_put_circulate(false, false);
}

void test_item_put_saturate(void)
{
	struct mpsc_pbuf_buffer buffer;

	init(&buffer, false, true);

	int repeat = buffer.size;
	union test_item test_1word = {.data = {.valid = 1, .len = 1 }};
	union test_item *t;

	for (int i = 0; i < repeat/2; i++) {
		test_1word.data.data = i;
		mpsc_pbuf_put_word(&buffer, test_1word.item);

		t = (union test_item *)mpsc_pbuf_claim(&buffer);
		zassert_true(t, NULL);
		zassert_equal(t->data.data, i, NULL);
		mpsc_pbuf_free(&buffer, &t->item);
	}

	for (int i = 0; i < repeat; i++) {
		test_1word.data.data = i;
		mpsc_pbuf_put_word(&buffer, test_1word.item);
	}

	for (int i = 0; i < (repeat-1); i++) {
		t = (union test_item *)mpsc_pbuf_claim(&buffer);
		zassert_true(t, NULL);
		zassert_equal(t->data.data, i, NULL);
		mpsc_pbuf_free(&buffer, &t->item);
	}

	zassert_equal(mpsc_pbuf_claim(&buffer), NULL, NULL);
}

void print_config(const char *functionality, bool overflow, bool pow2)
{
	PRINT("\nBenchmarking %s:\n", functionality);
	PRINT("\t- %s\n", pow2 ? "pow2 size" : "uneven size");
	PRINT("\t- %s\n", overflow ? "overwrite when full" : "saturate");
}

void _test_benchmark_item_put(bool overflow, bool pow2)
{
	struct mpsc_pbuf_buffer buffer;

	init(&buffer, overflow, pow2);

	int repeat = buffer.size - 1;
	union test_item test_1word = {.data = {.valid = 1, .len = 1 }};
	uint32_t t = get_cyc();

	for (int i = 0; i < repeat; i++) {
		test_1word.data.data = i;
		mpsc_pbuf_put_word(&buffer, test_1word.item);
	}

	t = get_cyc() - t;
	print_config("single word put", overflow, pow2);
	PRINT("single word put time: %d cycles\n", t/repeat);

	t = get_cyc();
	for (int i = 0; i < repeat; i++) {
		union test_item *t;

		t = (union test_item *)mpsc_pbuf_claim(&buffer);
		zassert_true(t, NULL);
		zassert_equal(t->data.data, i, NULL);
		mpsc_pbuf_free(&buffer, &t->item);
	}

	t = get_cyc() - t;
	PRINT("single word item claim,free: %d cycles\n", t/repeat);

	zassert_equal(mpsc_pbuf_claim(&buffer), NULL, NULL);
}

void test_benchmark_item_put(void)
{
	_test_benchmark_item_put(true, true);
	_test_benchmark_item_put(true, false);
	_test_benchmark_item_put(false, true);
	_test_benchmark_item_put(false, false);
}

void test_item_put_ext_no_overflow(void)
{
	struct mpsc_pbuf_buffer buffer;

	init(&buffer, false, true);

	int repeat = buffer.size * 2;
	union test_item test_ext_item = {
		.data = {
			.valid = 1,
			.len = PUT_EXT_LEN
		}
	};
	void *data;

	for (int i = 0; i < repeat; i++) {
		union test_item *t;

		data = (void *)i;
		test_ext_item.data.data = i;
		mpsc_pbuf_put_word_ext(&buffer, test_ext_item.item, data);

		t = (union test_item *)mpsc_pbuf_claim(&buffer);
		zassert_true(t, NULL);
		zassert_equal(t->data_ext.data, (void *)i, NULL);
		zassert_equal(t->data_ext.hdr.data, i, NULL);
		mpsc_pbuf_free(&buffer, &t->item);
	}

	zassert_equal(mpsc_pbuf_claim(&buffer), NULL, NULL);
}

void test_item_put_ext_saturate(void)
{
	struct mpsc_pbuf_buffer buffer;

	init(&buffer, false, true);

	int repeat = buffer.size / PUT_EXT_LEN ;
	union test_item test_ext_item = {
		.data = {
			.valid = 1,
			.len = PUT_EXT_LEN
		}
	};
	void *data;
	union test_item *t;

	for (int i = 0; i < repeat/2; i++) {
		test_ext_item.data.data = i;
		data = (void *)i;
		mpsc_pbuf_put_word_ext(&buffer, test_ext_item.item, data);

		t = (union test_item *)mpsc_pbuf_claim(&buffer);
		zassert_true(t, NULL);
		zassert_equal(t->data.data, i, NULL);
		mpsc_pbuf_free(&buffer, &t->item);
	}

	for (int i = 0; i < repeat; i++) {
		test_ext_item.data.data = i;
		data = (void *)i;
		mpsc_pbuf_put_word_ext(&buffer, test_ext_item.item, data);
	}

	for (int i = 0; i < (repeat-1); i++) {
		t = (union test_item *)mpsc_pbuf_claim(&buffer);
		zassert_true(t, NULL);
		zassert_equal(t->data_ext.data, (void *)i, NULL);
		zassert_equal(t->data_ext.hdr.data, i, NULL);
		mpsc_pbuf_free(&buffer, &t->item);
	}

	zassert_equal(mpsc_pbuf_claim(&buffer), NULL, NULL);
}

void test_benchmark_item_put_ext(void)
{
	struct mpsc_pbuf_buffer buffer;

	init(&buffer, false, true);

	int repeat = (buffer.size - 1) / PUT_EXT_LEN;
	union test_item test_ext_item = {
		.data = {
			.valid = 1,
			.len = PUT_EXT_LEN
		}
	};
	void *data = NULL;
	uint32_t t = get_cyc();

	for (int i = 0; i < repeat; i++) {
		test_ext_item.data.data = i;
		mpsc_pbuf_put_word_ext(&buffer, test_ext_item.item, data);
	}

	t = get_cyc() - t;
	PRINT("put_ext time: %d cycles\n", t/repeat);

	t = get_cyc();
	for (int i = 0; i < repeat; i++) {
		union test_item *t;

		t = (union test_item *)mpsc_pbuf_claim(&buffer);
		zassert_true(t, NULL);
		zassert_equal(t->data.data, i, NULL);
		mpsc_pbuf_free(&buffer, &t->item);
	}

	t = get_cyc() - t;
	PRINT("ext item claim,free: %d cycles\n", t/repeat);

	zassert_equal(mpsc_pbuf_claim(&buffer), NULL, NULL);
}


void test_benchmark_item_put_data(void)
{
	struct mpsc_pbuf_buffer buffer;

	init(&buffer, false, true);

	int repeat = (buffer.size - 1) / PUT_EXT_LEN;
	union test_item test_ext_item = {
		.data_ext = {
			.hdr = {
				.valid = 1,
				.len = PUT_EXT_LEN
			},
			.data = NULL
		}
	};
	uint32_t t = get_cyc();

	for (int i = 0; i < repeat; i++) {
		test_ext_item.data_ext.hdr.data = i;
		test_ext_item.data_ext.data = (void *)i;
		mpsc_pbuf_put_data(&buffer, (uint32_t *)&test_ext_item,
				    PUT_EXT_LEN);
	}

	t = get_cyc() - t;
	PRINT("put_ext time: %d cycles\n", t/repeat);

	t = get_cyc();
	for (int i = 0; i < repeat; i++) {
		union test_item *t;

		t = (union test_item *)mpsc_pbuf_claim(&buffer);
		zassert_true(t, NULL);
		zassert_equal(t->data.data, i, NULL);
		mpsc_pbuf_free(&buffer, &t->item);
	}

	t = get_cyc() - t;
	PRINT("ext item claim,free: %d cycles\n", t/repeat);

	zassert_equal(mpsc_pbuf_claim(&buffer), NULL, NULL);
}

void test_item_alloc_commit(void)
{
	struct mpsc_pbuf_buffer buffer;

	init(&buffer, false, true);

	struct test_data_var *packet;
	uint32_t len = 5;
	int repeat = 1024;

	for (int i = 0; i < repeat; i++) {
		packet = (struct test_data_var *)mpsc_pbuf_alloc(&buffer, len,
								 K_NO_WAIT);
		packet->hdr.len = len;
		for (int j = 0; j < len - 1; j++) {
			packet->data[j] = i + j;
		}

		mpsc_pbuf_commit(&buffer, (union mpsc_pbuf_generic *)packet);

		packet = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
		zassert_true(packet, NULL);
		zassert_equal(packet->hdr.len, len, NULL);

		for (int j = 0; j < len - 1; j++) {
			zassert_equal(packet->data[j], i + j, NULL);
		}

		mpsc_pbuf_free(&buffer, (union mpsc_pbuf_generic *)packet);
	}
}

static uint32_t saturate_buffer_uneven(struct mpsc_pbuf_buffer *buffer,
					uint32_t len)
{
	struct test_data_var *packet;
	uint32_t uneven = 5;
	uint32_t cnt = 0;
	int repeat =
		uneven - 1 + ((buffer->size - (uneven * len)) / len);

	/* Put some data to include wrapping */
	for (int i = 0; i < uneven; i++) {
		packet = (struct test_data_var *)mpsc_pbuf_alloc(buffer, len,
								 K_NO_WAIT);
		packet->hdr.len = len;
		mpsc_pbuf_commit(buffer, (union mpsc_pbuf_generic *)packet);

		packet = (struct test_data_var *)mpsc_pbuf_claim(buffer);
		zassert_true(packet, NULL);
		mpsc_pbuf_free(buffer, (union mpsc_pbuf_generic *)packet);
	}

	for (int i = 0; i < repeat; i++) {
		packet = (struct test_data_var *)mpsc_pbuf_alloc(buffer, len,
								 K_NO_WAIT);
		zassert_true(packet, NULL);
		packet->hdr.len = len;
		packet->hdr.data = i;
		for (int j = 0; j < len - 1; j++) {
			packet->data[j] = i + j;
		}

		mpsc_pbuf_commit(buffer, (union mpsc_pbuf_generic *)packet);
		cnt++;
	}

	return cnt;
}

void test_item_alloc_commit_saturate(void)
{
	struct mpsc_pbuf_buffer buffer;

	init(&buffer, false, true);

	saturate_buffer_uneven(&buffer, 5);

	struct test_data_var *packet;
	uint32_t len = 5;

	packet = (struct test_data_var *)mpsc_pbuf_alloc(&buffer, len,
							 K_NO_WAIT);
	zassert_equal(packet, NULL, NULL);

	/* Get one packet from the buffer. */
	packet = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
	zassert_true(packet, NULL);
	mpsc_pbuf_free(&buffer, (union mpsc_pbuf_generic *)packet);

	/* and try to allocate one more time, this time with success. */
	packet = (struct test_data_var *)mpsc_pbuf_alloc(&buffer, len,
							 K_NO_WAIT);
	zassert_true(packet, NULL);
}

void test_item_alloc_preemption(void)
{
	struct mpsc_pbuf_buffer buffer;

	init(&buffer, false, true);

	struct test_data_var *p0;
	struct test_data_var *p1;
	struct test_data_var *p;

	p0 = (struct test_data_var *)mpsc_pbuf_alloc(&buffer, 10, K_NO_WAIT);
	zassert_true(p0, NULL);
	p0->hdr.len = 10;

	/* Check that no packet is yet available */
	p = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
	zassert_equal(p, NULL, NULL);

	p1 = (struct test_data_var *)mpsc_pbuf_alloc(&buffer, 20, K_NO_WAIT);
	zassert_true(p1, NULL);
	p1->hdr.len = 20;

	/* Commit p1, p0 is still not commited, there should be no packets
	 * available for reading.
	 */
	mpsc_pbuf_commit(&buffer, (union mpsc_pbuf_generic *)p1);

	/* Check that no packet is yet available */
	p = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
	zassert_equal(p, NULL, NULL);

	mpsc_pbuf_commit(&buffer, (union mpsc_pbuf_generic *)p0);

	/* Validate that p0 is the first one. */
	p = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
	zassert_true(p, NULL);
	zassert_equal(p->hdr.len, 10, NULL);
	mpsc_pbuf_free(&buffer, (union mpsc_pbuf_generic *)p);

	/* Validate that p1 is the next one. */
	p = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
	zassert_true(p, NULL);
	zassert_equal(p->hdr.len, 20, NULL);
	mpsc_pbuf_free(&buffer, (union mpsc_pbuf_generic *)p);

	/* No more packets. */
	p = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
	zassert_equal(p, NULL, NULL);
}

void test_overflow(void)
{
	struct test_data_var *p;
	uint32_t fill_len = 5;
	uint32_t len0, len1;
	struct mpsc_pbuf_buffer buffer;

	init(&buffer, true, true);
	uint32_t packet_cnt = saturate_buffer_uneven(&buffer, fill_len);

	exp_dropped_data[0] = 0;
	exp_dropped_len[0] = fill_len;
	len0 = 6;
	p = (struct test_data_var *)mpsc_pbuf_alloc(&buffer, len0, K_NO_WAIT);

	p->hdr.len = len0;
	mpsc_pbuf_commit(&buffer, (union mpsc_pbuf_generic *)p);
	zassert_equal(drop_cnt, 1, NULL);

	/* Request allocation which will require dropping 2 packets. */
	len1 = 9;
	exp_dropped_data[1] = 1;
	exp_dropped_len[1] = fill_len;
	exp_dropped_data[2] = 2;
	exp_dropped_len[2] = fill_len;

	p = (struct test_data_var *)mpsc_pbuf_alloc(&buffer, len1, K_NO_WAIT);

	p->hdr.len = len1;
	mpsc_pbuf_commit(&buffer, (union mpsc_pbuf_generic *)p);
	zassert_equal(drop_cnt, 3, NULL);

	for (int i = 0; i < (packet_cnt - drop_cnt); i++) {
		p = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
		zassert_true(p, NULL);
		zassert_equal(p->hdr.len, fill_len, NULL);
		zassert_equal(p->hdr.data, i + drop_cnt, NULL);
		for (int j = 0; j < fill_len - 1; j++) {
			zassert_equal(p->data[j], p->hdr.data + j, NULL);
		}

		mpsc_pbuf_free(&buffer, (union mpsc_pbuf_generic *)p);
	}

	p = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
	zassert_true(p, NULL);
	zassert_equal(p->hdr.len, len0, NULL);
	mpsc_pbuf_free(&buffer, (union mpsc_pbuf_generic *)p);

	p = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
	zassert_true(p, NULL);
	zassert_equal(p->hdr.len, len1, NULL);
	mpsc_pbuf_free(&buffer, (union mpsc_pbuf_generic *)p);

	p = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
	zassert_equal(p, NULL, NULL);
}

void test_overflow_while_claimed(void)
{
	struct test_data_var *p0;
	struct test_data_var *p1;
	struct mpsc_pbuf_buffer buffer;

	init(&buffer, true, true);

	uint32_t fill_len = 5;
	uint32_t len = 6;
	uint32_t packet_cnt = saturate_buffer_uneven(&buffer, fill_len);

	/* Start by claiming a packet. Buffer is now full. Allocation shall
	 * skip claimed packed and drop the next one.
	 */
	p0 = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
	zassert_true(p0, NULL);
	zassert_equal(p0->hdr.len, fill_len, NULL);

	exp_dropped_data[0] = p0->hdr.data + 1; /* next packet is dropped */
	exp_dropped_len[0] = fill_len;
	exp_dropped_data[1] = p0->hdr.data + 2; /* next packet is dropped */
	exp_dropped_len[1] = fill_len;
	p1 = (struct test_data_var *)mpsc_pbuf_alloc(&buffer, 6, K_NO_WAIT);

	zassert_equal(drop_cnt, 2, NULL);
	p1->hdr.len = len;
	mpsc_pbuf_commit(&buffer, (union mpsc_pbuf_generic *)p1);

	mpsc_pbuf_free(&buffer, (union mpsc_pbuf_generic *)p0);

	for (int i = 0; i < packet_cnt - drop_cnt - 1; i++) {
		p0 = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
		zassert_true(p0, NULL);
		zassert_equal(p0->hdr.len, fill_len, NULL);
		zassert_equal(p0->hdr.data, i + drop_cnt + 1, NULL);
		mpsc_pbuf_free(&buffer, (union mpsc_pbuf_generic *)p0);
	}

	p0 = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
	zassert_true(p0, NULL);
	zassert_equal(p0->hdr.len, len, NULL);

	p0 = (struct test_data_var *)mpsc_pbuf_claim(&buffer);
	zassert_equal(p0, NULL, NULL);
}

/*test case main entry*/
void test_main(void)
{
	ztest_test_suite(test_log_buffer,
		ztest_unit_test(test_benchmark_item_put),
		ztest_unit_test(test_item_put_saturate),
		ztest_unit_test(test_item_put_circulate),
		ztest_unit_test(test_item_put_ext_no_overflow),
		ztest_unit_test(test_item_put_ext_saturate),
		ztest_unit_test(test_benchmark_item_put_ext),
		ztest_unit_test(test_benchmark_item_put_data),
		ztest_unit_test(test_item_alloc_commit),
		ztest_unit_test(test_item_alloc_commit_saturate),
		ztest_unit_test(test_item_alloc_preemption),
		ztest_unit_test(test_overflow),
		ztest_unit_test(test_overflow_while_claimed)
		);
	ztest_run_test_suite(test_log_buffer);
}

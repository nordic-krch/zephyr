/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/ztress.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test);

#define RINGBUF_THR 0

typedef uint32_t sample_t;
struct ringbuf {
	sample_t *buf;
	uint32_t cons_idx;
	uint32_t prod_idx;
	uint32_t total;
	uint32_t size;
	/* Number of valid channel samples in the input buffer to put function. */
	uint32_t ch_valid;
	/* Number of channel that shall be skipped in the input buffer to put function. */
	uint32_t in_ch_skip;
	/* Number of channel that shall be skipped in the output buffer to get function. */
	uint32_t out_ch_skip;
};

/* Simple ring buffer that holds data between TDM and USB. It is not thread safe. */
static void ringbuf_reset(struct ringbuf *rb)
{
	rb->total = 0;
	rb->prod_idx = 0;
	rb->cons_idx = 0;
}

static int ringbuf_init(struct ringbuf *rb, sample_t *buf, size_t size,
			uint32_t ch_valid, uint32_t in_ch_skip, uint32_t out_ch_skip)
{
	if (size % ch_valid) {
		return -EINVAL;
	}

	rb->buf = buf;
	rb->size = size;
	rb->ch_valid = ch_valid;
	rb->in_ch_skip = in_ch_skip;
	rb->out_ch_skip = out_ch_skip;
	ringbuf_reset(rb);

	return 0;
}

static int ringbuf_put(struct ringbuf *rb, sample_t *buf, size_t sample_num)
{
	size_t rem_space = rb->size - rb->prod_idx;
	size_t len = sample_num * rb->ch_valid;
	size_t rem_sample = rem_space / rb->ch_valid;
	size_t cpy_sample = MIN(sample_num, rem_sample);
	sample_t *dst;

	if (rb->total + len > rb->size) {
		LOG_WRN("rb %p put no mem", rb);
		return -ENOMEM;
	}

	rb->total += len;

	do {
		dst = &rb->buf[rb->prod_idx];

		for (int i = 0; i < cpy_sample; i++) {
			for (int j = 0; j < rb->ch_valid; j++) {
				*dst = *buf;
				dst++;
				buf++;
			}
			buf += rb->in_ch_skip;
		}

		rb->prod_idx += cpy_sample * rb->ch_valid;
		if (rb->prod_idx == rb->size) {
			rb->prod_idx = 0;
		}
		sample_num -= cpy_sample;
		if (sample_num == 0) {
			return 0;
		}
		cpy_sample = sample_num;
	} while (1);

	return 0;
}

static int ringbuf_get(struct ringbuf *rb, sample_t *buf, size_t sample_num)
{
	size_t len = sample_num * rb->ch_valid;
	size_t rem = rb->size - rb->cons_idx;
	size_t rem_samples = rem / rb->ch_valid;
	size_t cpy_samples = MIN(rem_samples, sample_num);
	sample_t *src;

	if (rb->total < MAX(len, RINGBUF_THR)) {
		LOG_WRN("rb %p get no mem", rb);
		return -ENOMEM;
	}
	LOG_INF("rb %p get samples: %d (%d) (total:%d)", rb, sample_num, len, rb->total);
	rb->total -= len;

	do {
		src = &rb->buf[rb->cons_idx];
		for (int i = 0; i < cpy_samples; i++) {
			for (int j = 0; j < rb->ch_valid; j++) {
				*buf = *src;
				buf++;
				src++;
			}
			buf += rb->out_ch_skip;
		}

		rb->cons_idx += (cpy_samples * rb->ch_valid);
		if (rb->cons_idx == rb->size) {
			rb->cons_idx = 0;
		}

		sample_num -= cpy_samples;
		if (sample_num == 0) {
			return 0;
		}
		cpy_samples = sample_num;
	} while (1);

	return 0;
}

ZTEST(test_tdm_ringbuf, test_basic1)
{
	static uint32_t buf[6*10];
	struct ringbuf rb;
	int err;

	err = ringbuf_init(&rb, buf, ARRAY_SIZE(buf), 6, 2, 0);
	zassert_equal(err, 0);

	uint32_t in_buf[] = {
		1, 2, 3, 4, 5, 6, 10, 10,
		100 + 1, 100 + 2, 100 + 3, 100 + 4, 100 + 5, 100 + 6, 10, 10
	};
	uint32_t exp_out_buf[] = {
		1, 2, 3, 4, 5, 6,
		100 + 1, 100 + 2, 100 + 3, 100 + 4, 100 + 5, 100 + 6
	};
	uint32_t out_buf[2 * 6 + 1];

	memset(out_buf, 0, sizeof(out_buf));

	err = ringbuf_get(&rb, out_buf, 2);
	zassert_equal(err, -ENOMEM);

	err = ringbuf_put(&rb, in_buf, 2);
	zassert_equal(err, 0);

	err = ringbuf_get(&rb, out_buf, 2);
	zassert_equal(err, 0);
	zassert_equal(memcmp(out_buf, exp_out_buf, sizeof(exp_out_buf)), 0);
	zassert_equal(out_buf[2*6], 0);

	err = ringbuf_get(&rb, out_buf, 2);
	zassert_equal(err, -ENOMEM);
}

ZTEST(test_tdm_ringbuf, test_basic2)
{
	static uint32_t buf[6*10];
	struct ringbuf rb;
	int err;

	err = ringbuf_init(&rb, buf, ARRAY_SIZE(buf), 2, 0, 6);
	zassert_equal(err, 0);

	uint32_t in_buf[] = {
		1, 2, 3, 4
	};

	uint32_t exp_out_buf[] = {
		1, 2, 0, 0, 0, 0, 0, 0,
		3, 4, 0, 0, 0, 0, 0 ,0
	};
	uint32_t out_buf[2 * 8 + 1];

	memset(out_buf, 0, sizeof(out_buf));

	err = ringbuf_get(&rb, out_buf, 2);
	zassert_equal(err, -ENOMEM);

	err = ringbuf_put(&rb, in_buf, 2);
	zassert_equal(err, 0);

	err = ringbuf_get(&rb, out_buf, 2);
	zassert_equal(err, 0);
	zassert_equal(memcmp(out_buf, exp_out_buf, sizeof(exp_out_buf)), 0);
	zassert_equal(out_buf[2*8], 0);

	err = ringbuf_get(&rb, out_buf, 2);
	zassert_equal(err, -ENOMEM);
}

static void test_multiple_ops(int ch_cnt, int in_skip, int out_skip)
{
	static uint32_t buf[64];
	struct ringbuf rb;
	int err;

	zassert_true(10 * ch_cnt <= ARRAY_SIZE(buf));
	err = ringbuf_init(&rb, buf, 10 * ch_cnt, ch_cnt, in_skip, out_skip);
	zassert_equal(err, 0);

	uint32_t in_buf[4 *  8];
	uint32_t out_buf[4 * 8];
	int in_cnt = 0;
	int out_cnt = 0;

	for (int i = 0; i < 100; i++) {
		int in_samples = (sys_rand8_get() & 0x3) + 1;
		int out_samples = (sys_rand8_get() & 0x3) + 1;
		int tmp_cnt = in_cnt;

		for (int i = 0; i < in_samples * (ch_cnt + in_skip); i++) {
			if (i % (ch_cnt + in_skip) < ch_cnt) {
				in_buf[i] = tmp_cnt++;
			}
		}

		err = ringbuf_put(&rb, in_buf, in_samples);
		if (err == 0) {
			in_cnt = tmp_cnt;
		}

		memset(out_buf, 0, sizeof(out_buf));
		err = ringbuf_get(&rb, out_buf, out_samples);
		if (err == 0) {
			for (int i = 0; i < out_samples * (ch_cnt + out_skip); i++) {
				if (i % (ch_cnt +  out_skip) < ch_cnt) {
					zassert_equal(out_buf[i], out_cnt);
					out_cnt++;
				} else {
					zassert_equal(out_buf[i], 0);
				}
			}
		}
	}
}

ZTEST(test_tdm_ringbuf, test_multiple_put)
{
	test_multiple_ops(2, 0, 6);
	test_multiple_ops(6, 2, 0);
}

ZTEST_SUITE(test_tdm_ringbuf, NULL, NULL, NULL, NULL, NULL);

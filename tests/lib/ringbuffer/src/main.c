/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ztest.h>
#include <irq_offload.h>
#include <ring_buffer.h>
#include <logging/sys_log.h>

/**
 * @addtogroup t_ringbuffer
 * @{
 * @defgroup t_ringbuffer_api test_ringbuffer_api
 * @brief TestPurpose: verify zephyr ring buffer API functionality
 * - API coverage
 *   -# SYS_RING_BUF_DECLARE_POW2
 *   -# SYS_RING_BUF_DECLARE_SIZE
 *   -# sys_ring_buf_init
 *   -# sys_ring_buf_is_empty
 *   -# sys_ring_buf_space_get
 *   -# sys_ring_buf_put
 *   -# sys_ring_buf_get
 * @}
 */

SYS_RING_BUF_DECLARE_POW2(ring_buf1, 8);

#define TYPE    1
#define VALUE   2
#define INITIAL_SIZE    2


#define RINGBUFFER_SIZE 5
#define DATA_MAX_SIZE 3
#define POW 2


void test_ring_buffer_main(void)
{
	int ret, put_count, i;
	u32_t getdata[6];
	u8_t getsize, getval;
	u16_t gettype;
	int dsize = INITIAL_SIZE;
	char rb_data[] = "ABCDEFGHIJKLMNOPQRSTUVWX";
	put_count = 0;

	while (1) {
		ret = sys_ring_buf_put(&ring_buf1, TYPE, VALUE,
				       (u32_t *)rb_data, dsize);
		if (ret == -EMSGSIZE) {
			SYS_LOG_DBG("ring buffer is full");
			break;
		}
		SYS_LOG_DBG("inserted %d chunks, %d remaining", dsize,
			    sys_ring_buf_space_get(&ring_buf1));
		dsize = (dsize + 1) % SIZE32_OF(rb_data);
		put_count++;
	}

	getsize = INITIAL_SIZE - 1;
	ret = sys_ring_buf_get(&ring_buf1, &gettype, &getval, getdata, &getsize);
	if (ret != -EMSGSIZE) {
		SYS_LOG_DBG("Allowed retreival with insufficient destination buffer space");
		zassert_true((getsize == INITIAL_SIZE), "Correct size wasn't reported back to the caller");
	}

	for (i = 0; i < put_count; i++) {
		getsize = SIZE32_OF(getdata);
		ret = sys_ring_buf_get(&ring_buf1, &gettype, &getval, getdata,
				       &getsize);
		zassert_true((ret == 0), "Couldn't retrieve a stored value");
		SYS_LOG_DBG("got %u chunks of type %u and val %u, %u remaining",
			    getsize, gettype, getval,
			    sys_ring_buf_space_get(&ring_buf1));

		zassert_true((memcmp((char *)getdata, rb_data, getsize * sizeof(u32_t)) == 0),
			     "data corrupted");
		zassert_true((gettype == TYPE), "type information corrupted");
		zassert_true((getval == VALUE), "value information corrupted");
	}

	getsize = SIZE32_OF(getdata);
	ret = sys_ring_buf_get(&ring_buf1, &gettype, &getval, getdata,
			       &getsize);
	zassert_true((ret == -EAGAIN), "Got data out of an empty buffer");
}

/**TESTPOINT: init via SYS_RING_BUF_DECLARE_POW2*/
SYS_RING_BUF_DECLARE_POW2(ringbuf_pow2, POW);

/**TESTPOINT: init via SYS_RING_BUF_DECLARE_SIZE*/
SYS_RING_BUF_DECLARE_SIZE(ringbuf_size, RINGBUFFER_SIZE);

SYS_RING_BUF_BYTES_DECLARE_SIZE(ringbuf_raw, RINGBUFFER_SIZE);

static struct ring_buf ringbuf, *pbuf;

static u32_t buffer[RINGBUFFER_SIZE];

static struct {
	u8_t length;
	u8_t value;
	u16_t type;
	u32_t buffer[DATA_MAX_SIZE];
} data[] = {
	{ 0, 32, 1, {} },
	{ 1, 76, 54, { 0x89ab } },
	{ 3, 0xff, 0xffff, { 0x0f0f, 0xf0f0, 0xff00 } }
};

/*entry of contexts*/
static void tringbuf_put(void *p)
{
	int index = (int)p;
	/**TESTPOINT: ring buffer put*/
	int ret = sys_ring_buf_put(pbuf, data[index].type, data[index].value,
				   data[index].buffer, data[index].length);

	zassert_equal(ret, 0, NULL);
}

static void tringbuf_get(void *p)
{
	u16_t type;
	u8_t value, size32 = DATA_MAX_SIZE;
	u32_t rx_data[DATA_MAX_SIZE];
	int ret, index = (int)p;

	/**TESTPOINT: ring buffer get*/
	ret = sys_ring_buf_get(pbuf, &type, &value, rx_data, &size32);
	zassert_equal(ret, 0, NULL);
	zassert_equal(type, data[index].type, NULL);
	zassert_equal(value, data[index].value, NULL);
	zassert_equal(size32, data[index].length, NULL);
	zassert_equal(memcmp(rx_data, data[index].buffer, size32), 0, NULL);
}

/*test cases*/
void test_ringbuffer_init(void)
{
	/**TESTPOINT: init via sys_ring_buf_init*/
	sys_ring_buf_init(&ringbuf, RINGBUFFER_SIZE, buffer);
	zassert_true(sys_ring_buf_is_empty(&ringbuf), NULL);
	zassert_equal(sys_ring_buf_space_get(&ringbuf), RINGBUFFER_SIZE - 1, NULL);
}

void test_ringbuffer_declare_pow2(void)
{
	zassert_true(sys_ring_buf_is_empty(&ringbuf_pow2), NULL);
	zassert_equal(sys_ring_buf_space_get(&ringbuf_pow2), (1 << POW) - 1, NULL);
}

void test_ringbuffer_declare_size(void)
{
	zassert_true(sys_ring_buf_is_empty(&ringbuf_size), NULL);
	zassert_equal(sys_ring_buf_space_get(&ringbuf_size), RINGBUFFER_SIZE - 1,
		      NULL);
}

void test_ringbuffer_put_get_thread(void)
{
	pbuf = &ringbuf;
	tringbuf_put((void *)0);
	tringbuf_put((void *)1);
	tringbuf_get((void *)0);
	tringbuf_get((void *)1);
	tringbuf_put((void *)2);
	zassert_false(sys_ring_buf_is_empty(pbuf), NULL);
	tringbuf_get((void *)2);
	zassert_true(sys_ring_buf_is_empty(pbuf), NULL);
}

void test_ringbuffer_put_get_isr(void)
{
	pbuf = &ringbuf;
	irq_offload(tringbuf_put, (void *)0);
	irq_offload(tringbuf_put, (void *)1);
	irq_offload(tringbuf_get, (void *)0);
	irq_offload(tringbuf_get, (void *)1);
	irq_offload(tringbuf_put, (void *)2);
	zassert_false(sys_ring_buf_is_empty(pbuf), NULL);
	irq_offload(tringbuf_get, (void *)2);
	zassert_true(sys_ring_buf_is_empty(pbuf), NULL);
}

void test_ringbuffer_put_get_thread_isr(void)
{
	pbuf = &ringbuf;
	tringbuf_put((void *)0);
	irq_offload(tringbuf_put, (void *)1);
	tringbuf_get((void *)0);
	irq_offload(tringbuf_get, (void *)1);
	tringbuf_put((void *)2);
	irq_offload(tringbuf_get, (void *)2);
}

void test_ringbuffer_pow2_put_get_thread_isr(void)
{
	pbuf = &ringbuf_pow2;
	tringbuf_put((void *)0);
	irq_offload(tringbuf_put, (void *)1);
	tringbuf_get((void *)0);
	irq_offload(tringbuf_get, (void *)1);
	tringbuf_put((void *)1);
	irq_offload(tringbuf_get, (void *)1);
}

void test_ringbuffer_size_put_get_thread_isr(void)
{
	pbuf = &ringbuf_size;
	tringbuf_put((void *)0);
	irq_offload(tringbuf_put, (void *)1);
	tringbuf_get((void *)0);
	irq_offload(tringbuf_get, (void *)1);
	tringbuf_put((void *)2);
	irq_offload(tringbuf_get, (void *)2);
}

void test_ringbuffer_raw(void)
{
	int i;
	u8_t inbuf[RINGBUFFER_SIZE];
	u8_t outbuf[RINGBUFFER_SIZE];
	size_t in_size;
	size_t out_size;

	/* Initialize test buffer. */
	for (i = 0; i < RINGBUFFER_SIZE; i++) {
		inbuf[i] = i;
	}

	for (i = 0; i < 10; i++) {
		memset(outbuf, 0, sizeof(outbuf));
		in_size = sys_ring_buf_bytes_cpy_put(&ringbuf_raw, inbuf,
					       RINGBUFFER_SIZE - 2);
		out_size = sys_ring_buf_bytes_cpy_get(&ringbuf_raw, outbuf,
						RINGBUFFER_SIZE - 2);

		zassert_true(in_size == RINGBUFFER_SIZE - 2, NULL);
		zassert_true(in_size == out_size, NULL);
		zassert_true(memcmp(inbuf, outbuf, RINGBUFFER_SIZE - 2) == 0,
			     NULL);
	}

	in_size = sys_ring_buf_bytes_cpy_put(&ringbuf_raw, inbuf,
				       RINGBUFFER_SIZE);
	zassert_equal(in_size, RINGBUFFER_SIZE - 1, NULL);

	in_size = sys_ring_buf_bytes_cpy_put(&ringbuf_raw, inbuf,
				       1);
	zassert_equal(in_size, 0, NULL);

	out_size = sys_ring_buf_bytes_cpy_get(&ringbuf_raw, outbuf,
					RINGBUFFER_SIZE);

	zassert_true(out_size == RINGBUFFER_SIZE - 1, NULL);

	out_size = sys_ring_buf_bytes_cpy_get(&ringbuf_raw, outbuf,
					RINGBUFFER_SIZE + 1);
	zassert_true(out_size == 0, NULL);


}

void test_ringbuffer_alloc_put(void)
{
	u8_t outputbuf[RINGBUFFER_SIZE];
	u8_t inputbuf[] = {1, 2, 3, 4};
	u32_t read_size;
	u32_t allocated;
	u32_t sum_allocated;
	u8_t *data;
	int err;

	sys_ring_buf_init(&ringbuf_raw, RINGBUFFER_SIZE, ringbuf_raw.buf.buf8);

	allocated = sys_ring_buf_bytes_alloc(&ringbuf_raw, &data, 1);
	sum_allocated = allocated;
	zassert_true(allocated == 1, NULL);


	allocated = sys_ring_buf_bytes_alloc(&ringbuf_raw, &data,
					   RINGBUFFER_SIZE - 1);
	sum_allocated += allocated;
	zassert_true(allocated == RINGBUFFER_SIZE - 2, NULL);

	/* Putting too much returns error */
	err = sys_ring_buf_bytes_put(&ringbuf_raw, RINGBUFFER_SIZE);
	zassert_true(err != 0, NULL);

	err = sys_ring_buf_bytes_put(&ringbuf_raw, 1);
	zassert_true(err == 0, NULL);

	err = sys_ring_buf_bytes_put(&ringbuf_raw, RINGBUFFER_SIZE - 2);
	zassert_true(err == 0, NULL);

	read_size = sys_ring_buf_bytes_cpy_get(&ringbuf_raw, outputbuf,
					     RINGBUFFER_SIZE - 1);
	zassert_true(read_size == (RINGBUFFER_SIZE - 1), NULL);

	for (int i = 0; i < 10; i++) {
		allocated = sys_ring_buf_bytes_alloc(&ringbuf_raw, &data, 2);
		if (allocated == 2) {
			data[0] = inputbuf[0];
			data[1] = inputbuf[1];
		} else {
			data[0] = inputbuf[0];
			sys_ring_buf_bytes_alloc(&ringbuf_raw, &data, 1);
			data[0] = inputbuf[1];
		}

		allocated = sys_ring_buf_bytes_alloc(&ringbuf_raw, &data, 2);
		if (allocated == 2) {
			data[0] = inputbuf[2];
			data[1] = inputbuf[3];
		} else {
			data[0] = inputbuf[2];
			sys_ring_buf_bytes_alloc(&ringbuf_raw, &data, 1);
			data[0] = inputbuf[3];
		}

		err = sys_ring_buf_bytes_put(&ringbuf_raw, 4);
		zassert_true(err == 0, NULL);

		read_size = sys_ring_buf_bytes_cpy_get(&ringbuf_raw,
						     outputbuf, 4);
		zassert_true(read_size == 4, NULL);

		zassert_true(memcmp(outputbuf, inputbuf, 4) == 0, NULL);
	}
}

void test_raw_put_free(void)
{
	u8_t indata[] = {1, 2, 3, 4, 5};
	int err;
	u32_t granted;
	u8_t *data;

	sys_ring_buf_init(&ringbuf_raw, RINGBUFFER_SIZE, ringbuf_raw.buf.buf8);

	/* Ring buffer is empty */
	granted = sys_ring_buf_bytes_get(&ringbuf_raw, &data, RINGBUFFER_SIZE);
	zassert_true(granted == 0, NULL);

	for (int i = 0; i < 10; i++) {
		sys_ring_buf_bytes_cpy_put(&ringbuf_raw, indata,
					 RINGBUFFER_SIZE-2);

		granted = sys_ring_buf_bytes_get(&ringbuf_raw, &data,
					       RINGBUFFER_SIZE);

		if (granted == (RINGBUFFER_SIZE-2)) {
			zassert_true(memcmp(indata, data, granted) == 0, NULL);
		} else if (granted < (RINGBUFFER_SIZE-2)) {
			/* When buffer wraps, operation is split. */
			u32_t granted_1 = granted;

			zassert_true(memcmp(indata, data, granted) == 0, NULL);
			granted = sys_ring_buf_bytes_get(&ringbuf_raw, &data,
						       RINGBUFFER_SIZE);

			zassert_true((granted + granted_1) ==
					RINGBUFFER_SIZE - 2, NULL);
			zassert_true(memcmp(&indata[granted_1], data, granted)
					== 0, NULL);
		} else {
			zassert_true(false, NULL);
		}

		/* Freeing more than possible case. */
		err = sys_ring_buf_bytes_free(&ringbuf_raw, RINGBUFFER_SIZE-1);
		zassert_true(err != 0, NULL);

		err = sys_ring_buf_bytes_free(&ringbuf_raw, RINGBUFFER_SIZE-2);
		zassert_true(err == 0, NULL);
	}
}

/*test case main entry*/
void test_main(void)
{
	ztest_test_suite(test_ringbuffer_api,
			 ztest_unit_test(test_ringbuffer_init),/*keep init first!*/
			 ztest_unit_test(test_ringbuffer_declare_pow2),
			 ztest_unit_test(test_ringbuffer_declare_size),
			 ztest_unit_test(test_ringbuffer_put_get_thread),
			 ztest_unit_test(test_ringbuffer_put_get_isr),
			 ztest_unit_test(test_ringbuffer_put_get_thread_isr),
			 ztest_unit_test(test_ringbuffer_pow2_put_get_thread_isr),
			 ztest_unit_test(test_ringbuffer_size_put_get_thread_isr),
			 ztest_unit_test(test_ring_buffer_main),
			 ztest_unit_test(test_ringbuffer_raw),
			 ztest_unit_test(test_ringbuffer_alloc_put),
			 ztest_unit_test(test_raw_put_free)
			 );
	ztest_run_test_suite(test_ringbuffer_api);
}

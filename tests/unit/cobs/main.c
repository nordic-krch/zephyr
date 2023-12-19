 /* Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/sys/cobs.h>
#include <../../../lib/os/cobs.c>

static void print_buf(uint8_t *buf, size_t len)
{
	int l16 = len / 16;
	int rem = len % 16;
	for (int i = 0; i < l16; i++) {
		PRINT("%d\t", 16*i);
		for (int j = 0; j <  16; j++) {
			PRINT("%02x ", *buf++);
		}
		PRINT("\n");
	}

	PRINT("%d\t", 16*l16);
	for (int i = 0; i < rem; i++) {
		PRINT("%02x ", *buf++);
	}
	PRINT("\n");
}


static void test_data(uint8_t *data, uint8_t *in_data, uint8_t *exp_data,
		      int exp_rv, size_t len, size_t off, int line)
{
	int rv;

	memcpy(&data[1], in_data, len);

	rv = cobs_r_encode(data, len, off);
	zassert_equal(rv, exp_rv, "Failed test at line:%d", line);

	int res = memcmp(exp_data, data, rv);

	if (res != 0) {
		PRINT("exp: ");
		print_buf(exp_data, exp_rv);
		PRINT("got: ");
		print_buf(data, exp_rv);
	}

	zassert_equal(res, 0, "Failed test at line:%d", line);
	rv = cobs_r_decode(data, exp_rv);
	zassert_equal(rv, len);

	rv = memcmp(data, in_data, len);
	zassert_equal(rv, 0);
}

#define TEST_DATA(buf, in, exp) do {\
	test_data(buf, in, exp, sizeof(exp), sizeof(in), 1, __LINE__); \
} while (0)

ZTEST(cobs_r_api, test_cobs)
{
	{
		uint8_t in_data[] = { 0xf8, 0xc6 };
		uint8_t exp_buf[] = { 0xc6, 0xf8 };
		uint8_t buf[sizeof(in_data) + 5] __aligned(sizeof(uint32_t));

		/* aligned */
		TEST_DATA(&buf[3], in_data, exp_buf);
		/* unaligned */
		TEST_DATA(&buf[2], in_data, exp_buf);
	}
	{
		uint8_t in_data[] = { 0x2f, 0xa2, 0xff };
		uint8_t exp_buf[] = { 0x03, 0x2f, 0xa2, 0x01 };
		uint8_t buf[sizeof(in_data) + 5] __aligned(sizeof(uint32_t));

		/* aligned */
		TEST_DATA(&buf[3], in_data, exp_buf);
		/* unaligned */
		TEST_DATA(&buf[2], in_data, exp_buf);
	}

	{
		uint8_t in_data[] = { 0x2f, 0xa2, 0xff, 0x92, 0x73, 0x02 };
		uint8_t exp_buf[] = { 0x03, 0x2F, 0xA2, 0x04, 0x92, 0x73, 0x02 };
		uint8_t buf[sizeof(in_data) + 5] __aligned(sizeof(uint32_t));

		/* aligned */
		TEST_DATA(&buf[3], in_data, exp_buf);
		/* unaligned */
		TEST_DATA(&buf[2], in_data, exp_buf);
	}

	{
		uint8_t in_data[] = { 0x2f, 0xa2, 0x92, 0x73, 0x11 };
		uint8_t exp_buf[] = { 0x11, 0x2f, 0xa2, 0x92, 0x73 };
		uint8_t buf[sizeof(in_data) + 5] __aligned(sizeof(uint32_t));

		/* aligned */
		TEST_DATA(&buf[3], in_data, exp_buf);
		/* unaligned */
		TEST_DATA(&buf[2], in_data, exp_buf);
	}

	{
		uint8_t in_data[] = { 0x2f, 0xa2, 0x92, 0x73, 0x05 };
		uint8_t exp_buf[] = { 0x06, 0x2f, 0xa2, 0x92, 0x73, 0x05 };
		uint8_t buf[sizeof(in_data) + 5] __aligned(sizeof(uint32_t));

		/* aligned */
		TEST_DATA(&buf[3], in_data, exp_buf);
		/* unaligned */
		TEST_DATA(&buf[2], in_data, exp_buf);
	}

	{
		uint8_t in_data[] = { 0x2f, 0xa2, 0xff, 0xff, 0x05 };
		uint8_t exp_buf[] = { 0x03, 0x2f, 0xa2, 0x01, 0x05 };
		uint8_t buf[sizeof(in_data) + 5] __aligned(sizeof(uint32_t));

		/* aligned */
		TEST_DATA(&buf[3], in_data, exp_buf);
		/* unaligned */
		TEST_DATA(&buf[2], in_data, exp_buf);
	}

	{
		uint8_t in_data[] = { 0x2f, 0xa2, 0xff, 0xff, 0x01 };
		uint8_t exp_buf[] = { 0x03, 0x2f, 0xa2, 0x01, 0x02, 0x01 };
		uint8_t buf[sizeof(in_data) + 5] __aligned(sizeof(uint32_t));

		/* aligned */
		TEST_DATA(&buf[3], in_data, exp_buf);
		/* unaligned */
		TEST_DATA(&buf[2], in_data, exp_buf);
	}

	{
		uint8_t in_data[253] = { 0 };
		uint8_t exp_buf[253+2] = { 0 };
		uint8_t buf[sizeof(in_data) + 5] __aligned(sizeof(uint32_t));

		exp_buf[0] = 0xfe;
		exp_buf[0xfe] = 1;

		/* aligned */
		TEST_DATA(&buf[3], in_data, exp_buf);
		/* unaligned */
		TEST_DATA(&buf[2], in_data, exp_buf);
	}

	{
		uint8_t in_data[253] = { 0 };
		uint8_t exp_buf[253+1] = { 0 };
		uint8_t buf[sizeof(in_data) + 5] __aligned(sizeof(uint32_t));

		in_data[252] = 0xff;
		exp_buf[0] = 253;
		exp_buf[253] = 1;

		/* aligned */
		TEST_DATA(&buf[3], in_data, exp_buf);
		/* unaligned */
		TEST_DATA(&buf[2], in_data, exp_buf);
	}
}

ZTEST(cobs_r_api, test_performance)
{
	{
		uint8_t in_data[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		uint8_t buf[sizeof(in_data) + 5] __aligned(sizeof(uint32_t));

		memcpy(&buf[4], in_data, sizeof(in_data));

		/* aligned */
		uint32_t t_aligned = k_cycle_get_32();
		for (int i = 0; i < 10000; i++) {
			(void)cobs_r_encode(&buf[3], sizeof(in_data), 1);
		}
		t_aligned = k_cycle_get_32() - t_aligned;

		/* unaligned */
		memcpy(&buf[3], in_data, sizeof(in_data));

		uint32_t t_unaligned = k_cycle_get_32();
		for (int i = 0; i < 10000; i++) {
			(void)cobs_r_encode(&buf[2], sizeof(in_data), 1);
		}
		t_unaligned = k_cycle_get_32() - t_unaligned;

		PRINT("aligned:%d unaligned:%d\n", t_aligned, t_unaligned);
	}
}

/* Simple LCRNG (modulus is 2^64!) cribbed from:
 * https://nuclear.llnl.gov/CNP/rng/rngman/node4.html
 *
 * Don't need much in the way of quality, do need repeatability across
 * platforms.
 */
static unsigned int rand_mod(unsigned int mod)
{
	static unsigned long long state = 123456789; /* seed */

	state = state * 2862933555777941757ul + 3037000493ul;

	return ((unsigned int)(state >> 32)) % mod;
}

ZTEST(cobs_r_api, test_random)
{
	static uint8_t buffer[1024 + 16] __aligned(4);
	static uint8_t exp_buffer[1024 + 16];
	int rpt = 100000;

	while (rpt-- > 0) {
		size_t len = rand_mod(600);
		size_t ffs = rand_mod(5);

		len = MAX(1, len);
		/*printk("ITER %d len:%d-------------------\n", rpt, len);*/
		size_t off = 1 + len / 253;

		for (int i = 0; i < len; i++) {
			buffer[i + off] = rand_mod(255);
		}

		/* Add some 0xffs */
		for (int i = 0; i < ffs; i++) {
			buffer[rand_mod(len)] = 0xff;
		}

		memcpy(exp_buffer, &buffer[off], len);

		int rv = cobs_r_encode(buffer, len, off);

		for (int i = 0; i < rv; i++) {
			/* no delimiter inside the packet. */
			zassert_true(buffer[i] != 0xFF);
		}

		rv = cobs_r_decode(buffer, rv);
		zassert_equal(rv, len);

		rv = memcmp(exp_buffer, buffer, len);
		zassert_equal(rv, 0);
	}

}
ZTEST_SUITE(cobs_r_api, NULL, NULL, NULL, NULL, NULL);

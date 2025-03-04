/*
 * Copyright (c) 2025 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Single core test for shared MPSC Packet buffer.
 */

#include <zephyr/ztest.h>
#include <zephyr/ztress.h>
#include <zephyr/random/random.h>
#include <zephyr/ipc/shmpsc_pbuf.h>

static uint32_t mem_buf[256];
static struct shmpsc_pbuf *pb;
static struct k_spinlock lock;

static void before(void *arg)
{
	ARG_UNUSED(arg);
	uint32_t *buffer = (uint32_t *)((uintptr_t)mem_buf + sizeof(struct shmpsc_pbuf));
	uint32_t size = sizeof(mem_buf) - sizeof(struct shmpsc_pbuf);
	struct shmpsc_pbuf_config config = {
		.buf = buffer,
		.size = size,
		.lock = &lock
	};

	pb = (struct shmpsc_pbuf *)mem_buf;

	shmpsc_pbuf_init(pb, &config);
}

ZTEST(shmpsc_pbuf, test_basic)
{
	int rpt = 5;
	uint32_t cnt;

	do {
		for (int i = 1; i < 100; i++) {
			uint8_t *buf = shmpsc_pbuf_alloc(pb, i);
			uint8_t *rbuf;
			uint32_t rlen;

			zassert_not_null(buf);

			for (int j = 0; j < i; j++) {
				buf[j] = i + j;
			}
			shmpsc_pbuf_commit(pb, (uint32_t *)buf, i);
			rbuf = shmpsc_pbuf_claim(pb, &rlen, &cnt);

			zassert_not_null(buf);
			zassert_equal(rlen, i);
			for (int j = 0; j < i; j++) {
				zassert_equal(rbuf[j], i + j);
			}

			shmpsc_pbuf_free(pb, rbuf);
		}
		rpt--;
	} while (rpt > 0);
}

struct ctx_data {
	uint8_t prod_idx;
	uint8_t cons_idx;
};

struct test_data {
	struct shmpsc_pbuf *pb;
	struct ctx_data ctx[4];
	uint32_t cnt;
};

static bool consume(void *user_data, uint32_t cnt, bool last, int prio)
{
	struct test_data *data = user_data;
	struct shmpsc_pbuf *pb = data->pb;
	struct ctx_data *ctx;
	uint32_t rpt = sys_rand32_get() & 0x3;
	uint8_t *buf;
	uint32_t len;
	uint32_t src;

	while (rpt) {
		buf = shmpsc_pbuf_claim(pb, &len, &data->cnt);
		if (!buf) {
			return true;
		}
		src = buf[0];
		ctx = &data->ctx[src];
		zassert_equal(buf[1], ctx->cons_idx);
		ctx->cons_idx++;
		for (int i = 2; i < len; i++) {
			zassert_equal(buf[i], src + i);
		}
		shmpsc_pbuf_free(pb, buf);
		rpt--;
	}

	return true;
}

static bool produce(void *user_data, uint32_t cnt, bool last, int prio)
{
	struct test_data *data = user_data;
	struct shmpsc_pbuf *pb = data->pb;
	struct ctx_data *ctx = &data->ctx[prio];
	uint32_t len = sys_rand32_get() % (pb->size / 4) + 2;
	uint8_t *buf;

	buf = shmpsc_pbuf_alloc(pb, len);
	if (!buf) {
		return true;
	}

	buf[0] = prio;
	buf[1] = ctx->prod_idx;
	ctx->prod_idx++;
	for (int i = 2; i < len; i++) {
		buf[i] = prio + i;
	}

	shmpsc_pbuf_commit(pb, buf, len);
	return true;
}

static void stress_test(ztress_handler h1,
			ztress_handler h2,
			ztress_handler h3,
			ztress_handler h4)
{
	struct test_data data;
	uint32_t preempt_max = 4000;
	k_timeout_t t = Z_TIMEOUT_TICKS(20);

	if (CONFIG_SYS_CLOCK_TICKS_PER_SEC < 10000) {
		ztest_test_skip();
	}

	ztress_set_timeout(K_MSEC(10000));
	memset(&data, 0, sizeof(data));
	data.pb = pb;

	ZTRESS_EXECUTE(ZTRESS_THREAD(h1,  &data, 0, 0, t),
			ZTRESS_THREAD(h2, &data, 0, preempt_max, t),
			ZTRESS_THREAD(h2, &data, 0, preempt_max, t),
			ZTRESS_THREAD(h3, &data, 0, preempt_max, t));
}

ZTEST(shmpsc_pbuf, test_stress_low_consume)
{
	stress_test(produce, produce, produce, consume);
}

ZTEST(shmpsc_pbuf, test_stress_mid_consume)
{
	stress_test(produce, produce, consume, produce);
}

ZTEST(shmpsc_pbuf, test_stress_high_consume)
{
	stress_test(consume, produce, produce, produce);
}

/*test case main entry*/
ZTEST_SUITE(shmpsc_pbuf, NULL, NULL, before, NULL, NULL);

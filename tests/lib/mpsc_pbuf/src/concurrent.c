/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <ztest.h>
#include <ztress.h>
#include <sys/mpsc_pbuf.h>
#include <sys/ring_buffer.h>
#include <random/rand32.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(test);

static uint32_t buf32[128];
static struct mpsc_pbuf_buffer buffer;
volatile int test_microdelay_cnt;
static struct k_spinlock lock;

static atomic_t test_failed;
static int test_failed_line;
static uint32_t test_failed_cnt;

static uint32_t track_mask[8];
static uint32_t track_base_idx;

/* stats */
struct test_stats {
	atomic_t claim_cnt;
	atomic_t claim_miss_cnt;
	atomic_t produce_cnt;
	atomic_t alloc_fails;
};

static struct test_stats stats;

#define LEN_BITS 8
#define DATA_BITS (32 - MPSC_PBUF_HDR_BITS - LEN_BITS)

struct test_data {
	MPSC_PBUF_HDR;
	uint32_t len : LEN_BITS;
	uint32_t data : DATA_BITS;
	uint32_t buf[];
};

static void track_produce_locked(uint32_t idx)
{
	uint32_t ridx = idx - track_base_idx;
	uint32_t byte = ridx / 32;
	uint32_t bit = ridx & 0x1f;

	track_mask[byte] |= BIT(bit);
}

static bool track_consume_locked(uint32_t idx)
{
	uint32_t ridx = idx - track_base_idx;
	uint32_t byte = ridx / 32;
	uint32_t bit = ridx & 0x1f;

	if (idx < track_base_idx || idx > (track_base_idx + 32 * ARRAY_SIZE(track_mask))) {
		printk("Strange value %d base:%d", idx, track_base_idx);
		return false;
	}

	if (track_mask[byte] & BIT(bit)) {
		track_mask[byte] &= ~BIT(bit);
	} else {
		/* Already consumed. */
		printk("already consumed\n");
		return false;
	}

	if (byte > 4) {
		/* Far in the past should all be consumed by now. */
		if (track_mask[0]) {
			printk("not all dropped\n");
			return false;
		}

		memmove(track_mask, &track_mask[1], sizeof(track_mask) - sizeof(uint32_t));
		track_mask[ARRAY_SIZE(track_mask) - 1] = 0;
		track_base_idx += 32;
	}

	return true;
}

static void test_fail(int line, uint32_t cnt)
{
	if (atomic_cas(&test_failed, 0, 1)) {
		test_failed_line = line;
		test_failed_cnt = cnt;
		ztress_abort();
	}
}

static void consume_check(struct test_data *data, bool claim)
{
	bool res;
	k_spinlock_key_t key = k_spin_lock(&lock);

	res = track_consume_locked(data->data);

	k_spin_unlock(&lock, key);

	if (!res) {
		test_fail(__LINE__, data->data);
	}

	for (int i = 0; i < data->len - 1; i++) {
		if (data->buf[i] != (0x80000000 | (data->data + i))) {
			test_fail(__LINE__, data->buf[i]);
		}
	}
}

static void drop(const struct mpsc_pbuf_buffer *buffer, const union mpsc_pbuf_generic *item)
{
	struct test_data *data = (struct test_data *)item;

	consume_check(data, false);
}

static bool consume(void *user_data, uint32_t cnt, bool last, int prio)
{
	struct mpsc_pbuf_buffer *buffer = user_data;
	struct test_data *data = (struct test_data *)mpsc_pbuf_claim(buffer);

	if (data) {

		consume_check(data, true);

		mpsc_pbuf_free(buffer, (union mpsc_pbuf_generic *)data);
	} else {
		atomic_inc(&stats.claim_miss_cnt);
	}

	atomic_inc(&stats.claim_cnt);

	return true;
}

static bool produce(void *user_data, uint32_t cnt, bool last, int prio)
{
	struct mpsc_pbuf_buffer *buffer = user_data;
	k_spinlock_key_t key = k_spin_lock(&lock);
	uint32_t id;

	track_produce_locked(stats.produce_cnt);
	id = stats.produce_cnt;
	stats.produce_cnt++;
	k_spin_unlock(&lock, key);

	uint32_t wlen = sys_rand32_get() % (buffer->size / 4) + 1;
	struct test_data *data = (struct test_data *)mpsc_pbuf_alloc(buffer, wlen, K_NO_WAIT);

	if (!data) {
		k_spinlock_key_t key = k_spin_lock(&lock);
		bool res = track_consume_locked(id);

		k_spin_unlock(&lock, key);
		if (res == false) {
			test_fail(__LINE__, data->data);
		}

		atomic_inc(&stats.alloc_fails);
		return true;
	}

	/* Note that producing may be interrupted and there will be discontinuity
	 * which must be handled when verifying correctness during consumption.
	 */
	data->data = id;
	data->len = wlen;
	for (int i = 0; i < (wlen - 1); i++) {
		data->buf[i] = 0x80000000 | (id + i);
	}

	mpsc_pbuf_commit(buffer, (union mpsc_pbuf_generic *)data);

	return true;
}

static uint32_t get_wlen(const union mpsc_pbuf_generic *item)
{
	struct test_data *data = (struct test_data *)item;

	return data->len;
}

/* Test is using 3 contexts to access single mpsc_pbuf instance. Those contexts
 * are on different priorities (2 threads and timer interrupt) and preempt
 * each other. One context is consuming and other two are producing. It
 * validates that each produced packet is consumed or dropped.
 *
 * Test is randomized. Thread sleep time and timer timeout are random. Packet
 * size is also random. Dedicated work is used to fill a pool of random number
 * (generating random numbers is time consuming so it is decoupled from the main
 * test.
 *
 * Test attempts to stress mpsc_pbuf but having as many preemptions as possible.
 * In order to achieve that CPU load is monitored periodically and if load is
 * to low then sleep/timeout time is reduced by reducing a factor that
 * is used to calculate sleep/timeout time (factor * random number). Test aims
 * to keep cpu load at ~80%. Some room is left for keeping random number pool
 * filled.
 */
static void stress_test(ztress_handler h1, ztress_handler h2, ztress_handler h3)
{
	uint32_t preempt_max = 4000;
	k_timeout_t t = Z_TIMEOUT_TICKS(20);
	struct mpsc_pbuf_buffer_config config = {
		.buf = buf32,
		.size = ARRAY_SIZE(buf32),
		.notify_drop = drop,
		.get_wlen = get_wlen,
		.flags = MPSC_PBUF_MODE_OVERWRITE
	};

	if (CONFIG_SYS_CLOCK_TICKS_PER_SEC < 100000) {
		ztest_test_skip();
	}

	test_failed = 0;
	track_base_idx = 0;
	memset(track_mask, 0, sizeof(track_mask));
	memset(&stats, 0, sizeof(stats));
	memset(&buffer, 0, sizeof(buffer));
	mpsc_pbuf_init(&buffer, &config);

	ztress_set_timeout(K_MSEC(5000));

	ZTRESS_EXECUTE(ZTRESS_TIMER(h1,  &buffer, 0, t),
		       ZTRESS_THREAD(h2, &buffer, 0, preempt_max, t),
		       ZTRESS_THREAD(h3, &buffer, 0, preempt_max, t));

	PRINT("Test report:\n");
	PRINT("\tClaims:%ld, claim misses:%ld\n", stats.claim_cnt, stats.claim_miss_cnt);
	PRINT("\tProduced:%ld, allocation failures:%ld\n", stats.produce_cnt, stats.alloc_fails);
}

void test_stress_preemptions_low_consumer(void)
{
	stress_test(produce, produce, consume);
}

/* Consumer has medium priority with one lower priority consumer and one higher. */
void test_stress_preemptions_mid_consumer(void)
{
	stress_test(produce, consume, produce);
}

/* Consumer has the highest priority, it preempts both producer. */
void test_stress_preemptions_high_consumer(void)
{
	stress_test(consume, produce, produce);
}

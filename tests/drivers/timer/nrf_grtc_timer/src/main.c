/*
 * Copyright (c) 2022, Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/ztest.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>
#include <zephyr/busy_sim.h>
#include <nrfx_grtc.h>
#include <hal/nrf_grtc.h>
LOG_MODULE_REGISTER(test, 4);

#define GRTC_SLEW_TICKS 10
#define NUMBER_OF_TRIES 2000
#define CYC_PER_TICK                                                                               \
	((uint64_t)sys_clock_hw_cycles_per_sec() / (uint64_t)CONFIG_SYS_CLOCK_TICKS_PER_SEC)
#define TIMER_COUNT_TIME_MS 10
#define WAIT_FOR_TIMER_EVENT_TIME_MS TIMER_COUNT_TIME_MS + 5

static volatile uint8_t compare_isr_call_counter;

/* GRTC timer compare interrupt handler */
static void timer_compare_interrupt_handler(int32_t id, uint64_t expire_time, void *user_data)
{
	compare_isr_call_counter++;
	TC_PRINT("Compare value reached, user data: '%s'\n", (char *)user_data);
	TC_PRINT("Call counter: %d\n", compare_isr_call_counter);
}

ZTEST(nrf_grtc_timer, test_get_ticks)
{
	k_timeout_t t = K_MSEC(1);

	uint64_t exp_ticks = z_nrf_grtc_timer_read() + t.ticks * CYC_PER_TICK;
	int64_t ticks;

	/* Relative 1ms from now timeout converted to GRTC */
	ticks = z_nrf_grtc_timer_get_ticks(t);
	zassert_true((ticks >= exp_ticks) && (ticks <= (exp_ticks + GRTC_SLEW_TICKS)),
		     "Unexpected result %" PRId64 " (expected: %" PRId64 ")", ticks, exp_ticks);

	k_msleep(1);

	for (uint32_t i = 0; i < NUMBER_OF_TRIES; i++) {
		/* Absolute timeout 1ms in the past */
		uint64_t curr_tick;
		uint64_t curr_grtc_tick;
		uint64_t curr_tick2;

		do {
			/* GRTC and system tick must be read during single system tick. */
			curr_tick = sys_clock_tick_get();
			curr_grtc_tick = z_nrf_grtc_timer_read();
			curr_tick2 = sys_clock_tick_get();
		} while (curr_tick != curr_tick2);

		t = Z_TIMEOUT_TICKS(Z_TICK_ABS(curr_tick - K_MSEC(1).ticks));

		exp_ticks = curr_grtc_tick - K_MSEC(1).ticks * CYC_PER_TICK;
		ticks = z_nrf_grtc_timer_get_ticks(t);

		zassert_true((ticks >= (exp_ticks - CYC_PER_TICK + 1)) &&
				     (ticks <= (exp_ticks + GRTC_SLEW_TICKS)),
			     "Unexpected result %" PRId64 " (expected: %" PRId64 ")", ticks,
			     exp_ticks);

		/* Absolute timeout 10ms in the future */
		do {
			/* GRTC and system tick must be read during single system tick. */
			curr_tick = sys_clock_tick_get();
			curr_grtc_tick = z_nrf_grtc_timer_read();
			curr_tick2 = sys_clock_tick_get();
		} while (curr_tick != curr_tick2);

		t = Z_TIMEOUT_TICKS(Z_TICK_ABS(curr_tick + K_MSEC(10).ticks));
		exp_ticks = curr_grtc_tick + K_MSEC(10).ticks * CYC_PER_TICK;
		ticks = z_nrf_grtc_timer_get_ticks(t);
		zassert_true((ticks >= (exp_ticks - CYC_PER_TICK + 1)) &&
				     (ticks <= (exp_ticks + GRTC_SLEW_TICKS)),
			     "Unexpected result %" PRId64 " (expected: %" PRId64 ")", ticks,
			     exp_ticks);
	}
}

ZTEST(nrf_grtc_timer, test_timer_count_in_compare_mode)
{
	int err;
	uint64_t test_ticks = 0;
	uint64_t compare_value = 0;
	char user_data[] = "test_timer_count_in_compare_mode\n";
	int32_t channel = z_nrf_grtc_timer_chan_alloc();

	TC_PRINT("Allocated GRTC channel %d\n", channel);
	if (channel < 0) {
		TC_PRINT("Failed to allocate GRTC channel, chan=%d\n", channel);
		ztest_test_fail();
	}

	compare_isr_call_counter = 0;
	test_ticks = z_nrf_grtc_timer_get_ticks(K_MSEC(TIMER_COUNT_TIME_MS));
	err = z_nrf_grtc_timer_set(channel, test_ticks, timer_compare_interrupt_handler,
				   (void *)user_data);

	zassert_equal(err, 0, "z_nrf_grtc_timer_set raised an error: %d", err);

	z_nrf_grtc_timer_compare_read(channel, &compare_value);
	zassert_true(K_TIMEOUT_EQ(K_TICKS(compare_value), K_TICKS(test_ticks)),
		     "Compare register set failed");
	zassert_equal(err, 0, "Unexpected error raised when setting timer, err: %d", err);

	k_sleep(K_MSEC(WAIT_FOR_TIMER_EVENT_TIME_MS));

	TC_PRINT("Compare event generated ?: %d\n", z_nrf_grtc_timer_compare_evt_check(channel));
	TC_PRINT("Compare event register address: %X\n",
		 z_nrf_grtc_timer_compare_evt_address_get(channel));

	zassert_equal(compare_isr_call_counter, 1, "Compare isr call counter: %d",
		      compare_isr_call_counter);
	z_nrf_grtc_timer_chan_free(channel);
}

ZTEST(nrf_grtc_timer, test_timer_abort_in_compare_mode)
{
	int err;
	uint64_t test_ticks = 0;
	uint64_t compare_value = 0;
	char user_data[] = "test_timer_abort_in_compare_mode\n";
	int32_t channel = z_nrf_grtc_timer_chan_alloc();

	TC_PRINT("Allocated GRTC channel %d\n", channel);
	if (channel < 0) {
		TC_PRINT("Failed to allocate GRTC channel, chan=%d\n", channel);
		ztest_test_fail();
	}

	compare_isr_call_counter = 0;
	test_ticks = z_nrf_grtc_timer_get_ticks(K_MSEC(TIMER_COUNT_TIME_MS));
	err = z_nrf_grtc_timer_set(channel, test_ticks, timer_compare_interrupt_handler,
				   (void *)user_data);
	zassert_equal(err, 0, "z_nrf_grtc_timer_set raised an error: %d", err);

	z_nrf_grtc_timer_abort(channel);

	z_nrf_grtc_timer_compare_read(channel, &compare_value);
	zassert_true(K_TIMEOUT_EQ(K_TICKS(compare_value), K_TICKS(test_ticks)),
		     "Compare register set failed");

	zassert_equal(err, 0, "Unexpected error raised when setting timer, err: %d", err);

	k_sleep(K_MSEC(WAIT_FOR_TIMER_EVENT_TIME_MS));
	zassert_equal(compare_isr_call_counter, 0, "Compare isr call counter: %d",
		      compare_isr_call_counter);
	z_nrf_grtc_timer_chan_free(channel);
}

enum test_timer_state {
	TIMER_IDLE,
	TIMER_PREPARE,
	TIMER_ACTIVE
};

struct test_grtc_timer {
	struct k_timer timer;
	uint32_t expire;
	uint32_t start_cnt;
	uint32_t expire_cnt;
	uint32_t abort_cnt;
	uint32_t exp_expire;
	uint32_t max_late;
	uint32_t avg_late;
	enum test_timer_state state;
};

static struct test_grtc_timer timers[8];
static uint32_t test_end;
static k_tid_t test_tid;
static volatile bool test_run;

#define MAIN_CHAN 0

static void stress_test_action(int ctx, int id)
{
	struct test_grtc_timer *timer = &timers[id];

	if (timer->state == TIMER_ACTIVE) {
		if (timer->abort_cnt < timer->expire_cnt / 2) {
			timer->state = TIMER_PREPARE;
			k_timer_stop(&timer->timer);
			timer->abort_cnt++;
			LOG_DBG("timer:%p ctx:%d abort %d", timer, ctx, timer->abort_cnt);
			timer->state = TIMER_IDLE;
		}
	} else if (timer->state == TIMER_IDLE) {
		int ticks = 10 + (sys_rand32_get() & 0x3F);
		uint32_t elapsed = sys_clock_elapsed();
		uint32_t base;

		if (elapsed == 0) {
			nrfx_err_t err;
			uint64_t val;

			err = nrfx_grtc_syscounter_cc_value_read(MAIN_CHAN, &val);
			zassert_equal(err, NRFX_SUCCESS);

			base = (uint32_t)val;
		} else {
			base = sys_clock_cycle_get_32();
			if (base > test_end) {
				test_run = false;
			}
		}

		uint32_t cyc = k_ticks_to_cyc_floor32(ticks);
		timer->exp_expire = base + cyc;
		k_timeout_t t = K_TICKS(ticks);

		LOG_DBG("timer:%p ctx:%d start ticks:%d cyc:%d", timer, ctx, ticks, cyc);
		timer->state = TIMER_PREPARE;
		k_timer_start(&timer->timer, t, K_NO_WAIT);
		timer->start_cnt++;
		timer->state = TIMER_ACTIVE;
	}
}

static void stress_test_actions(int ctx)
{
	uint32_t r = sys_rand32_get();
	int action_cnt = Z_MAX(r & 0x3, 1);
	int tmr_id = (r >> 8) % ARRAY_SIZE(timers);

	if (((r >> 2) & 0x3) == 0) {
		LOG_DBG("ctx:%d thread wakeup", ctx);
		k_wakeup(test_tid);
	}

	for (int i = 0; i < action_cnt; i++) {
		stress_test_action(ctx, tmr_id);
	}
}

static void timer_cb(struct k_timer *timer)
{
	struct test_grtc_timer *test_timer = CONTAINER_OF(timer, struct test_grtc_timer, timer);
	uint32_t now = k_cycle_get_32();
	int diff = now - test_timer->exp_expire;

	LOG_DBG("timer %p expired diff:%d", test_timer, diff);
	zassert_true(diff >= 0);
	test_timer->max_late = MAX(diff, test_timer->max_late);

	if (test_timer->expire_cnt == 0) {
		test_timer->avg_late = diff;
	} else {
		test_timer->avg_late = (test_timer->avg_late * test_timer->expire_cnt + diff) /
				test_timer->expire_cnt + 1;
	}

	test_timer->expire_cnt++;
	test_timer->state = TIMER_IDLE;

	if (test_run) {
		stress_test_actions(1);
	}
}

static void counter_set(const struct device *dev, struct counter_alarm_cfg *cfg)
{
	int err;

	cfg->ticks = 10 + (sys_rand32_get() & 0x1f);
	err = counter_set_channel_alarm(dev, 0, cfg);
	zassert_equal(err, 0);
}

static void counter_cb(const struct device *dev, uint8_t chan_id, uint32_t ticks, void *user_data)
{
	struct counter_alarm_cfg *config = user_data;

	if (test_run) {
		stress_test_actions(0);
		counter_set(dev, config);
	}
}

static void grtc_stress_test(bool busy_sim_en)
{
	static struct counter_alarm_cfg alarm_cfg;
	const struct device *const counter_dev = DEVICE_DT_GET(DT_NODELABEL(test_timer));
	uint32_t test_ms = 20;

	test_end = k_cycle_get_32() + k_ms_to_cyc_floor32(test_ms);
	test_tid = k_current_get();

	for (size_t i = 0; i < ARRAY_SIZE(timers); i++) {
		k_timer_init(&timers[i].timer, timer_cb, NULL);
	}

	counter_start(counter_dev);

	alarm_cfg.callback = counter_cb;
	alarm_cfg.user_data = &alarm_cfg;
	test_run = true;
	/*counter_set(counter_dev, &alarm_cfg);*/

	if (busy_sim_en) {
		busy_sim_start(500, 200, 1000, 400, NULL);
	}

	LOG_DBG("Starting test, will end at %d", test_end);
	while (k_uptime_get_32() < test_end) {
		stress_test_actions(2);
		k_sleep(K_MSEC(test_ms));
	}

	test_run = false;
	k_msleep(10);

	for (size_t i = 0; i < ARRAY_SIZE(timers); i++) {
		zassert_equal(timers[i].state, TIMER_IDLE);
		TC_PRINT("Timer%d\r\n\tstart_cnt:%d abort_cnt:%d expire_cnt:%d\n",
			i, timers[i].start_cnt, timers[i].abort_cnt, timers[i].expire_cnt);
		TC_PRINT("\tavarage late:%d ticks, max late:%d\n",
				timers[i].avg_late, timers[i].max_late);
	}

	if (busy_sim_en) {
		busy_sim_stop();
	}

	counter_stop(counter_dev);
}

ZTEST(nrf_grtc_timer, test_stress)
{
	grtc_stress_test(false);
}

ZTEST_SUITE(nrf_grtc_timer, NULL, NULL, NULL, NULL, NULL);

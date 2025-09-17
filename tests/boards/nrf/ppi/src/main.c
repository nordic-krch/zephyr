/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_lpcomp.h>
#include <ppi/gppi.h>

#ifndef CONFIG_NORDIC_DPPI_MULTI_DOMAIN
nrfx_gppi_t gppi_instance = {
#ifdef CONFIG_NORDIC_GPPI_PPI
	.ch_mask = BIT_MASK(PPI_CH_NUM),
	.group_mask = BIT_MASK(PPI_GROUP_NUM),
#else
	.ch_mask = (DPPIC_CH_NUM == 32) ? UINT32_MAX : BIT_MASK(DPPIC_CH_NUM),
	.group_mask = BIT_MASK(DPPIC_GROUP_NUM),
#endif
};
#endif

NRF_TIMER_Type *timer0 = (NRF_TIMER_Type *)DT_REG_ADDR(DT_NODELABEL(dut_timer0));
NRF_TIMER_Type *timer1 = (NRF_TIMER_Type *)DT_REG_ADDR(DT_NODELABEL(dut_timer1));
NRF_TIMER_Type *timer2 = (NRF_TIMER_Type *)DT_REG_ADDR(DT_NODELABEL(dut_timer2));
NRF_LPCOMP_Type *lpcomp = (NRF_LPCOMP_Type *)DT_REG_ADDR(DT_NODELABEL(comp));

/* Setup a single PPI connection TIMER_COMPARE->LPCOMP_START. Use various timers. */
static void test_single_connection(NRF_TIMER_Type *timer)
{
	uint32_t evt = nrf_timer_event_address_get(timer, NRF_TIMER_EVENT_COMPARE0);
	uint32_t tsk = nrf_lpcomp_task_address_get(lpcomp, NRF_LPCOMP_TASK_START);
	gppi_handle_t handle;
	int rv;

	nrf_timer_mode_set(timer, NRF_TIMER_MODE_TIMER);
	nrf_timer_cc_set(timer, NRF_TIMER_CC_CHANNEL0, 100);
	nrf_timer_event_clear(timer, NRF_TIMER_EVENT_COMPARE0);

	nrf_lpcomp_event_clear(lpcomp, NRF_LPCOMP_EVENT_READY);
	nrf_lpcomp_enable(lpcomp);

	rv = gppi_conn_alloc(evt, tsk, &handle);
	zassert_ok(rv);

	/* Enable PPI connection and validate that task-event connection is working. */
	gppi_conn_enable(handle);

	nrf_timer_task_trigger(timer, NRF_TIMER_TASK_START);
	k_busy_wait(1000);

	zassert_equal(nrf_timer_event_check(timer, NRF_TIMER_EVENT_COMPARE0), 1);
	zassert_equal(nrf_lpcomp_event_check(lpcomp, NRF_LPCOMP_EVENT_READY), 1);

	nrf_timer_task_trigger(timer, NRF_TIMER_TASK_STOP);
	nrf_timer_event_clear(timer, NRF_TIMER_EVENT_COMPARE0);
	nrf_timer_task_trigger(timer, NRF_TIMER_TASK_CLEAR);

	nrf_lpcomp_task_trigger(lpcomp, NRF_LPCOMP_TASK_STOP);
	nrf_lpcomp_event_clear(lpcomp, NRF_LPCOMP_EVENT_READY);

	/* Disable PPI to check that task is not triggered. */
	gppi_conn_disable(handle);

	nrf_timer_task_trigger(timer, NRF_TIMER_TASK_START);
	k_busy_wait(1000);

	/* TIMER event is set but LPCOMP is not which means that LPCOMP task START was not
	 * triggered.
	 */
	zassert_equal(nrf_timer_event_check(timer, NRF_TIMER_EVENT_COMPARE0), 1);
	zassert_equal(nrf_lpcomp_event_check(lpcomp, NRF_LPCOMP_EVENT_READY), 0);

	/* Clean up. */
	nrf_timer_task_trigger(timer, NRF_TIMER_TASK_STOP);
	nrf_timer_event_clear(timer, NRF_TIMER_EVENT_COMPARE0);
	nrf_timer_task_trigger(timer, NRF_TIMER_TASK_CLEAR);

	nrf_lpcomp_task_trigger(lpcomp, NRF_LPCOMP_TASK_STOP);
	nrf_lpcomp_disable(lpcomp);

	gppi_conn_free(evt, tsk, handle);
}

ZTEST(ppi, test_basic)
{
	test_single_connection(timer0);
	test_single_connection(timer1);
	test_single_connection(timer2);
}

/* Test is checking that it is possible to attach task to a connection.
 *
 * Connection TIMER0_COMPARE0->LPCOMP_START
 * Attached TIMER1_CAPTURE0
 */
ZTEST(ppi, test_attach_task)
{
	uint32_t evt = nrf_timer_event_address_get(timer0, NRF_TIMER_EVENT_COMPARE0);
	uint32_t tsk = nrf_lpcomp_task_address_get(lpcomp, NRF_LPCOMP_TASK_START);
	uint32_t tsk2 = nrf_timer_task_address_get(timer1, NRF_TIMER_TASK_CAPTURE0);
	gppi_handle_t handle;
	int rv;

	/* Setup TIMER0 TIMER1 in timer mode, set CC0 to 100 on TIMER0 */
	nrf_timer_mode_set(timer0, NRF_TIMER_MODE_TIMER);
	nrf_timer_mode_set(timer1, NRF_TIMER_MODE_TIMER);
	nrf_timer_cc_set(timer0, NRF_TIMER_CC_CHANNEL0, 100);
	nrf_timer_cc_set(timer1, NRF_TIMER_CC_CHANNEL0, 0);
	nrf_timer_event_clear(timer0, NRF_TIMER_EVENT_COMPARE0);

	/* Enable LPCOMP */
	nrf_lpcomp_event_clear(lpcomp, NRF_LPCOMP_EVENT_READY);
	nrf_lpcomp_enable(lpcomp);

	/* Setup PPI connection. */
	rv = gppi_conn_alloc(evt, tsk, &handle);
	zassert_ok(rv);

	/* Attach task to the connection. */
	rv = gppi_ep_attach(handle, tsk2);
	zassert_ok(rv);

	gppi_conn_enable(handle);

	/* Start both timers. */
	nrf_timer_task_trigger(timer0, NRF_TIMER_TASK_START);
	nrf_timer_task_trigger(timer1, NRF_TIMER_TASK_START);

	/* Wait and validate that COMPARE0 event occurred. */
	k_busy_wait(1000);
	zassert_equal(nrf_timer_event_check(timer0, NRF_TIMER_EVENT_COMPARE0), 1);

	/* Validate that PPI connection triggered both tasks (LPCOMP START and TIMER CAPTURE). */
	zassert_equal(nrf_lpcomp_event_check(lpcomp, NRF_LPCOMP_EVENT_READY), 1);
	zassert_true(nrf_timer_cc_get(timer1, NRF_TIMER_CC_CHANNEL0) != 0);

	/* Clean up. */
	gppi_conn_disable(handle);

	nrf_timer_task_trigger(timer0, NRF_TIMER_TASK_STOP);
	nrf_timer_task_trigger(timer0, NRF_TIMER_TASK_CLEAR);
	nrf_timer_event_clear(timer0, NRF_TIMER_EVENT_COMPARE0);
	nrf_timer_task_trigger(timer1, NRF_TIMER_TASK_STOP);
	nrf_timer_task_trigger(timer1, NRF_TIMER_TASK_CLEAR);
	nrf_lpcomp_task_trigger(lpcomp, NRF_LPCOMP_TASK_STOP);
	nrf_lpcomp_disable(lpcomp);

	gppi_ep_clear(tsk2);
	gppi_conn_free(evt, tsk, handle);
}

/* Test is checking that it is possible to attach events to a connection.
 *
 * Connection TIMER0_COMPARE0->TIMER1_COUNT
 * Attached TIMER0_COMPARE1
 */
ZTEST(ppi, test_attach_event)
{
	if (IS_ENABLED(CONFIG_NORDIC_GPPI_PPI)) {
		ztest_test_skip();
	}

	uint32_t evt = nrf_timer_event_address_get(timer0, NRF_TIMER_EVENT_COMPARE0);
	uint32_t evt2 = nrf_timer_event_address_get(timer0, NRF_TIMER_EVENT_COMPARE1);
	uint32_t tsk = nrf_timer_task_address_get(timer1, NRF_TIMER_TASK_COUNT);
	gppi_handle_t handle;
	int rv;

	nrf_timer_cc_set(timer0, NRF_TIMER_CC_CHANNEL0, 100);
	nrf_timer_cc_set(timer0, NRF_TIMER_CC_CHANNEL1, 200);
	nrf_timer_mode_set(timer1, NRF_TIMER_MODE_COUNTER);
	nrf_timer_event_clear(timer0, NRF_TIMER_EVENT_COMPARE0);
	nrf_timer_event_clear(timer0, NRF_TIMER_EVENT_COMPARE1);

	/* Setup  PPI connection. */
	rv = gppi_conn_alloc(evt, tsk, &handle);
	zassert_ok(rv);

	rv = gppi_ep_attach(handle, evt2);
	zassert_ok(rv);

	gppi_conn_enable(handle);

	/* Start timers. */
	nrf_timer_task_trigger(timer1, NRF_TIMER_TASK_START);
	nrf_timer_task_trigger(timer0, NRF_TIMER_TASK_START);

	/* Wait and check that both COMPARE events expired. */
	k_busy_wait(1000);
	zassert_equal(nrf_timer_event_check(timer0, NRF_TIMER_EVENT_COMPARE0), 1);
	zassert_equal(nrf_timer_event_check(timer0, NRF_TIMER_EVENT_COMPARE1), 1);

	/* TIMER1 should be incremented twice by both events. */
	nrf_timer_task_trigger(timer1, NRF_TIMER_TASK_CAPTURE0);
	zassert_equal(nrf_timer_cc_get(timer1, NRF_TIMER_CC_CHANNEL0), 2);

	/* Clean up. */
	gppi_conn_disable(handle);
	nrf_timer_task_trigger(timer0, NRF_TIMER_TASK_STOP);
	nrf_timer_task_trigger(timer0, NRF_TIMER_TASK_CLEAR);
	nrf_timer_event_clear(timer0, NRF_TIMER_EVENT_COMPARE0);
	nrf_timer_event_clear(timer0, NRF_TIMER_EVENT_COMPARE1);
	nrf_timer_task_trigger(timer1, NRF_TIMER_TASK_STOP);
	nrf_timer_task_trigger(timer1, NRF_TIMER_TASK_CLEAR);

	gppi_ep_clear(evt2);
	gppi_conn_free(evt, tsk, handle);
}

/* Test is checking PPI group functionality. Group can contain one or more PPI channel
 * and it has tasks for enabling and disabling all channel in the group.
 *
 * Test is using 2 TIMERs and has following connections:
 *
 * PPI connections that are included in a group:
 * 1a. TIMER0_COMPARE1->TIMER1_COUNT
 * 1b. TIMER0_COMPARE3->TIMER1_COUNT
 *
 * 2. TIMER0_COMPARE0->GROUP_EN
 * 3. TIMER0_COMPARE2->GROUP_DIS
 *
 * Compare channels in TIMER0 are set to 100, 110, 120 and 130.
 *
 * Expected behavior is that first event at 100 will enable the PPI group so that
 * the second compare event (at 110) will increment TIMER1 counter. Next event
 * (compare 2 at 120) will disable the group so that the last compare event (at 130)
 * will NOT increment TIMER1.
 */
ZTEST(ppi, test_group)
{
	uint32_t evt0 = nrf_timer_event_address_get(timer0, NRF_TIMER_EVENT_COMPARE0);
	uint32_t evt1 = nrf_timer_event_address_get(timer0, NRF_TIMER_EVENT_COMPARE1);
	uint32_t evt2 = nrf_timer_event_address_get(timer0, NRF_TIMER_EVENT_COMPARE2);
	uint32_t evt3 = nrf_timer_event_address_get(timer0, NRF_TIMER_EVENT_COMPARE3);
	uint32_t tsk = nrf_timer_task_address_get(timer1, NRF_TIMER_TASK_COUNT);
	uint32_t gtsk_en, gtsk_dis;
	gppi_handle_t handle0;
	gppi_handle_t handle1;
	gppi_handle_t handle2;
	gppi_handle_t handle3;
	gppi_group_handle_t ghandle;
	uint32_t cc;
	int rv;

	nrf_timer_cc_set(timer0, NRF_TIMER_CC_CHANNEL0, 100);
	nrf_timer_cc_set(timer0, NRF_TIMER_CC_CHANNEL1, 110);
	nrf_timer_cc_set(timer0, NRF_TIMER_CC_CHANNEL2, 120);
	nrf_timer_cc_set(timer0, NRF_TIMER_CC_CHANNEL3, 130);
	nrf_timer_mode_set(timer1, NRF_TIMER_MODE_COUNTER);
	nrf_timer_event_clear(timer0, NRF_TIMER_EVENT_COMPARE0);
	nrf_timer_event_clear(timer0, NRF_TIMER_EVENT_COMPARE1);
	nrf_timer_event_clear(timer0, NRF_TIMER_EVENT_COMPARE2);
	nrf_timer_event_clear(timer0, NRF_TIMER_EVENT_COMPARE3);

	/* PPI 1a. TIMER0_CC1->TIMER1_COUNT */
	rv = gppi_conn_alloc(evt1, tsk, &handle0);
	zassert_ok(rv);

	/* Allocate a group and add connection 1 to the group. */
	rv = gppi_group_alloc(&evt1, 1, &ghandle);
	zassert_ok(rv);

	if (IS_ENABLED(CONFIG_NORDIC_GPPI_PPI)) {
		rv = gppi_conn_alloc(evt3, tsk, &handle3);
		zassert_ok(rv);

		rv = gppi_group_ep_add(ghandle, evt3);
		zassert_ok(rv);
	} else {
		/* PPI 1b. TIMER0_CC3->TIMER1_COUNT */
		rv = gppi_ep_attach(handle0, evt3);
		zassert_ok(rv);
	}

	gtsk_en = gppi_group_task_en_addr(ghandle);
	gtsk_dis = gppi_group_task_dis_addr(ghandle);

	/* Allocate PPI 2. TIMER0_CC0->GROUP_EN */
	rv = gppi_conn_alloc(evt0, gtsk_en, &handle1);
	zassert_ok(rv);

	/* Allocate PPI 3. TIMER0_CC2->GROUP_DIS */
	rv = gppi_conn_alloc(evt2, gtsk_dis, &handle2);
	zassert_ok(rv);

	/* Enable connection but then disable the channel in the connection source.
	 * On single domain SoC it is redundant but on multi domain SoC it will enable
	 * channels used for a connection that uses multiple DPPIC and PPIB and disable
	 * the channel only for source. PPI group will then enable it.
	 */
	gppi_conn_enable(handle0);
	gppi_ep_disable(evt1);

	/* Enable PPIs which enables and disables the group. Connection PPI 1 is now disabled. */
	gppi_conn_enable(handle1);
	gppi_conn_enable(handle2);

	/* Start both timers. */
	nrf_timer_task_trigger(timer1, NRF_TIMER_TASK_START);
	nrf_timer_task_trigger(timer1, NRF_TIMER_TASK_CAPTURE0);
	zassert_equal(nrf_timer_cc_get(timer1, NRF_TIMER_CC_CHANNEL0), 0);

	nrf_timer_task_trigger(timer0, NRF_TIMER_TASK_START);

	/* Wait for all COMPARE events to expire. */
	k_busy_wait(1000);

	/* Stop timers and check that all events expired. */
	nrf_timer_task_trigger(timer0, NRF_TIMER_TASK_STOP);
	nrf_timer_task_trigger(timer1, NRF_TIMER_TASK_STOP);
	zassert_equal(nrf_timer_event_check(timer0, NRF_TIMER_EVENT_COMPARE0), 1);
	zassert_equal(nrf_timer_event_check(timer0, NRF_TIMER_EVENT_COMPARE1), 1);
	zassert_equal(nrf_timer_event_check(timer0, NRF_TIMER_EVENT_COMPARE2), 1);
	zassert_equal(nrf_timer_event_check(timer0, NRF_TIMER_EVENT_COMPARE3), 1);

	/* Validate that TIMER1 counter got incremented exactly once. */
	nrf_timer_task_trigger(timer1, NRF_TIMER_TASK_CAPTURE0);
	cc = nrf_timer_cc_get(timer1, NRF_TIMER_CC_CHANNEL0);
	zassert_equal(cc, 1, "Unexpected cc:%d (exp:%d)", cc, 1);

	/* Clean up. */
	gppi_group_dis(ghandle);
	gppi_ep_clear(evt3);
	gppi_conn_disable(handle1);
	gppi_conn_disable(handle2);
	gppi_conn_free(evt1, tsk, handle0);
	gppi_conn_free(evt0, gtsk_en, handle1);
	gppi_conn_free(evt2, gtsk_dis, handle2);
	gppi_group_free(ghandle);
}

ZTEST_SUITE(ppi, NULL, NULL, NULL, NULL, NULL);

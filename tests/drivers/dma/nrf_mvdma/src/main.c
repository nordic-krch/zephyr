/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/dma/dma_nrf_mvdma.h>
#include <zephyr/cache.h>
#include <zephyr/kernel.h>
#include <hal/nrf_gpio.h>

#define DBG_PIN_SET(x) NRF_P9->OUTSET = BIT(x)
#define DBG_PIN_CLR(x) NRF_P9->OUTCLR = BIT(x)

static struct k_sem done;
static void *exp_user_data;

static void mvdma_handler(void *user_data)
{
	DBG_PIN_CLR(1);
	zassert_equal(user_data, exp_user_data);
	k_sem_give(&done);
}

ZTEST(nrf_mvdma, test_memory_copy)
{
	int err;
	uint32_t in_buf[]  __aligned(CONFIG_DCACHE_LINE_SIZE) = {1, 2, 3, 4, 5};
	uint32_t out_buf[ARRAY_SIZE(in_buf)] __aligned(CONFIG_DCACHE_LINE_SIZE);
	void *udata = &err;
	int cache_err = IS_ENABLED(CONFIG_DCACHE) ? 0 : -ENOTSUP;

	const uint32_t source_job[] __aligned(32) = {
		NRF_MVDMA_JOB_DESC(in_buf, sizeof(in_buf), NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_TERMINATE
	};
	const uint32_t sink_job[] __aligned(32) = {
		NRF_MVDMA_JOB_DESC(out_buf, sizeof(out_buf), NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_TERMINATE
	};

	struct nrf_mvdma_jobs_desc job = {
		.source = source_job,
		.source_desc_size = sizeof(source_job),
		.sink = sink_job,
		.sink_desc_size = sizeof(sink_job),
		.handler = mvdma_handler,
		.user_data = udata
	};

	exp_user_data = udata;

	err = sys_cache_data_flush_range(in_buf, sizeof(in_buf));
	zassert_equal(err, cache_err);

	DBG_PIN_SET(0);
	DBG_PIN_CLR(0);
	DBG_PIN_SET(1);
	DBG_PIN_SET(0);
	err = nrf_mvdma_xfer(&job);
	DBG_PIN_CLR(0);
	zassert_equal(err, 0);

	err = k_sem_take(&done, K_MSEC(100));
	zassert_equal(err, 0);

	err = sys_cache_data_invd_range(out_buf, sizeof(out_buf));
	zassert_equal(err, cache_err);

	zassert_equal(memcmp(in_buf, out_buf, sizeof(in_buf)), 0);
}

static void before(void *unused)
{
	nrf_gpio_cfg_output(9*32);
	nrf_gpio_cfg_output(9*32+1);
	nrf_gpio_cfg_output(9*32+2);
	nrf_gpio_cfg_output(9*32+3);
	k_sem_init(&done, 0, 1);
}

ZTEST_SUITE(nrf_mvdma, NULL, NULL, before, NULL, NULL);

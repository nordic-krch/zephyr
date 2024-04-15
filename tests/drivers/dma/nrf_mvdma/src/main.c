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
#include <hal/nrf_uarte.h>
#include <zephyr/linker/devicetree_regions.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>

#define DBG_PIN_SET(x) NRF_P9->OUTSET = BIT(x)
#define DBG_PIN_CLR(x) NRF_P9->OUTCLR = BIT(x)

static struct k_sem done;
static struct k_sem done2;
static void *exp_user_data;

#define SLOW_PERIPH_NODE DT_CHOSEN(zephyr_console)
#define SLOW_PERIPH_MEMORY_SECTION()						\
	COND_CODE_1(DT_NODE_HAS_PROP(SLOW_PERIPH_NODE, memory_regions),		\
		(__attribute__((__section__(LINKER_DT_NODE_REGION_NAME(		\
			DT_PHANDLE(SLOW_PERIPH_NODE, memory_regions)))))),	\
		())

#define BUF_LEN 128
#define REAL_BUF_LEN ROUND_UP(BUF_LEN, CONFIG_DCACHE_LINE_SIZE)

static uint8_t mem_slow_periph[REAL_BUF_LEN] SLOW_PERIPH_MEMORY_SECTION();
static uint8_t mem_default[REAL_BUF_LEN] __aligned(CONFIG_DCACHE_LINE_SIZE);

static void mvdma_handler(void *user_data)
{
	DBG_PIN_CLR(1);
	zassert_equal(user_data, exp_user_data);
	k_sem_give(&done);
}

static void mvdma_handler2(void *user_data)
{
	struct k_sem *sem = user_data;

	k_sem_give(sem);
}

static uint32_t get_ts(void)
{
	return (uint32_t)z_nrf_grtc_timer_read();
}

static void dma_run(const uint32_t *src_desc, size_t src_len,
		    const uint32_t *sink_desc, size_t sink_len)
{
	int err;
	void *udata = &err;
	uint32_t t;
	struct nrf_mvdma_jobs_desc job = {
		.source = src_desc,
		.source_desc_size = src_len,
		.sink = sink_desc,
		.sink_desc_size = sink_len,
		.handler = mvdma_handler,
		.user_data = udata
	};

	exp_user_data = udata;

	t = get_ts();
	DBG_PIN_SET(1);
	DBG_PIN_SET(0);
	err = nrf_mvdma_xfer(&job);
	DBG_PIN_CLR(0);
	uint32_t t_dma_setup = get_ts() - t;
	zassert_equal(err, 0);

	err = k_sem_take(&done, K_MSEC(100));
	t = get_ts() - t;
	zassert_equal(err, 0);
	TC_PRINT("DMA setup took %dus\n", t_dma_setup);
	TC_PRINT("DMA transfer (back to thread) %dus\n", t);
}

static void test_memcpy(void *dst, void *src, size_t len, bool frag_dst, bool frag_src)
{
	int err;
	int cache_err = IS_ENABLED(CONFIG_DCACHE) ? 0 : -ENOTSUP;
	uint32_t t;

	t = get_ts();
	DBG_PIN_SET(1);
	memcpy(dst, src, len);
	DBG_PIN_CLR(1);
	t = get_ts() - t;
	TC_PRINT("DMA transfer for dst:%p%s src:%p%s length:%d\n",
		 dst, frag_dst ? "(fragmented)" : "", src, frag_src ? "(fragmented)" : "",
		 len);
	TC_PRINT("CPU copy took %dus\n", t);

	memset(dst, 0, len);
	for (size_t i = 0; i < len; i++) {
		((uint8_t *)src)[i] = (uint8_t)i;
	}

	uint32_t source_job[] __aligned(CONFIG_DCACHE_LINE_SIZE) = {
		NRF_MVDMA_JOB_DESC(src, len, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_TERMINATE
	};
	uint32_t source_job_frag[] __aligned(CONFIG_DCACHE_LINE_SIZE) = {
		NRF_MVDMA_JOB_DESC(src, len / 2, NRF_MVDMA_ATTR_DEFAULT, 0),
		/* empty tranfer in the middle. */
		NRF_MVDMA_JOB_DESC(1/*dummy addr*/, 0, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_DESC(&((uint8_t *)src)[len / 2], len / 2, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_TERMINATE
	};
	uint32_t sink_job[] __aligned(CONFIG_DCACHE_LINE_SIZE) = {
		NRF_MVDMA_JOB_DESC(dst, len, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_TERMINATE
	};

	uint32_t sink_job_frag[] __aligned(CONFIG_DCACHE_LINE_SIZE) = {
		NRF_MVDMA_JOB_DESC(dst, len / 2, NRF_MVDMA_ATTR_DEFAULT, 0),
		/* empty tranfer in the middle. */
		NRF_MVDMA_JOB_DESC(1/*dummy addr*/, 0, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_DESC(&((uint8_t *)dst)[len / 2], len / 2, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_TERMINATE
	};

	err = sys_cache_data_flush_range(src, len);
	zassert_equal(err, cache_err);

	dma_run(frag_src ? source_job_frag : source_job,
		frag_src ? sizeof(source_job_frag) : sizeof(source_job),
		frag_dst ? sink_job_frag : sink_job,
		frag_dst ? sizeof(sink_job_frag) : sizeof(sink_job));

	err = sys_cache_data_invd_range(dst, len);
	zassert_equal(err, cache_err);

	zassert_equal(memcmp(src, dst, len), 0);
}

static void test_unaligned(uint8_t *dst, uint8_t *src, size_t len,
			   size_t total_dst, size_t offset_dst)
{
	int err;
	int cache_err = IS_ENABLED(CONFIG_DCACHE) ? 0 : -ENOTSUP;

	memset(dst, 0, total_dst);
	for (size_t i = 0; i < len; i++) {
		((uint8_t *)src)[i] = (uint8_t)i;
	}

	const uint32_t source_job[] __aligned(CONFIG_DCACHE_LINE_SIZE) = {
		NRF_MVDMA_JOB_DESC(src, len, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_TERMINATE
	};
	const uint32_t sink_job[] __aligned(CONFIG_DCACHE_LINE_SIZE) = {
		NRF_MVDMA_JOB_DESC(&dst[offset_dst], len, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_TERMINATE
	};

	struct nrf_mvdma_jobs_desc job = {
		.source = source_job,
		.source_desc_size = sizeof(source_job),
		.sink = sink_job,
		.sink_desc_size = sizeof(sink_job),
		.handler = mvdma_handler,
	};

	exp_user_data = NULL;

	err = sys_cache_data_flush_range(src, len);
	zassert_equal(err, cache_err);
	err = sys_cache_data_flush_range(dst, total_dst);
	zassert_equal(err, cache_err);

	err = nrf_mvdma_xfer(&job);
	zassert_equal(err, 0);

	err = k_sem_take(&done, K_MSEC(100));

	err = sys_cache_data_invd_range(dst, total_dst);
	zassert_equal(err, cache_err);

	zassert_equal(memcmp(src, &dst[offset_dst], len), 0);
	for (size_t i = 0; i < offset_dst; i++) {
		zassert_equal(dst[i], 0);
	}
	for (size_t i = offset_dst + len; i < total_dst; i++) {
		zassert_equal(dst[i], 0);
	}
}

ZTEST(nrf_mvdma, test_copy_unaligned)
{
	uint8_t src[4] __aligned(CONFIG_DCACHE_LINE_SIZE) = {0xaa, 0xbb, 0xcc, 0xdd};
	uint8_t dst[CONFIG_DCACHE_LINE_SIZE] __aligned(CONFIG_DCACHE_LINE_SIZE);

	for (int i = 1; i < 4; i++) {
		for (int j = 1; j < 4; j++) {
			test_unaligned(dst, src, i, sizeof(dst), j);
		}
	}
}

ZTEST(nrf_mvdma, test_copy_from_slow_periph_mem)
{
	test_memcpy(mem_default, mem_slow_periph, BUF_LEN, false, false);
	test_memcpy(mem_default, mem_slow_periph, BUF_LEN, true, false);
	test_memcpy(mem_default, mem_slow_periph, BUF_LEN, false, true);
	test_memcpy(mem_default, mem_slow_periph, BUF_LEN, true, true);
	test_memcpy(mem_default, mem_slow_periph, 16, false, false);
}

ZTEST(nrf_mvdma, test_copy_to_slow_periph_mem)
{
	test_memcpy(mem_slow_periph, mem_default, BUF_LEN, false, false);
	test_memcpy(mem_slow_periph, mem_default, 16, false, false);
}

ZTEST(nrf_mvdma, test_memory_copy)
{
	uint8_t in_buf[BUF_LEN]  __aligned(CONFIG_DCACHE_LINE_SIZE);
	uint8_t out_buf[BUF_LEN] __aligned(CONFIG_DCACHE_LINE_SIZE);

	test_memcpy(out_buf, in_buf, sizeof(out_buf), false, false);
}

ZTEST(nrf_mvdma, test_concurrent_jobs)
{
	uint8_t in_buf[BUF_LEN]  __aligned(CONFIG_DCACHE_LINE_SIZE);
	uint8_t out_buf[BUF_LEN] __aligned(CONFIG_DCACHE_LINE_SIZE);
	uint8_t out_buf2[BUF_LEN]  __aligned(CONFIG_DCACHE_LINE_SIZE);
	int cache_err = IS_ENABLED(CONFIG_DCACHE) ? 0 : -ENOTSUP;
	int err;

	memset(out_buf, 0, BUF_LEN);
	memset(out_buf2, 0, BUF_LEN);
	for (size_t i = 0; i < BUF_LEN; i++) {
		out_buf[i] = (uint8_t)i;
		out_buf2[i] = (uint8_t)i + 10;
	}

	const uint32_t source_job_periph_ram[] __aligned(CONFIG_DCACHE_LINE_SIZE)  = {
		NRF_MVDMA_JOB_DESC(out_buf2, BUF_LEN, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_TERMINATE
	};
	const uint32_t sink_job_periph_ram[] __aligned(CONFIG_DCACHE_LINE_SIZE) = {
		NRF_MVDMA_JOB_DESC(mem_slow_periph, BUF_LEN, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_TERMINATE
	};

	struct nrf_mvdma_jobs_desc job = {
		.source = source_job_periph_ram,
		.source_desc_size = sizeof(source_job_periph_ram),
		.sink = sink_job_periph_ram,
		.sink_desc_size = sizeof(sink_job_periph_ram),
		.handler = mvdma_handler2,
		.user_data = &done
	};

	const uint32_t source_job[] __aligned(CONFIG_DCACHE_LINE_SIZE) = {
		NRF_MVDMA_JOB_DESC(out_buf, sizeof(out_buf), NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_TERMINATE
	};
	const uint32_t sink_job[] __aligned(CONFIG_DCACHE_LINE_SIZE) = {
		NRF_MVDMA_JOB_DESC(in_buf, sizeof(in_buf), NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_TERMINATE
	};

	struct nrf_mvdma_jobs_desc job2 = {
		.source = source_job,
		.source_desc_size = sizeof(source_job),
		.sink = sink_job,
		.sink_desc_size = sizeof(sink_job),
		.handler = mvdma_handler2,
		.user_data = &done2
	};

	err = sys_cache_data_flush_range(out_buf, BUF_LEN);
	zassert_equal(err, cache_err);

	err = sys_cache_data_flush_range(out_buf2, BUF_LEN);
	zassert_equal(err, cache_err);

	DBG_PIN_SET(0);
	err = nrf_mvdma_xfer(&job);
	zassert_equal(err, 0);

	err = nrf_mvdma_xfer(&job2);
	zassert_equal(err, 1);

	err = k_sem_take(&done, K_MSEC(100));
	zassert_equal(err, 0);

	k_msleep(100);
	err = k_sem_take(&done2, K_MSEC(100));
	zassert_equal(err, 0);
	DBG_PIN_CLR(0);

	err = sys_cache_data_invd_range(in_buf, BUF_LEN);
	zassert_equal(err, cache_err);

	err = sys_cache_data_invd_range(mem_slow_periph, BUF_LEN);
	zassert_equal(err, cache_err);

	zassert_equal(memcmp(in_buf, out_buf, BUF_LEN), 0);
	zassert_equal(memcmp(mem_slow_periph, out_buf2, BUF_LEN), 0);
}

ZTEST(nrf_mvdma, test_peripheral_operation)
{
#if DT_SAME_NODE(DT_CHOSEN(zephyr_console), DT_NODELABEL(uart135))
#define p_reg NRF_UARTE135
#elif DT_SAME_NODE(DT_CHOSEN(zephyr_console), DT_NODELABEL(uart136))
#define p_reg NRF_UARTE136
#else
#error "Not supported"
#endif
	static const uint8_t zero = 0;
	static const uint32_t evt_err = (uint32_t)&p_reg->EVENTS_ERROR;
	static const uint32_t evt_rxto = (uint32_t)&p_reg->EVENTS_RXTO;
	static const uint32_t evt_endrx = (uint32_t)&p_reg->EVENTS_DMA.RX.END;
	static const uint32_t evt_rxstarted = (uint32_t)&p_reg->EVENTS_DMA.RX.READY;
	static const uint32_t evt_txstopped = (uint32_t)&p_reg->EVENTS_TXSTOPPED;
	static uint32_t evts[8] __aligned(CONFIG_DCACHE_LINE_SIZE);

	static const int len = 4;
	static uint32_t source_job_periph_ram[] __aligned(CONFIG_DCACHE_LINE_SIZE)  = {
		NRF_MVDMA_JOB_DESC(evt_err, len,
				NRF_MVDMA_ATTR_DEFAULT, NRF_MVDMA_EXT_ATTR_PERIPH),
		NRF_MVDMA_JOB_DESC(&zero, len, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_DESC(evt_endrx, len,
				NRF_MVDMA_ATTR_DEFAULT, NRF_MVDMA_EXT_ATTR_PERIPH),
		NRF_MVDMA_JOB_DESC(&zero, len, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_DESC(evt_rxto, len,
				NRF_MVDMA_ATTR_DEFAULT, NRF_MVDMA_EXT_ATTR_PERIPH),
		NRF_MVDMA_JOB_DESC(&zero, len, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_DESC(evt_rxstarted, len,
				NRF_MVDMA_ATTR_DEFAULT, NRF_MVDMA_EXT_ATTR_PERIPH),
		NRF_MVDMA_JOB_DESC(&zero, len, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_DESC(evt_txstopped, len,
				NRF_MVDMA_ATTR_DEFAULT, NRF_MVDMA_EXT_ATTR_PERIPH),
		NRF_MVDMA_JOB_TERMINATE
	};
	static uint32_t sink_job_periph_ram[] __aligned(CONFIG_DCACHE_LINE_SIZE) = {
		NRF_MVDMA_JOB_DESC(&evts[0], len, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_DESC(evt_err, len,
				NRF_MVDMA_ATTR_DEFAULT, NRF_MVDMA_EXT_ATTR_PERIPH),
		NRF_MVDMA_JOB_DESC(&evts[1], len, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_DESC(evt_endrx, len,
				NRF_MVDMA_ATTR_DEFAULT, NRF_MVDMA_EXT_ATTR_PERIPH),
		NRF_MVDMA_JOB_DESC(&evts[2], len, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_DESC(evt_rxto, len,
				NRF_MVDMA_ATTR_DEFAULT, NRF_MVDMA_EXT_ATTR_PERIPH),
		NRF_MVDMA_JOB_DESC(&evts[3], len, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_DESC(evt_rxstarted, len,
				NRF_MVDMA_ATTR_DEFAULT, NRF_MVDMA_EXT_ATTR_PERIPH),
		NRF_MVDMA_JOB_DESC(&evts[4], len, NRF_MVDMA_ATTR_DEFAULT, 0),
		NRF_MVDMA_JOB_TERMINATE
	};

	TC_PRINT("Reading and clearing UARTE events.");
	dma_run(source_job_periph_ram, sizeof(source_job_periph_ram),
		sink_job_periph_ram, sizeof(sink_job_periph_ram));
	for (int i = 0; i < 8; i++) {
		printk("evt%d:%d\n", i, evts[i]);
	}
}

static void before(void *unused)
{
	nrf_gpio_cfg_output(9*32);
	nrf_gpio_cfg_output(9*32+1);
	nrf_gpio_cfg_output(9*32+2);
	nrf_gpio_cfg_output(9*32+3);
	k_sem_init(&done, 0, 1);
	k_sem_init(&done2, 0, 1);
}

ZTEST_SUITE(nrf_mvdma, NULL, NULL, before, NULL, NULL);

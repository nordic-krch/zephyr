/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/dma/dma_nrf_mvdma.h>
#include <hal/nrf_cache.h>
#include <hal/nrf_mvdma.h>
#include <zephyr/cache.h>

#define CH_CNT 8

static atomic_t alloc_mask = BIT_MASK(CH_CNT);

struct mvdma_handler {
	nrf_mvdma_handler_t handler;
	void *user_data;
};

static const void *sources[CH_CNT] __aligned(CONFIG_DCACHE_LINE_SIZE);
static const void *sinks[CH_CNT] __aligned(CONFIG_DCACHE_LINE_SIZE);
struct mvdma_handler handlers[CH_CNT];

#define EARLY_EXIT 0
#define USE_CACHE_DRV 1
static inline void wait_for_cache(void)
{
	if (USE_CACHE_DRV) {
		return;
	}

	while (nrf_cache_busy_check(NRF_DCACHE)) {
		/* empty */
	}
}

static void cache_flush(const void *addr, size_t size)
{
	if (size == 0) {
		return;
	}

	if (USE_CACHE_DRV) {
		sys_cache_data_flush_range(addr, size);
		return;
	}

	uintptr_t line_addr = BIT(28) | (uintptr_t)addr;
	uintptr_t end_addr = line_addr + size;

	line_addr &= ~(CONFIG_DCACHE_LINE_SIZE - 1);

	do {
		bool cont;

		wait_for_cache();

		do {
			nrf_cache_lineaddr_set(NRF_DCACHE, line_addr);
			nrf_cache_task_trigger(NRF_DCACHE, NRF_CACHE_TASK_FLUSHLINE);

			if (nrf_cache_lineaddr_get(NRF_DCACHE) == line_addr) {
				cont = false;
			} else {
				cont = true;
				wait_for_cache();
			}
		} while (cont);
		line_addr += CONFIG_DCACHE_LINE_SIZE;
	} while (line_addr < end_addr);

	if (EARLY_EXIT) {
		return;
	}
	wait_for_cache();
}

static int flag_alloc(atomic_t *mask)
{
	int idx;
	uint32_t new_mask, prev_mask;

	do {
		prev_mask = *mask;
		if (prev_mask == 0)
		{
			return -ENOMEM;
		}
		else
		{
			idx = __builtin_ctz(prev_mask);
		}

		new_mask = prev_mask & ~BIT(idx);
	} while (!atomic_cas(mask, prev_mask, new_mask));

	return idx;
}

void flag_free(atomic_t *mask, int flag)
{
	uint32_t new_mask, prev_mask;

	if ((BIT(flag) & *mask))
	{
		return;
	}

	do {
		prev_mask = *mask;
		new_mask = prev_mask | BIT(flag);
	} while (!atomic_cas(mask, prev_mask, new_mask));
}

int nrf_mvdma_xfer(const struct nrf_mvdma_jobs_desc *jobs_desc)
{
	int rv;

	rv = flag_alloc(&alloc_mask);
	if (rv < 0) {
		return rv;
	}

	cache_flush(jobs_desc->source, jobs_desc->source_desc_size);
	sources[rv] = jobs_desc->source;
	cache_flush(&sources[rv], sizeof(sources[rv]));

	cache_flush(jobs_desc->sink, jobs_desc->sink_desc_size);
	sinks[rv] = jobs_desc->sink;

	cache_flush(&sinks[rv], sizeof(sinks[rv]));

	handlers[rv].handler = jobs_desc->handler;
	handlers[rv].user_data  = jobs_desc->user_data;

	if (EARLY_EXIT) {
		wait_for_cache();
	}

	NRF_MVDMA->TASKS_START[rv] = 1;

	return 0;
}

static void error_handler(void)
{
	nrf_mvdma_event_clear(NRF_MVDMA, NRF_MVDMA_EVENT_SOURCEBUSERROR);
	nrf_mvdma_event_clear(NRF_MVDMA, NRF_MVDMA_EVENT_SINKBUSERROR);

	printk("dma err\n");
}

static void ch_handler(uint32_t ch)
{
	nrf_mvdma_handler_t handler;
	void *user_data;

	NRF_MVDMA->EVENTS_COMPLETED[ch] = 0;
	handler = handlers[ch].handler;
	user_data = handlers[ch].user_data;
	flag_free(&alloc_mask, ch);

	handler(user_data);
}

static void mvdma_isr(const void *arg)
{
	/*printk("is %08x\n", NRF_MVDMA->INTPEND);*/

	uint32_t ints = nrf_mvdma_int_pending_get(NRF_MVDMA);

	while (ints) {
		uint32_t i = __builtin_ctz(ints);

		ints &= ~BIT(i);

		if (i < 8) {
			error_handler();
		} else {
			ch_handler(i - 8);
		}
	}
}

static int nrf_mvdma_init(void)
{
	/* completed and bus errors. */
	NRF_MVDMA->INTENSET = (BIT_MASK(CH_CNT) << 8) | BIT(4) | BIT(6);
	/* Multimode. */
	NRF_MVDMA->CONFIG.MODE = BIT(0);
	NRF_MVDMA->SOURCE.LISTPTR = (uint32_t)sources;
	NRF_MVDMA->SINK.LISTPTR = (uint32_t)sinks;

	IRQ_CONNECT(MVDMA_IRQn, 2, mvdma_isr, 0, 0);
	irq_enable(MVDMA_IRQn);

	return 0;
}

SYS_INIT(nrf_mvdma_init, PRE_KERNEL_1, 0);

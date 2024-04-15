/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/dma/dma_nrf_mvdma.h>
#include <hal/nrf_cache.h>
#include <hal/nrf_mvdma.h>
#include <zephyr/cache.h>

#define DBG_PIN_SET(x) NRF_P9->OUTSET = BIT(x)
#define DBG_PIN_CLR(x) NRF_P9->OUTCLR = BIT(x)
static atomic_t alloc_mask = CH_CNT == 1 ? 0 : BIT_MASK(CH_CNT);

struct mvdma_handler {
	nrf_mvdma_handler_t handler;
	void *user_data;
};

static uint32_t sources[CH_CNT] __aligned(CONFIG_DCACHE_LINE_SIZE);
static uint32_t sinks[CH_CNT] __aligned(CONFIG_DCACHE_LINE_SIZE);
static struct mvdma_handler handlers[CH_CNT];
static sys_slist_t list;

static int flag_alloc(atomic_t *mask)
{
	int idx;
	uint32_t new_mask, prev_mask;

	if (CH_CNT == 1) {
		return atomic_cas(&alloc_mask, 0, 1) ? 0 : -ENOMEM;
	}

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

	if (CH_CNT == 1) {
		atomic_set(&alloc_mask, 0);
		return;
	}

	if ((BIT(flag) & *mask))
	{
		return;
	}

	do {
		prev_mask = *mask;
		new_mask = prev_mask | BIT(flag);
	} while (!atomic_cas(mask, prev_mask, new_mask));
}

static inline void set_desc(volatile uint32_t *dst, const uint32_t *desc, size_t len)
{
	sys_cache_data_flush_range((void *)desc, len);

	*dst = (uint32_t)desc;

	if (CH_CNT > 1) {
		sys_cache_data_flush_range((void *)dst, sizeof(*dst));
	}
}

int nrf_mvdma_xfer(struct nrf_mvdma_jobs_desc *jobs_desc)
{
	int rv;

	if (CH_CNT == 1) {
		int k = irq_lock();

		if (alloc_mask) {
			sys_slist_append(&list, &jobs_desc->node);
			irq_unlock(k);
			return 1;
		} else {
			rv = 0;
			alloc_mask = 1;
			irq_unlock(k);
		}
	} else {
		rv = flag_alloc(&alloc_mask);
	}

	set_desc(CH_CNT == 1 ? &NRF_MVDMA->SOURCE.LISTPTR : &sources[rv],
		 jobs_desc->source, jobs_desc->source_desc_size);
	set_desc(CH_CNT == 1 ? &NRF_MVDMA->SINK.LISTPTR : &sinks[rv],
		 jobs_desc->sink, jobs_desc->sink_desc_size);

	handlers[rv].handler = jobs_desc->handler;
	handlers[rv].user_data  = jobs_desc->user_data;

	nrf_mvdma_task_t task = NRF_MVDMA_TASK_START0 + (rv * sizeof(uint32_t));
	nrf_mvdma_task_trigger(NRF_MVDMA, task);

	return 0;
}

static void error_handler(void)
{
	nrf_mvdma_event_clear(NRF_MVDMA, NRF_MVDMA_EVENT_SOURCEBUSERROR);
	nrf_mvdma_event_clear(NRF_MVDMA, NRF_MVDMA_EVENT_SINKBUSERROR);
	printk("error");
}

static void ch_handler(uint32_t ch)
{
	nrf_mvdma_handler_t handler;
	void *user_data;

	handler = handlers[ch].handler;
	user_data = handlers[ch].user_data;
	if (CH_CNT == 1) {
		int k = irq_lock();
		sys_snode_t *node = sys_slist_get(&list);

		alloc_mask = 0;
		if (node) {
			struct nrf_mvdma_jobs_desc *desc =
				CONTAINER_OF(node, struct nrf_mvdma_jobs_desc, node);

			irq_unlock(k);
			(void)nrf_mvdma_xfer(desc);
		} else {
			irq_unlock(k);
		}
	} else {
		flag_free(&alloc_mask, ch);
	}

	handler(user_data);
}

static void mvdma_isr(const void *arg)
{
	uint32_t ints = nrf_mvdma_int_pending_get(NRF_MVDMA);

	if (CH_CNT == 1) {
		DBG_PIN_SET(0);
		if (ints & NRF_MVDMA_INT_END_MASK) {
			nrf_mvdma_event_clear(NRF_MVDMA, NRF_MVDMA_EVENT_END);
			ch_handler(0);
		}

		if (ints &
		   (NRF_MVDMA_INT_SOURCEBUSERROR_MASK | NRF_MVDMA_EVENT_SINKBUSERROR)) {
			error_handler();
		}
		DBG_PIN_CLR(0);
		return;
	}

	while (ints) {
		uint32_t i = __builtin_ctz(ints);

		ints &= ~BIT(i);

		if (i < 8) {
			error_handler();
		} else {
			NRF_MVDMA->EVENTS_COMPLETED[i] = 0;
			ch_handler(i - 8);
		}
	}
}

static int nrf_mvdma_init(void)
{
	/* completed and bus errors. */
	uint32_t int_mask = 0x50 | (CH_CNT == 1 ? 0x1 : (BIT_MASK(CH_CNT) << 8));

	nrf_mvdma_int_enable(NRF_MVDMA, int_mask);

	if (CH_CNT > 1) {
		nrf_mvdma_mode_set(NRF_MVDMA, NRF_MVDMA_MODE_MULTI);
		nrf_mvdma_source_list_ptr_set(NRF_MVDMA, (nrf_vdma_job_t *)sources);
		nrf_mvdma_sink_list_ptr_set(NRF_MVDMA, (nrf_vdma_job_t *)sinks);
	}

	IRQ_CONNECT(MVDMA_IRQn, 2, mvdma_isr, 0, 0);
	irq_enable(MVDMA_IRQn);

	sys_slist_init(&list);

	return 0;
}

SYS_INIT(nrf_mvdma_init, PRE_KERNEL_1, 0);

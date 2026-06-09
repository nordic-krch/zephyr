/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/stats/stats.h>
#include <inttypes.h>

#if defined(CONFIG_STATS)

static int stats_print_cb(struct stats_hdr *hdr, void *arg, const char *name, uint16_t off)
{
	ARG_UNUSED(arg);
	void *addr = (uint8_t *)hdr + off;
	uint64_t val = 0;

	switch (hdr->s_size) {
	case sizeof(uint16_t):
		val = *(uint16_t *)(addr);
		break;
	case sizeof(uint32_t):
		val = *(uint32_t *)(addr);
		break;
	case sizeof(uint64_t):
		val = *(uint64_t *)(addr);
		break;
	}

	printk("\t%s (offset: %u, addr: %p): %" PRIu64 "\n", name, off, addr, val);
	return 0;
}

static int stats_group_print_cb(struct stats_hdr *hdr, void *arg)
{
	ARG_UNUSED(arg);

	printk("Stats Group %s (hdr addr: %p)\n", hdr->s_name, (void *)hdr);
	return stats_walk(hdr, stats_print_cb, NULL);
}

void stats_print_all(void)
{
	stats_group_walk(stats_group_print_cb, NULL);
}

#endif /* CONFIG_STATS */

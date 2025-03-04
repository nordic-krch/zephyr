/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include "common.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(remote, LOG_LEVEL_INF);

#ifdef CONFIG_BOARD_NRF54H20DK_NRF54H20_CPURAD

#define TX_BUF_NODE DT_NODELABEL(cpurad_cpuapp_ipc_shm)
#define RX_BUF_NODE DT_NODELABEL(cpuapp_cpurad_ipc_shm)

#elif defined(CONFIG_BOARD_NRF54H20DK_NRF54H20_CPUPPR)

#define TX_BUF_NODE DT_NODELABEL(cpuppr_cpuapp_ipc_shm)
#define RX_BUF_NODE DT_NODELABEL(cpuapp_cpuppr_ipc_shm)

#elif defined(CONFIG_BOARD_NRF54L15DK_NRF54L15_CPUFLPR)

#define TX_BUF_NODE DT_NODELABEL(sram_tx)
#define RX_BUF_NODE DT_NODELABEL(sram_rx)

#else
#error "Unsupported board"
#endif

static const uint32_t tx_buf_size = DT_REG_SIZE(TX_BUF_NODE);
static const uint32_t rx_buf_size = DT_REG_SIZE(RX_BUF_NODE);
static uint32_t *tx_buf = (uint32_t *)(DT_REG_ADDR(TX_BUF_NODE));
static uint32_t *rx_buf = (uint32_t *)(DT_REG_ADDR(RX_BUF_NODE));

static struct shmpsc_pbuf *txb;
static struct shmpsc_pbuf *rxb;
static struct k_spinlock lock;
static uint32_t rx_cnt;
static uint32_t tx_cnt;

static bool pkt_start(union test_item *item, uint32_t len)
{
	ARG_UNUSED(item);
	ARG_UNUSED(len);

	struct test_hdr *hdr;
	int wdog_cnt = 100;

	do {
		hdr = (struct test_hdr *)shmpsc_pbuf_alloc(txb, sizeof(hdr));
		if (hdr) {
			hdr->type = PKT_TYPE_TEST_START;
			shmpsc_pbuf_commit(txb, (uint32_t *)hdr);
			break;
		} else {
			wdog_cnt--;
			k_msleep(1);
		}
	} while (wdog_cnt);

	rx_cnt = 0;

	return (wdog_cnt > 0);
}

static bool pkt(union test_item *item, uint32_t len)
{
	rx_cnt++;
	if (item->data.hdr.rsp) {
		struct test_data *rsp;

		do {
			rsp = (struct test_data *)shmpsc_pbuf_alloc(txb, len);
			if (rsp) {
				break;
			}
			k_sleep(K_USEC(100));
		} while (1);

		memcpy((void *)rsp, (void *)item, len);
		shmpsc_pbuf_commit(txb, (uint32_t *)rsp);
	}
	return true;
}

static bool pkt_end(union test_item *item, uint32_t len)
{
	struct test_hdr *rsp;

	printk("end rv:%p len:%d", item, len);
	do {
		rsp = (struct test_hdr *)shmpsc_pbuf_alloc(txb, sizeof(struct test_hdr));
		if (rsp) {
			break;
		}
		k_sleep(K_USEC(100));
	} while (1);

	rsp->type = PKT_TYPE_TEST_END;
	rsp->rsp = 0;
	rsp->data = rx_cnt;

	return true;
}

static void handle_rx(void)
{
	union test_item *item;
	uint32_t len;
	bool cont = true;

	do {
		item = (union test_item *)shmpsc_pbuf_claim(rxb, &len, &tx_cnt);
		if (item) {
			switch (item->generic.type) {
			case PKT_TYPE_TEST_START:
				cont = pkt_start(item, len);
				break;
			case PKT_TYPE_TEST_PKT:
				cont = pkt(item, len);
				break;
			case PKT_TYPE_TEST_END:
				LOG_ERR("end");
				cont = pkt_end(item, len);
				break;
			default:
				LOG_ERR("unexpected");
				cont = false;
				break;
			}
			shmpsc_pbuf_free(rxb, len);
		} else {
			k_sleep(K_USEC(200));
		}
	} while (cont);
}

int main(void)
{
	int ret;

	k_msleep(20);
	ret = sync();
	if (ret < 0) {
		printk("sync failed\n");
		LOG_ERR("sync failed");
		return 0;
	}

	txb = buffer_init(&lock, tx_buf, tx_buf_size, true);
	if (!txb) {
		LOG_ERR("tx buffer init failed");
		return -1;
	}

	rxb = buffer_init(NULL, rx_buf, rx_buf_size, false);
	LOG_WRN("init done");
	if (!rxb) {
		LOG_ERR("rx buffer init failed");
		return -1;
	}


	handle_rx();

	printk("End of test\n");
	return 0;
}

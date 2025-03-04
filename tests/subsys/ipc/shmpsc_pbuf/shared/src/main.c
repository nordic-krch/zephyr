/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ztest.h>
#include <zephyr/ztress.h>
#include <zephyr/random/random.h>

#if defined(CONFIG_SOC_NRF5340_CPUAPP)
#include <nrf53_cpunet_mgmt.h>
#endif
#include <string.h>

#include "common.h"

#include <zephyr/cache.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(host, LOG_LEVEL_INF);

#ifdef CONFIG_BOARD_NRF54H20DK_NRF54H20_CPUAPP
#if 0 //vpr enabled
#define TX_BUF_NODE DT_NODELABEL(cpuapp_cpuppr_ipc_shm)
#define RX_BUF_NODE DT_NODELABEL(cpuppr_cpuapp_ipc_shm)
#else
#define TX_BUF_NODE DT_NODELABEL(cpuapp_cpurad_ipc_shm)
#define RX_BUF_NODE DT_NODELABEL(cpurad_cpuapp_ipc_shm)
#endif
#elif defined(CONFIG_BOARD_NRF54L15DK_NRF54L15_CPUAPP)
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

static void send_start(void)
{
	struct test_hdr *hdr;
	int wdog_cnt = 200;
	uint32_t len;
	bool rsp = false;

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

	zassert_true(wdog_cnt > 0);

	do {
		hdr = (struct test_hdr *)shmpsc_pbuf_claim(rxb, &len);
		if (hdr) {
			if (hdr->type == PKT_TYPE_TEST_START) {
				rsp = true;
			}
			shmpsc_pbuf_free(rxb, len);
			break;
		} else {
			wdog_cnt--;
			k_msleep(1);
		}
	} while (wdog_cnt);


	zassert_true(wdog_cnt > 0);
}

static void ping_pong(uint32_t rpt, uint32_t len)
{
	struct test_data *pkt;
	uint32_t dlen = len - offsetof(struct test_data, data);
	uint32_t rlen;
	int wdog_cnt;

	do {
		wdog_cnt = 200;
		do {
			pkt = (struct test_data *)shmpsc_pbuf_alloc(txb, len);
			if (pkt) {
				break;
			}
			k_sleep(K_USEC(100));
		} while (wdog_cnt--);

		uintptr_t start = (uintptr_t)txb->buf;
		uintptr_t end = start + 4 * txb->size;

		if (wdog_cnt < 2) LOG_ERR("er");
		zassert_true(wdog_cnt > 1);
		zassert_true((uintptr_t)pkt >= start && (uintptr_t)pkt < end,
				"pkt:%p start:%p end:%p", pkt, start, end);
		pkt->hdr.type = PKT_TYPE_TEST_PKT;
		pkt->hdr.rsp = 1;
		pkt->hdr.data = rpt;
		for (int i = 0; i < dlen; i++) {
			pkt->data[i] = rpt + i;
		}
		shmpsc_pbuf_commit(txb, (void *)pkt);

		wdog_cnt = 200;
		do {
			pkt = (struct test_data *)shmpsc_pbuf_claim(rxb, &rlen);
			if (pkt) {
				break;
			}
			k_sleep(K_USEC(100));
		} while (wdog_cnt--);

		/*printk("rlen:%d, head:%08x (exp:%08x)\n", rlen, pkt->hdr.data, rpt);*/
		zassert_true(wdog_cnt > 1);
		zassert_true((uintptr_t)pkt >= (uintptr_t)rxb->buf &&
			     (uintptr_t)pkt < ((uintptr_t)rxb->buf + 4 * rxb->size));
		zassert_equal(rlen, len);
		zassert_equal(pkt->hdr.type, PKT_TYPE_TEST_PKT);
		zassert_equal(pkt->hdr.rsp, 1);
		if (pkt->hdr.data != rpt) {
			printk("rpt: %d data:%08x\n", rpt, pkt->hdr.data);
			for (int i = 0; i < dlen; i++) {
				printk("data%d:%02x\n", i, pkt->data[i]);
			}
		}
		zassert_equal(pkt->hdr.data, rpt, "Exp:%08x got:%08x", rpt, pkt->hdr.data);
		for (int i = 0; i < dlen; i++) {
			if (pkt->data[i] != (uint8_t)(rpt + i)) {
				LOG_HEXDUMP_INF(pkt->data, dlen, "pkt");
			}

			zassert_equal(pkt->data[i], (uint8_t)(rpt + i),
				"Unexpected byte %d: exp:%02x got:%08x",
				i, rpt + 1, *(uint32_t *)&pkt->data[i]);

		}
		shmpsc_pbuf_free(rxb, rlen);
	} while (rpt--);
}


ZTEST(_shmpsc_pbuf_shared, test_basic)
{
	send_start();
	ping_pong(100, 30);
	ping_pong(100, 10);
	ping_pong(100, 40);
}

struct ctx_data {
	uint8_t prod_idx;
	uint8_t cons_idx;
};

struct stress_test_data {
	struct shmpsc_pbuf *tx_pb;
	struct shmpsc_pbuf *rx_pb;
	struct ctx_data ctx[4];
};

static bool consume(void *user_data, uint32_t cnt, bool last, int prio)
{
	struct stress_test_data *data = user_data;
	struct shmpsc_pbuf *pb = data->rx_pb;
	struct ctx_data *ctx;
	uint32_t rpt = sys_rand32_get() & 0x3;
	struct test_data *pkt;
	uint32_t len;
	uint32_t src;
	uint8_t pkt_cnt;

	while (rpt) {
		pkt = (struct test_data *)shmpsc_pbuf_claim(pb, &len);
		if (!pkt) {
			return true;
		}
		src = pkt->hdr.data >> 8;
		pkt_cnt = pkt->hdr.data & 0xff;
		ctx = &data->ctx[src];
		zassert_equal(pkt_cnt, ctx->cons_idx);
		ctx->cons_idx++;
		for (int i = 0; i < len - sizeof(struct test_hdr); i++) {
			zassert_equal(pkt->data[i], src + i);
		}
		shmpsc_pbuf_free(pb, len);
		rpt--;
	}

	return true;
}

static bool produce(void *user_data, uint32_t cnt, bool last, int prio)
{
	struct stress_test_data *data = user_data;
	struct shmpsc_pbuf *pb = data->tx_pb;
	struct ctx_data *ctx = &data->ctx[prio];
	uint32_t len = sys_rand32_get() % (pb->size / 4) + 1 + sizeof(struct test_hdr);
	struct test_data *pkt;

	pkt = (struct test_data *)shmpsc_pbuf_alloc(pb, len);
	if (!pkt) {
		return true;
	}

	pkt->hdr.type = PKT_TYPE_TEST_PKT;
	pkt->hdr.rsp = 1;
	pkt->hdr.data = ctx->prod_idx | (prio << 8);
	ctx->prod_idx++;
	for (int i = 0; i < len - sizeof(struct test_hdr); i++) {
		pkt->data[i] = prio + i;
	}

	shmpsc_pbuf_commit(pb, (uint32_t *)pkt);
	return true;
}

static void stress_test(ztress_handler h1,
			ztress_handler h2,
			ztress_handler h3,
			ztress_handler h4, uint32_t timeout_ms)
{
	struct stress_test_data data;
	uint32_t preempt_max = INT32_MAX;
	k_timeout_t t = Z_TIMEOUT_TICKS(20);

	if (CONFIG_SYS_CLOCK_TICKS_PER_SEC < 10000) {
		ztest_test_skip();
	}

	ztress_set_timeout(K_MSEC(timeout_ms));
	memset(&data, 0, sizeof(data));
	data.rx_pb = rxb;
	data.tx_pb = txb;

	ZTRESS_EXECUTE(ZTRESS_THREAD(h1,  &data, 0, 0, t),
			ZTRESS_THREAD(h2, &data, 0, preempt_max, t),
			ZTRESS_THREAD(h2, &data, 0, preempt_max, t),
			ZTRESS_THREAD(h3, &data, 0, preempt_max, t));

	/* Consume all responses. */
	for (int i = 0; i < 300; i++) {
		consume(&data, 0 , false, 0);
	}
}

ZTEST(_shmpsc_pbuf_shared, test_stress_low_consume)
{
	stress_test(produce, produce, produce, consume, 10000);
}

ZTEST(_shmpsc_pbuf_shared, test_stress_mid_consume)
{
	stress_test(produce, produce, consume, produce, 10000);
}

ZTEST(_shmpsc_pbuf_shared, test_stress_high_consume)
{
	stress_test(consume, produce, produce, produce,10000);
}


void *setup(void)
{
	int ret;

	k_msleep(10);
	ret = sync();
	zassert_equal(ret, 0);

#if defined(CONFIG_SOC_NRF5340_CPUAPP)
	LOG_INF("Run network core");
	nrf53_cpunet_enable(true);
#endif
	printk("host tx_buf:%p rx_buf:%p\n", tx_buf, rx_buf);
	printk("tx_size:%d rx_size:%d\n", tx_buf_size, rx_buf_size);

	txb = buffer_init(&lock, tx_buf, tx_buf_size, true);
	zassert_not_null(txb);
	rxb = buffer_init(NULL, rx_buf, rx_buf_size, false);
	zassert_not_null(rxb);
	LOG_ERR("rx pb:%p buf:%p %d", rxb, (void *)rxb->buf, rxb->size);

	return NULL;
}

ZTEST_SUITE(_shmpsc_pbuf_shared, NULL, setup, NULL, NULL, NULL);

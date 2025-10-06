/*
 * Copyright (c) 2019 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/sys/printk.h>
#include <hal/nrf_timer.h>
#include <zephyr/drivers/misc/ppi/nrfx_dppi.h>

#define DELAY 20000
#define ALARM_CHANNEL_ID 0

struct counter_alarm_cfg alarm_cfg;

#if defined(CONFIG_BOARD_SAMD20_XPRO)
#define TIMER DT_NODELABEL(tc4)
#elif defined(CONFIG_SOC_FAMILY_ATMEL_SAM)
#define TIMER DT_NODELABEL(tc0)
#elif defined(CONFIG_COUNTER_MICROCHIP_MCP7940N)
#define TIMER DT_NODELABEL(extrtc0)
#elif defined(CONFIG_COUNTER_NRF_RTC)
#define TIMER DT_NODELABEL(rtc0)
#elif defined(CONFIG_COUNTER_NRF_TIMER)
#define TIMER DT_CHOSEN(counter)
#elif defined(CONFIG_COUNTER_TIMER_STM32)
#define TIMER DT_INST(0, st_stm32_counter)
#elif defined(CONFIG_COUNTER_RTC_STM32)
#define TIMER DT_INST(0, st_stm32_rtc)
#elif defined(CONFIG_COUNTER_SMARTBOND_TIMER)
#define TIMER DT_NODELABEL(timer3)
#elif defined(CONFIG_COUNTER_NATIVE_SIM)
#define TIMER DT_NODELABEL(counter0)
#elif defined(CONFIG_COUNTER_XLNX_AXI_TIMER)
#define TIMER DT_INST(0, xlnx_xps_timer_1_00_a)
#elif defined(CONFIG_COUNTER_TMR_ESP32)
#define TIMER DT_INST(0, espressif_esp32_counter)
#elif defined(CONFIG_COUNTER_MCUX_CTIMER)
#define TIMER DT_NODELABEL(ctimer0)
#elif defined(CONFIG_COUNTER_MSPM0_TIMER)
#define TIMER DT_ALIAS(counter)
#elif defined(CONFIG_COUNTER_NXP_S32_SYS_TIMER)
#define TIMER DT_NODELABEL(stm0)
#elif defined(CONFIG_COUNTER_TIMER_GD32)
#define TIMER DT_NODELABEL(timer0)
#elif defined(CONFIG_COUNTER_GECKO_RTCC)
#define TIMER DT_NODELABEL(rtcc0)
#elif defined(CONFIG_COUNTER_GECKO_STIMER)
#define TIMER DT_NODELABEL(stimer0)
#elif defined(CONFIG_COUNTER_INFINEON_CAT1) || defined(CONFIG_COUNTER_INFINEON_TCPWM)
#define TIMER DT_NODELABEL(counter0_0)
#elif defined(CONFIG_COUNTER_AMBIQ)
#ifdef TIMER
#undef TIMER
#endif
#define TIMER DT_NODELABEL(counter0)
#elif defined(CONFIG_COUNTER_SNPS_DW)
#define TIMER DT_NODELABEL(timer0)
#elif defined(CONFIG_COUNTER_TIMER_RPI_PICO)
#ifdef CONFIG_SOC_SERIES_RP2040
#define TIMER DT_NODELABEL(timer)
#elif CONFIG_SOC_SERIES_RP2350
#define TIMER DT_NODELABEL(timer0)
#endif
#elif defined(CONFIG_COUNTER_TIMER_MAX32)
#define TIMER DT_NODELABEL(counter0)
#elif defined(CONFIG_COUNTER_RA_AGT)
#define TIMER DT_NODELABEL(counter0)
#elif defined(CONFIG_COUNTER_RENESAS_RZ_GTM)
#define TIMER DT_INST(0, renesas_rz_gtm_counter)
#elif defined(CONFIG_COUNTER_CC23X0_RTC)
#define TIMER DT_NODELABEL(rtc0)
#elif defined(CONFIG_COUNTER_RENESAS_RZ_CMTW)
#define TIMER DT_INST(0, renesas_rz_cmtw_counter)
#else
#error Unable to find a counter device node in devicetree
#endif

static NRF_TIMER_Type *const ppi_timer = (NRF_TIMER_Type *)DT_REG_ADDR(DT_NODELABEL(ppi_timer));
static NRF_TIMER_Type *const alarm_timer = (NRF_TIMER_Type *)DT_REG_ADDR(TIMER);

static void test_counter_interrupt_fn(const struct device *counter_dev,
				      uint8_t chan_id, uint32_t ticks,
				      void *user_data)
{
	nrf_timer_task_trigger(ppi_timer, NRF_TIMER_TASK_CAPTURE3);
	struct counter_alarm_cfg *config = user_data;
	uint32_t now_ticks;
	uint64_t now_usec;
	int now_sec;
	int err;

	err = counter_get_value(counter_dev, &now_ticks);
	if (!counter_is_counting_up(counter_dev)) {
		now_ticks = counter_get_top_value(counter_dev) - now_ticks;
	}

	if (err) {
		printk("Failed to read counter value (err %d)", err);
		return;
	}

	now_usec = counter_ticks_to_us(counter_dev, now_ticks);
	now_sec = (int)(now_usec / USEC_PER_SEC);

	printk("!!! Alarm !!!\n");
	printk("Now: %u\n", now_sec);

	/* Set a new alarm with a double length duration */
	config->ticks = config->ticks * 2U;

	printk("Set alarm in %u sec (%u ticks)\n",
	       (uint32_t)(counter_ticks_to_us(counter_dev,
					   config->ticks) / USEC_PER_SEC),
	       config->ticks);

	return;

	err = counter_set_channel_alarm(counter_dev, ALARM_CHANNEL_ID,
					user_data);
	if (err != 0) {
		printk("Alarm could not be set\n");
	}
}

int main(void)
{
	const struct device *const counter_dev = DEVICE_DT_GET(TIMER);
	int err;
	uint32_t handle;
	int rv;
	uint32_t evt = nrf_timer_event_address_get(alarm_timer, NRF_TIMER_EVENT_COMPARE2);
	uint32_t tsk = nrf_timer_task_address_get(ppi_timer, NRF_TIMER_TASK_CAPTURE0);

	k_msleep(10);

	/**(volatile uint32_t *)((uint32_t)NRF_NVMC+0x700) = 1;*/
	/*NRF_RRAMC->POWER.LOWPOWERCONFIG=1;*/
	ppi_timer->PRESCALER=0;
	nrf_timer_bit_width_set(ppi_timer, NRF_TIMER_BIT_WIDTH_32);
	nrf_timer_task_trigger(ppi_timer, NRF_TIMER_TASK_START);
	rv = nrf_dppi_conn_alloc(evt, tsk, &handle);
	if (rv < 0) {
		printk("failed\n");
	}
	nrf_dppi_conn_enable(handle);
	printk("Counter alarm sample\n\n");

	if (!device_is_ready(counter_dev)) {
		printk("device not ready.\n");
		return 0;
	}

	counter_start(counter_dev);
	nrf_timer_task_trigger(ppi_timer, NRF_TIMER_TASK_CAPTURE2);

	alarm_cfg.flags = 0;
	alarm_cfg.ticks = counter_us_to_ticks(counter_dev, DELAY);
	alarm_cfg.callback = test_counter_interrupt_fn;
	alarm_cfg.user_data = &alarm_cfg;

	err = counter_set_channel_alarm(counter_dev, ALARM_CHANNEL_ID,
					&alarm_cfg);
	printk("Set alarm in %u sec (%u ticks)\n",
	       (uint32_t)(counter_ticks_to_us(counter_dev,
					   alarm_cfg.ticks) / USEC_PER_SEC),
	       alarm_cfg.ticks);

	if (-EINVAL == err) {
		printk("Alarm settings invalid\n");
	} else if (-ENOTSUP == err) {
		printk("Alarm setting request not supported\n");
	} else if (err != 0) {
		printk("Error\n");
	}

	k_msleep(400);
	uint32_t t_start = ppi_timer->CC[2];
	uint32_t t_evt = ppi_timer->CC[0];
	uint32_t t_int1 = ppi_timer->CC[1];
	uint32_t t_int2 = ppi_timer->CC[3];
	printk("started:%d (took %d us) evt:%d int1:%d irq_h latency:%d us user_h latency:%d us\n",
			t_start, (t_evt - t_start) / 16, t_evt, t_int1, (t_int1 - t_evt)/16,
			(t_int2-t_evt) / 16);
	while (1) {
		k_sleep(K_FOREVER);
	}
	return 0;
}

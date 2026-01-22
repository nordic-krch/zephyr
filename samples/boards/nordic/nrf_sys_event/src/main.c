/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nrf_sys_event.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/cache.h>
#include <stdio.h>

#if defined(CONFIG_SOC_NRF54L15_CPUAPP)
#define DBG_PIN1 8
#define DBG_PORT1 2
#define DBG_PIN2 10
#define DBG_PORT2 2
#elif defined(CONFIG_SOC_NRF54LM20A)
#define DBG_PIN1 8
#define DBG_PORT1 2
#define DBG_PIN2 10
#define DBG_PORT2 2
#else /* Firelight */
#define DBG_PIN1 10
#define DBG_PORT1 1
#define DBG_PIN2 12
#define DBG_PORT2 1
#endif

#ifdef CONFIG_NRF_SYS_EVENT_IRQ_LATENCY
static void counter_handler(const struct device *counter_dev, uint8_t ch_id,
			    uint32_t ticks, void *user_data)
{
	NRFX_CONCAT(NRF_P,DBG_PORT1)->OUTSET=BIT(DBG_PIN1);
	k_sem_give((struct k_sem *)user_data);
	NRFX_CONCAT(NRF_P,DBG_PORT1)->OUTCLR=BIT(DBG_PIN1);
}

static uint32_t counter_alarm_execute(const struct device *counter_dev,
				      struct counter_alarm_cfg *alarm_cfg, k_timeout_t timeout)
{
	struct k_sem sem;
	uint32_t now;
	int err;

	k_sem_init(&sem, 0, 1);
	alarm_cfg->user_data = &sem;

	now = k_cycle_get_32();
	NRFX_CONCAT(NRF_P,DBG_PORT2)->OUTSET=BIT(DBG_PIN2);
	err = counter_set_channel_alarm(counter_dev, 0, alarm_cfg);
	if (err < 0) {
		printf("Failed to set the counter alarm.\n");
		return 0;
	}
	err = k_sem_take(&sem, timeout);
	if (err < 0) {
		printf("Failed waiting for counter alarm.\n");
		return 0;
	}
	NRFX_CONCAT(NRF_P,DBG_PORT2)->OUTCLR=BIT(DBG_PIN2);

	return k_cycle_get_32() - now;
}

#include <helpers/nrfx_gppi.h>
#include <hal/nrf_gpio.h>
static void sys_event_irq_latency(void)
{
	int rv;

	nrf_gpio_cfg_output(DBG_PORT1*32+DBG_PIN1);
	nrf_gpio_cfg_output(DBG_PORT2*32+DBG_PIN2);

	/* Setup TIMER_COMPARE->PIN DPPI connection. */
	nrfx_gppi_handle_t h;
	uint32_t eep = (uint32_t)&NRF_TIMER20->EVENTS_COMPARE[2];
	uint32_t tep = (uint32_t)&NRF_GPIOTE30->TASKS_OUT[0];
	NRF_GPIOTE30->CONFIG[0] = 3 | (4<<4) | (3<<16);
	rv = nrfx_gppi_conn_alloc(eep, tep, &h);
	if (rv < 0) {
		printf("fail\n");
		return;
	}
	nrfx_gppi_conn_enable(h);

#if defined(CONFIG_SOC_NRF54LM20A)
	/* Setup RRAMC_WOKENUP->PIN connection. */
	nrfx_gppi_handle_t h2;
	uint32_t eep2 = (uint32_t)&NRF_RRAMC->EVENTS_WOKENUP;
	uint32_t tep2 = (uint32_t)&NRF_GPIOTE30->TASKS_OUT[1];

	NRF_GPIOTE30->CONFIG[1] = 3 | (5<<4) | (3<<16);
	rv = nrfx_gppi_conn_alloc(eep2, tep2, &h2);
	if (rv < 0) {
		printf("fail\n");
		return;
	}
	nrfx_gppi_conn_enable(h2);
#endif

	const struct device *counter = DEVICE_DT_GET(DT_NODELABEL(sample_counter));
	struct counter_alarm_cfg alarm_cfg;
	int delay = 1000;
	int delay_adj = 8;
	uint32_t rpt = 10;
	uint32_t cyc;
	int event_handle;

	counter_start(counter);
	alarm_cfg.flags = 0;
	alarm_cfg.ticks = counter_us_to_ticks(counter, delay);
	alarm_cfg.callback = counter_handler;

	cyc = 0;
	for (int i = 0; i < rpt; i++) {
		sys_cache_instr_invd_all();
		cyc += counter_alarm_execute(counter, &alarm_cfg, K_USEC(delay + 100));
	}

	cyc /= rpt;
	printf("Alarm set for %d us, execution took:%d (no event registered)\n", delay, cyc);

	cyc = 0;

	for (int i = 0; i < rpt; i++) {
		sys_cache_instr_invd_all();
		/* Event is delayed because it is registered early and not as it should just
		 * before starting. Triggering event too early may result in RRAMC going back
		 * to sleep before actual event wakes up the CPU.
		 */

		/**(volatile uint32_t *)((uint32_t)NRF_RRAMC + 0x514)=3;*/
		nrf_sys_event_request_global_constlat();

		uint32_t prev = NRF_RRAMC->POWER.LOWPOWERCONFIG;
#if defined(CONFIG_SOC_NRF54LM20A)
		NRF_RRAMC->POWER.LOWPOWERCONFIG=1;// | BIT(4);
#else
		NRF_RRAMC->POWER.LOWPOWERCONFIG=1;
#endif

		cyc += counter_alarm_execute(counter, &alarm_cfg, K_USEC(delay + 100));

		NRF_RRAMC->POWER.LOWPOWERCONFIG=prev;
		nrf_sys_event_release_global_constlat();
	}

	cyc /= rpt;
	printf("Alarm set for %d us, execution took:%d\n", delay, cyc);

	cyc = 0;

	for (int i = 0; i < rpt; i++) {
		sys_cache_instr_invd_all();
		/* Event is delayed because it is registered early and not as it should just
		 * before starting. Triggering event too early may result in RRAMC going back
		 * to sleep before actual event wakes up the CPU.
		 */
		event_handle = nrf_sys_event_register(delay + delay_adj, true);
		if (event_handle < 0) {
			printf("Failed to register an event:%d\n", event_handle);
			return;
		}
		cyc += counter_alarm_execute(counter, &alarm_cfg, K_USEC(delay + 100));
		(void)nrf_sys_event_unregister(event_handle, false);
	}

	cyc /= rpt;
	printf("Alarm set for %d us, execution took:%d\n", delay, cyc);
}
#endif /* CONFIG_NRF_SYS_EVENT_IRQ_LATENCY */

int main(void)
{
#ifdef CONFIG_NRF_SYS_EVENT_IRQ_LATENCY
	sys_event_irq_latency();
#endif
	return 0;
}

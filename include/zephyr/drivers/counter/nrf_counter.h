
#ifndef ZEPHYR_INCLUDE_DRIVERS_COUNTER_NRF_COUNTER_H_
#define ZEPHYR_INCLUDE_DRIVERS_COUNTER_NRF_COUNTER_H_

#include <zephyr/drivers/counter.h>

#define COUNTER_ALARM_CFG_NO_IRQ BIT(COUNTER_ALARM_CFG_BITS + 0)

#define COUNTER_ALARM_CFG_RESET_COUNTER BIT(COUNTER_ALARM_CFG_BITS + 1)

#define COUNTER_ALARM_CFG_STOP_COUNTER BIT(COUNTER_ALARM_CFG_BITS + 2)

enum nrf_counter_tsk_ep {
	NRF_COUNTER_TSK_EP_START,
	NRF_COUNTER_TSK_EP_STOP,
	NRF_COUNTER_TSK_EP_COUNT,
	NRF_COUNTER_TSK_EP_CLEAR,
};

uint32_t nrf_counter_get_compare_evt_ep(const struct device *dev, uint8_t chan_id);

uint32_t nrf_counter_get_capture_tsk_ep(const struct device *dev, uint8_t chan_id);

uint32_t nrf_counter_get_tsk_ep(const struct device *dev, enum nrf_counter_tsk_ep tsk_ep);

uint32_t nrf_counter_get_capture(const struct device *dev, uint8_t chan_id);

void nrf_counter_set_mode(const struct device *dev, bool counter);


#endif /* ZEPHYR_INCLUDE_DRIVERS_COUNTER_NRF_COUNTER_H_ */

# SAADC maxmimum performance {#saadc_maximum_performance}

This sample is a port to nrf54h20dk from sample that can be found in
``modules/hal/nordic/nrfx/samples/src/nrfx_saadc/maximum_performance``.
The sample demonstrates an advanced functionality of the nrfx_saadc driver operating at its peak performance.

## Requirements

The sample supports the following development kits:

| **Board**           | **Support** |
|---------------------|:-----------:|
| nrf54h20dk_nrf54h20 |     Yes     |

## Overview

Application initializes the nrfx_saadc driver and starts operating in the non-blocking mode.
Sampling is performed at the highest supported frequency.
In the sample @p m_single_channel is configured, and the SAADC driver is set to the advanced mode.
To achieve the maximum performance, do the following:
- Provide an external timer in order to perform sampling at @p MAX_SAADC_SAMPLE_FREQUENCY.
  You can do this by setting up endpoints of the channel @p m_gppi_channels [ @p gppi_channels_purpose_t::SAADC_SAMPLING ] to trigger the SAADC sample task ( @p nrf_saadc_task_t::NRF_SAADC_TASK_SAMPLE ) on the TIMER COMPARE event.
- Provide hardware start-on-end.
  You can do this by setting up endpoints of the channel @p m_gppi_channels [ @p gppi_channels_purpose_t::SAADC_START_ON_END ] to trigger SAADC task start ( @p nrf_saadc_task_t::NRF_SAADC_TASK_START ) on the SAADC event end ( @p nrf_saadc_event_t::NRF_SAADC_EVENT_END ).

@p nrfx_saadc_offset_calibrate triggers calibration in a non-blocking manner.
Then, sampling is initiated at @p NRFX_SAADC_EVT_CALIBRATEDONE event in @p saadc_handler() by calling @p nrfx_saadc_mode_trigger() function.
Consecutive sample tasks are triggered by the external timer at the sample rate specified in @p SAADC_SAMPLE_FREQUENCY symbol.

> For more information, see **SAADC driver - nrfx documentation**.

## Wiring

To run the sample correctly, connect pins as follows:
- nrf54h20dk Pin 1.0 (Analog pin 0) with Pin 9.0 (LED0 pin).

You should see the following output:

```
[00:00:00.169,958] <inf> app/app: Starting nrfx_saadc maximum performance example.
[00:00:00.170,096] <inf> app/app: SAADC event: CALIBRATEDONE
[00:00:00.170,102] <inf> app/app: SAADC event: READY
[00:00:00.170,107] <inf> app/app: SAADC event: BUF_REQ
[00:00:00.170,155] <inf> app/app: SAADC event: DONE
[00:00:00.170,158] <inf> app/app: Sample buffer address == 0x2fc12ec8
[00:00:00.170,161] <inf> app/app: [Sample 0] value == 2
[00:00:00.170,163] <inf> app/app: [Sample 1] value == 1
[00:00:00.170,166] <inf> app/app: [Sample 2] value == 0
[00:00:00.170,168] <inf> app/app: [Sample 3] value == 0
[00:00:00.170,171] <inf> app/app: [Sample 4] value == 0
[00:00:00.170,172] <inf> app/app: [Sample 5] value == 1
[00:00:00.170,176] <inf> app/app: [Sample 6] value == 0
[00:00:00.170,177] <inf> app/app: [Sample 7] value == 0
[00:00:00.170,179] <inf> app/app: SAADC event: BUF_REQ
[00:00:00.170,195] <inf> app/app: SAADC event: DONE
[00:00:00.170,196] <inf> app/app: Sample buffer address == 0x2fc12ed8
[00:00:00.170,200] <inf> app/app: [Sample 0] value == 0
[00:00:00.170,201] <inf> app/app: [Sample 1] value == 0
[00:00:00.170,204] <inf> app/app: [Sample 2] value == 1023
[00:00:00.170,206] <inf> app/app: [Sample 3] value == 1023
[00:00:00.170,208] <inf> app/app: [Sample 4] value == 1023
[00:00:00.170,211] <inf> app/app: [Sample 5] value == 1023
[00:00:00.170,212] <inf> app/app: [Sample 6] value == 1023
[00:00:00.170,216] <inf> app/app: [Sample 7] value == 1023
[00:00:00.170,217] <inf> app/app: SAADC event: BUF_REQ
[00:00:00.170,233] <inf> app/app: SAADC event: DONE
[00:00:00.170,235] <inf> app/app: Sample buffer address == 0x2fc12ec8
[00:00:00.170,238] <inf> app/app: [Sample 0] value == 1023
[00:00:00.170,241] <inf> app/app: [Sample 1] value == 1023
[00:00:00.170,243] <inf> app/app: [Sample 2] value == 1023
[00:00:00.170,244] <inf> app/app: [Sample 3] value == 1023
[00:00:00.170,248] <inf> app/app: [Sample 4] value == 1023
[00:00:00.170,249] <inf> app/app: [Sample 5] value == 1023
[00:00:00.170,252] <inf> app/app: [Sample 6] value == 1023
[00:00:00.170,254] <inf> app/app: [Sample 7] value == 1023
[00:00:00.170,257] <inf> app/app: FINISHED
```

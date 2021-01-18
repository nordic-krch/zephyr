/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tracing/tracing_log.h>
#include <kernel.h>

void sys_trace_idle(void)
{
	Z_TRACING_LOG_TRACE(TRACING_LOG_IDLE);
}

void sys_trace_isr_enter(void)
{
	Z_TRACING_LOG_TRACE(TRACING_LOG_ISR_ENTER);
}

void sys_trace_isr_exit(void)
{
	Z_TRACING_LOG_TRACE(TRACING_LOG_ISR_EXIT);
}


void sys_trace_thread_switched_in(void)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_THREAD_SWITCHED_IN,
				k_current_get());
}


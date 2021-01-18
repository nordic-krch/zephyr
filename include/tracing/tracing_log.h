/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_TRACING_TRACING_LOG_H_
#define ZEPHYR_INCLUDE_TRACING_TRACING_LOG_H_

#include <logging/log_core2.h>
#include <kernel.h>

void sys_trace_isr_enter(void);

void sys_trace_isr_exit(void);

static inline void sys_trace_isr_exit_to_scheduler(void)
{
	Z_TRACING_LOG_TRACE(TRACING_LOG_ISR_EXIT_TO_SCHEDULER);
}

void sys_trace_idle(void);

void sys_trace_thread_switched_in(void);

static inline void sys_trace_thread_switched_out(void)
{
	Z_TRACING_LOG_TRACE(TRACING_LOG_THREAD_SWITCHED_OUT);
}

static inline void sys_trace_thread_priority_set(struct k_thread *thread)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_THREAD_PRIO_SET, thread);
}

static inline void sys_trace_thread_create(struct k_thread *thread)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_THREAD_CREATE, thread);
}

static inline void sys_trace_thread_abort(struct k_thread *thread)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_THREAD_ABORT, thread);
}

static inline void sys_trace_thread_suspend(struct k_thread *thread)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_THREAD_SUSPEND, thread);
}

static inline void sys_trace_thread_resume(struct k_thread *thread)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_THREAD_RESUME, thread);
}

static inline void sys_trace_thread_ready(struct k_thread *thread)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_THREAD_READY, thread);
}

static inline void sys_trace_thread_pend(struct k_thread *thread)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_THREAD_PEND, thread);
}

static inline void sys_trace_thread_info(struct k_thread *thread)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_THREAD_INFO, thread);
}

static inline void sys_trace_thread_name_set(struct k_thread *thread)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_THREAD_NAME_SET, thread);
}

static inline void sys_trace_void(uint32_t id)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_VOID, (void *)id);
}

static inline void sys_trace_end_call(uint32_t id)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_END_CALL, (void *)id);
}

static inline void sys_trace_semaphore_init(struct k_sem *sem)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_SEMAPHORE_INIT, sem);
}

static inline void sys_trace_semaphore_take(struct k_sem *sem)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_SEMAPHORE_TAKE, sem);
}

static inline void sys_trace_semaphore_give(struct k_sem *sem)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_SEMAPHORE_GIVE, sem);
}

static inline void sys_trace_mutex_init(struct k_mutex *mutex)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_MUTEX_INIT, mutex);
}

static inline void sys_trace_mutex_lock(struct k_mutex *mutex)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_MUTEX_LOCK, mutex);
}

static inline void sys_trace_mutex_unlock(struct k_mutex *mutex)
{
	Z_TRACING_LOG_TRACE_PTR(TRACING_LOG_MUTEX_UNLOCK, mutex);
}

#endif /* ZEPHYR_INCLUDE_TRACING_TRACING_LOG_H_ */



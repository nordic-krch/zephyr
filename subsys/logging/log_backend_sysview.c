/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <logging/log_msg2.h>
#include <logging/log_core.h>
#include <logging/log_backend.h>
#include <logging/log_output.h>
#include <SEGGER_SYSVIEW.h>
#include <Global.h>
#include "SEGGER_SYSVIEW_Zephyr.h"
#include <kernel.h>
#include <kernel_structs.h>
#include <ksched.h>

#if CONFIG_THREAD_MAX_NAME_LEN
#define THREAD_NAME_LEN CONFIG_THREAD_MAX_NAME_LEN
#else
#define THREAD_NAME_LEN 20
#endif

static void thread_switched_in(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	struct k_thread *thread = msg->trace_ptr.ptr;

	if (z_is_idle_thread_object(thread)) {
		SEGGER_SYSVIEW_OnIdle();
	} else {
		SEGGER_SYSVIEW_OnTaskStartExec((uint32_t)(uintptr_t)thread);
	}
}

static void thread_switched_out(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	SEGGER_SYSVIEW_OnTaskStopExec();
}

static void isr_enter(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	SEGGER_SYSVIEW_RecordEnterISR();
}


static void isr_exit(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	SEGGER_SYSVIEW_RecordExitISR();
}

static void isr_exit_to_scheduler(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	SEGGER_SYSVIEW_RecordExitISRToScheduler();
}

static void trace_idle(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	SEGGER_SYSVIEW_OnIdle();
}

static void semaphore_init(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	SEGGER_SYSVIEW_RecordU32(
		SYS_TRACE_ID_SEMA_INIT,
		(uint32_t)(uintptr_t)msg->trace_ptr.ptr);
}

static void semaphore_take(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	SEGGER_SYSVIEW_RecordU32(
		SYS_TRACE_ID_SEMA_TAKE,
		(uint32_t)(uintptr_t)msg->trace_ptr.ptr);
}

static void semaphore_give(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	SEGGER_SYSVIEW_RecordU32(
		SYS_TRACE_ID_SEMA_GIVE,
		(uint32_t)(uintptr_t)msg->trace_ptr.ptr);
}

static void mutex_init(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	SEGGER_SYSVIEW_RecordU32(
		SYS_TRACE_ID_MUTEX_INIT,
		(uint32_t)(uintptr_t)msg->trace_ptr.ptr);
}

static void mutex_lock(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	SEGGER_SYSVIEW_RecordU32(
		SYS_TRACE_ID_MUTEX_LOCK,
		(uint32_t)(uintptr_t)msg->trace_ptr.ptr);
}

static void mutex_unlock(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	SEGGER_SYSVIEW_RecordU32(
		SYS_TRACE_ID_MUTEX_UNLOCK,
		(uint32_t)(uintptr_t)msg->trace_ptr.ptr);
}

static void set_thread_name(char *name, struct k_thread *thread)
{
	const char *tname = k_thread_name_get(thread);

	if (tname != NULL && tname[0] != '\0') {
		memcpy(name, tname, THREAD_NAME_LEN);
		name[THREAD_NAME_LEN - 1] = '\0';
	} else {
		snprintk(name, THREAD_NAME_LEN, "T%pE%p",
		thread, &thread->entry);
	}
}

static void thread_info(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	char name[THREAD_NAME_LEN];
	struct k_thread *thread = msg->trace_ptr.ptr;

	set_thread_name(name, thread);

	SEGGER_SYSVIEW_TASKINFO Info;

	Info.TaskID = (uint32_t)(uintptr_t)thread;
	Info.sName = name;
	Info.Prio = thread->base.prio;
	Info.StackBase = thread->stack_info.size;
	Info.StackSize = thread->stack_info.start;
	SEGGER_SYSVIEW_SendTaskInfo(&Info);
}

static void thread_create(const struct log_backend *const backend,
			  union log_msg2_generic *msg)
{
	SEGGER_SYSVIEW_OnTaskCreate((uint32_t)(uintptr_t)msg->trace_ptr.ptr);
	thread_info(backend, msg);
}

static void thread_ready(const struct log_backend *const backend,
			  union log_msg2_generic *msg)
{
	struct k_thread *thread = msg->trace_ptr.ptr;

	SEGGER_SYSVIEW_OnTaskStartReady((uint32_t)(uintptr_t)thread);
}

static void thread_pend(const struct log_backend *const backend,
			  union log_msg2_generic *msg)
{
	struct k_thread *thread = msg->trace_ptr.ptr;

	SEGGER_SYSVIEW_OnTaskStopReady((uint32_t)(uintptr_t)thread, 3 << 3);
}

static void trace_void(const struct log_backend *const backend,
			  union log_msg2_generic *msg)
{
	void *id = msg->trace_ptr.ptr;

	SEGGER_SYSVIEW_RecordVoid((uint32_t)(uintptr_t)id);
}

static void end_call(const struct log_backend *const backend,
		     union log_msg2_generic *msg)
{
	void *id = msg->trace_ptr.ptr;

	SEGGER_SYSVIEW_RecordEndCall((uint32_t)(uintptr_t)id);
}

static void send_task_list_cb(void)
{
	struct k_thread *thread;

	for (thread = _kernel.threads; thread; thread = thread->next_thread) {
		char name[THREAD_NAME_LEN];

		if (z_is_idle_thread_object(thread)) {
			continue;
		}

		set_thread_name(name, thread);

		SEGGER_SYSVIEW_SendTaskInfo(&(SEGGER_SYSVIEW_TASKINFO) {
			.TaskID = (uint32_t)(uintptr_t)thread,
			.sName = name,
			.StackSize = thread->stack_info.size,
			.StackBase = thread->stack_info.start,
			.Prio = thread->base.prio,
		});
	}
}


static U64 get_time_cb(void)
{
	return (U64)0;
}

uint32_t sysview_get_timestamp(void)
{
	return k_cycle_get_32();
}

uint32_t sysview_get_interrupt(void)
{
	return 0;
}


typedef void (*sysview_trace_handler_t)(const struct log_backend *const backend,
					union log_msg2_generic *msg);

static sysview_trace_handler_t handlers[] = {
	[TRACING_LOG_THREAD_SWITCHED_OUT] = thread_switched_out,
	[TRACING_LOG_ISR_ENTER] = isr_enter,
	[TRACING_LOG_ISR_EXIT] = isr_exit,
	[TRACING_LOG_ISR_EXIT_TO_SCHEDULER] = isr_exit_to_scheduler,
	[TRACING_LOG_IDLE] = trace_idle,

	/* IDs using additional data */
	[TRACING_LOG_THREAD_SWITCHED_IN] = thread_switched_in,
	[TRACING_LOG_THREAD_PRIO_SET] = NULL,
	[TRACING_LOG_THREAD_CREATE] = thread_create,
	[TRACING_LOG_THREAD_ABORT] = NULL,
	[TRACING_LOG_THREAD_SUSPEND] = NULL,
	[TRACING_LOG_THREAD_RESUME] = NULL,
	[TRACING_LOG_THREAD_READY] = thread_ready,
	[TRACING_LOG_THREAD_PEND] = thread_pend,
	[TRACING_LOG_THREAD_INFO] = thread_info,
	[TRACING_LOG_THREAD_NAME_SET] = NULL,
	[TRACING_LOG_VOID] = trace_void,
	[TRACING_LOG_END_CALL] = end_call,
	[TRACING_LOG_SEMAPHORE_INIT] = semaphore_init,
	[TRACING_LOG_SEMAPHORE_TAKE] = semaphore_take,
	[TRACING_LOG_SEMAPHORE_GIVE] = semaphore_give,
	[TRACING_LOG_MUTEX_INIT] = mutex_init,
	[TRACING_LOG_MUTEX_LOCK] = mutex_lock,
	[TRACING_LOG_MUTEX_UNLOCK] = mutex_unlock
};

const SEGGER_SYSVIEW_OS_API SYSVIEW_X_OS_TraceAPI = {
	get_time_cb,
	send_task_list_cb,
};
static uint8_t output_buf[256];
static int offset;

static uint8_t output_byte;

static int char_out(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);

	if (offset < sizeof(output_buf) - 1) {
		output_buf[offset++] = data[0];
	}

	return length;
}

LOG_OUTPUT_DEFINE(log_output_sysview, char_out, &output_byte, 1);


typedef void (*sysview_print_t)(const char *str);

static void msg_process(const struct log_backend *const backend,
			union log_msg2_generic *msg)
{
	sysview_print_t func;

	offset = 0;
	log_output_msg2_process(&log_output_sysview, &msg->log,
				LOG_OUTPUT_FLAG_CRLF_NONE);
	output_buf[offset] = 0;

	switch (msg->log.hdr.desc.level) {
	case  LOG_LEVEL_ERR:
		func = SEGGER_SYSVIEW_Error;
		break;
	case LOG_LEVEL_WRN:
		func = SEGGER_SYSVIEW_Warn;
		break;
	default:
		func = SEGGER_SYSVIEW_Print;
		break;
	}

	func(output_buf);
}

static void trace_process(const struct log_backend *const backend,
			  union log_msg2_generic *msg)
{
	sysview_trace_handler_t handler = handlers[msg->trace.hdr.evt_id];

	if (handler) {
		handler(backend, msg);
	}
}

static void process(const struct log_backend *const backend,
		union log_msg2_generic *msg)
{
	if (z_log_item_is_msg(msg)) {
		msg_process(backend, msg);
		return;
	}

	trace_process(backend, msg);
}

static void cbSendSystemDesc(void)
{
	SEGGER_SYSVIEW_SendSysDesc("N=ZephyrSysView");
	SEGGER_SYSVIEW_SendSysDesc("D=" CONFIG_BOARD " "
				   CONFIG_SOC_SERIES " " CONFIG_ARCH);
	SEGGER_SYSVIEW_SendSysDesc("O=Zephyr");
}

void SEGGER_SYSVIEW_Conf(void)
{
	SEGGER_SYSVIEW_Init(sys_clock_hw_cycles_per_sec(),
			    sys_clock_hw_cycles_per_sec(),
			    &SYSVIEW_X_OS_TraceAPI, cbSendSystemDesc);
#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_sram), okay)
	SEGGER_SYSVIEW_SetRAMBase(DT_REG_ADDR(DT_CHOSEN(zephyr_sram)));
#else
	/* Setting RAMBase is just an optimization: this value is subtracted
	 * from all pointers in order to save bandwidth.  It's not an error
	 * if a platform does not set this value.
	 */
#endif
}

static void init(void)
{
	SEGGER_SYSVIEW_Conf();
	SEGGER_SYSVIEW_Start();
}

const struct log_backend_api log_backend_sysview_api = {
	.process = process,
	.init = init
};

LOG_BACKEND_DEFINE(log_backend_sysview, log_backend_sysview_api, true);

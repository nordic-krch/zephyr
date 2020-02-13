/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ztest.h>
#include <sys/queued_operation.h>

struct service;

struct operation {
	struct queued_operation operation;
	void (*callback)(struct service *sp,
			 struct operation *op,
			 void *ud);
	void *user_data;
};

struct service {
	struct queued_operation_manager manager;

	/* The current operation cast for this service type.  Null if
	 * service is idle.
	 */
	struct operation *current;

	/* Value to return from service_impl_validate() */
	int validate_rv;

	/* Value to return from service_impl_validate()
	 *
	 * This is incremented before each synchronous finalization by
	 * service_impl_callback.
	 */
	int process_rv;

	/* Parameters passed to test_callback */
	struct operation *callback_op;
	int callback_res;

	/* Count of process submissions since reset. */
	size_t process_cnt;

	/* If set inhibit synchronous completion. */
	bool async;

	/* Set to indicate that the lass process() call provided an
	 * operation.
	 */
	bool active;
};

typedef void (*service_callback)(struct service *sp,
				 struct operation *op,
				 int res);

static void test_callback(struct service *sp,
			  struct operation *op,
			  int res)
{
	sp->callback_op = op;
	sp->callback_res = res;
	if (op->callback) {
		op->callback(sp, op, op->user_data);
	}
}

static inline void operation_init_spinwait(struct operation *op)
{
	*op = (struct operation){};
	async_notify_init_spinwait(&op->operation.notify);
}

static inline void operation_init_signal(struct operation *op,
					 struct k_poll_signal *sigp)
{
	*op = (struct operation){};
	async_notify_init_signal(&op->operation.notify, sigp);
}

static inline void operation_init_callback(struct operation *op,
					   service_callback handler)
{
	*op = (struct operation){};
	async_notify_init_callback(&op->operation.notify,
				   (async_notify_generic_callback)handler);
}

static int service_submit(struct service *sp,
			  struct operation *op,
			  int priority)
{
	return queued_operation_submit(&sp->manager, &op->operation, priority);
}

static int service_cancel(struct service *sp,
			  struct operation *op)
{
	return queued_operation_cancel(&sp->manager, &op->operation);
}

static int service_impl_validate(struct queued_operation_manager *mgr,
				 struct queued_operation *op)
{
	struct service *sp = CONTAINER_OF(mgr, struct service, manager);

	return sp->validate_rv;
}

static void service_impl_callback(struct queued_operation_manager *mgr,
				  struct queued_operation *op,
				  async_notify_generic_callback cb)
{
	service_callback handler = (service_callback)cb;
	struct service *sp = CONTAINER_OF(mgr, struct service, manager);
	struct operation *sop = CONTAINER_OF(op, struct operation, operation);
	int res = -EINPROGRESS;

	zassert_equal(queued_operation_fetch_result(op, &res), 0,
		      "callback before finalized");
	handler(sp, sop, res);
}

/* Split out finalization to support async testing. */
static void service_finalize(struct service *sp,
			     int res)
{
	struct queued_operation *op = &sp->current->operation;

	sp->current = NULL;
	queued_operation_finalize(&sp->manager, op, res);
}

static void service_impl_process(struct queued_operation_manager *mgr,
				 struct queued_operation *op)
{
	struct service *sp = CONTAINER_OF(mgr, struct service, manager);

	zassert_equal(sp->current, NULL,
		      "process collision");

	sp->active = (op != NULL);
	if (sp->active) {
		sp->process_cnt++;
		sp->current = CONTAINER_OF(op, struct operation, operation);
		if (!sp->async) {
			service_finalize(sp, ++sp->process_rv);
		}
	}
}

static struct queued_operation_functions const service_vtable = {
	.validate = service_impl_validate,
	.callback = service_impl_callback,
	.process = service_impl_process,
};
/* Live copy, mutated for testing. */
static struct queued_operation_functions vtable;

static struct service service = {
	.manager = QUEUED_OPERATION_MANAGER_INITIALIZER(&vtable),
};

static void reset_service(void)
{
	vtable = service_vtable;
	service = (struct service){
		.manager = QUEUED_OPERATION_MANAGER_INITIALIZER(&vtable),
	};
}

static void test_notification_spinwait(void)
{
	struct operation operation;
	struct operation *op = &operation;
	struct async_notify *np = &op->operation.notify;
	int res = 0;
	int rc = 0;

	reset_service();

	operation_init_spinwait(&operation);
	zassert_equal(async_notify_fetch_result(np, &res), -EAGAIN,
		      "failed spinwait unfinalized");

	rc = service_submit(&service, op, 0);
	zassert_equal(rc, service.validate_rv,
		      "submit spinwait failed: %d != %d", rc,
		      service.validate_rv);
	zassert_equal(async_notify_fetch_result(np, &res), 0,
		      "failed spinwait fetch");
	zassert_equal(res, service.process_rv,
		      "failed spinwait result");

	zassert_false(service.active, "service not idled");
}

static void test_notification_signal(void)
{
	struct operation operation;
	struct operation *op = &operation;
	struct async_notify *np = &op->operation.notify;
	struct k_poll_signal sig;
	unsigned int signaled;
	int res = 0;
	int rc = 0;

	reset_service();

	k_poll_signal_init(&sig);
	operation_init_signal(op, &sig);
	zassert_equal(async_notify_fetch_result(np, &res), -EAGAIN,
		      "failed signal unfinalized");
	k_poll_signal_check(&sig, &signaled, &res);
	zassert_equal(signaled, 0,
		      "failed signal unsignaled");

	service.process_rv = 23;
	rc = service_submit(&service, op, 0);
	zassert_equal(rc, 0,
		      "submit signal failed: %d", rc);
	zassert_equal(async_notify_fetch_result(np, &res), 0,
		      "failed signal fetch");
	zassert_equal(res, service.process_rv,
		      "failed signal result");
	k_poll_signal_check(&sig, &signaled, &res);
	zassert_equal(signaled, 1,
		      "failed signal signaled");
	zassert_equal(res, service.process_rv,
		      "failed signal signal result");
}

static void test_notification_callback(void)
{
	struct operation operation;
	struct operation *op = &operation;
	struct service *sp = &service;
	struct async_notify *np = &op->operation.notify;
	struct k_poll_signal sig;
	int res = 0;
	int rc = 0;

	reset_service();

	k_poll_signal_init(&sig);
	operation_init_callback(op, test_callback);
	zassert_equal(async_notify_fetch_result(np, &res), -EAGAIN,
		      "failed callback unfinalized");
	zassert_equal(sp->callback_op, NULL,
		      "failed callback pre-check");

	service.process_rv = 142;
	rc = service_submit(&service, op, 0);
	zassert_equal(rc, 0,
		      "submit callback failed: %d", rc);
	zassert_equal(async_notify_fetch_result(np, &res), 0,
		      "failed callback fetch");
	zassert_equal(res, service.process_rv,
		      "failed callback result");
	zassert_equal(sp->callback_op, op,
		      "failed callback captured op");
	zassert_equal(sp->callback_res, service.process_rv,
		      "failed callback captured res");
}

struct pri_order {
	int priority;
	size_t ordinal;
};

static void test_sync_priority(void)
{
	struct pri_order const pri_order[] = {
		{ 0, 0 }, /* first because it gets grabbed when submitted */
		/* rest in FIFO within priority */
		{ -1, 2 },
		{ 1, 4 },
		{ -2, 1 },
		{ 2, 6 },
		{ 1, 5 },
		{ 0, 3 },
	};
	struct operation operation[ARRAY_SIZE(pri_order)];
	struct async_notify *np[ARRAY_SIZE(operation)];
	int res = -EINPROGRESS;
	int rc;

	/* Reset the service, and tell it to not finalize operations
	 * synchronously (so we can build up a queue).
	 */
	reset_service();
	service.async = true;

	for (size_t i = 0; i < ARRAY_SIZE(operation); ++i) {
		operation_init_spinwait(&operation[i]);
		np[i] = &operation[i].operation.notify;
		rc = service_submit(&service, &operation[i], pri_order[i].priority);
		zassert_equal(rc, 0,
			      "submit op%u failed: %d", i, rc);
		zassert_equal(async_notify_fetch_result(np[i], &res), -EAGAIN,
			      "op%u finalized!", i);
	}

	zassert_equal(service.current, &operation[0],
		      "submit op0 didn't process");

	/* Enable synchronous finalization and kick off the first
	 * entry.  All the others will execute immediately.
	 */
	service.async = false;
	service_finalize(&service, service.process_rv);

	for (size_t i = 0; i < ARRAY_SIZE(operation); ++i) {
		size_t ordinal = pri_order[i].ordinal;

		zassert_equal(async_notify_fetch_result(np[i], &res), 0,
			      "op%u unfinalized", i);
		zassert_equal(res, ordinal,
			      "op%u wrong order: %d != %u", i, res, ordinal);
	}
}

struct delayed_submit {
	struct operation *op;
	int priority;
};

static void test_delayed_submit(struct service *sp,
				struct operation *op,
				void *ud)
{
	struct delayed_submit *dsp = ud;
	int rc = service_submit(sp, dsp->op, dsp->priority);

	zassert_equal(rc, 0,
		      "delayed submit failed: %d", rc);
}

static void test_resubmit_priority(void)
{
	struct pri_order const pri_order[] = {
		{ 0, 0 },       /* first because it gets grabbed when submitted */
		{ 0, 2 },       /* delayed by submit of higher priority during callback */
		{ -1, 1 },      /* submitted during completion of op0 */
	};
	size_t di = ARRAY_SIZE(pri_order) - 1;
	struct operation operation[ARRAY_SIZE(pri_order)];
	struct async_notify *np[ARRAY_SIZE(operation)];
	int res = -EINPROGRESS;
	int rc;

	/* Queue two operations, but in the callback for the first
	 * schedule a third operation that has higher priority.
	 */
	reset_service();
	service.async = true;

	for (size_t i = 0; i <= di; ++i) {
		operation_init_callback(&operation[i], test_callback);
		np[i] = &operation[i].operation.notify;
		if (i < di) {
			rc = service_submit(&service, &operation[i], 0);
			zassert_equal(rc, 0,
				      "submit op%u failed: %d", i, rc);
			zassert_equal(async_notify_fetch_result(np[i], &res), -EAGAIN,
				      "op%u finalized!", i);
		}
	}

	struct delayed_submit ds = {
		.op = &operation[di],
		.priority = pri_order[di].priority,
	};
	operation[0].callback = test_delayed_submit;
	operation[0].user_data = &ds;

	/* Enable synchronous finalization and kick off the first
	 * entry.  All the others will execute immediately.
	 */
	service.async = false;
	service_finalize(&service, service.process_rv);

	zassert_equal(service.process_cnt, ARRAY_SIZE(operation),
		      "not all processed once: %d != %d",
		      ARRAY_SIZE(operation), service.process_cnt);

	for (size_t i = 0; i < ARRAY_SIZE(operation); ++i) {
		size_t ordinal = pri_order[i].ordinal;

		zassert_equal(async_notify_fetch_result(np[i], &res), 0,
			      "op%u unfinalized", i);
		zassert_equal(res, ordinal,
			      "op%u wrong order: %d != %u", i, res, ordinal);
	}
}

static void test_missing_validation(void)
{
	struct operation operation;
	struct operation *op = &operation;
	struct async_notify *np = &op->operation.notify;
	int res = 0;
	int rc = 0;

	reset_service();
	vtable.validate = NULL;

	operation_init_spinwait(&operation);
	zassert_equal(async_notify_fetch_result(np, &res), -EAGAIN,
		      "failed spinwait unfinalized");

	rc = service_submit(&service, op, 0);
	zassert_equal(rc, 0,
		      "submit spinwait failed: %d", rc);
	zassert_equal(async_notify_fetch_result(np, &res), 0,
		      "failed spinwait fetch");
	zassert_equal(res, service.process_rv,
		      "failed spinwait result");
}

static void test_success_validation(void)
{
	struct operation operation;
	struct operation *op = &operation;
	struct async_notify *np = &op->operation.notify;
	int res = 0;
	int rc = 0;

	reset_service();
	service.validate_rv = 57;

	operation_init_spinwait(&operation);
	zassert_equal(async_notify_fetch_result(np, &res), -EAGAIN,
		      "failed spinwait unfinalized");

	rc = service_submit(&service, op, 0);
	zassert_equal(rc, service.validate_rv,
		      "submit validation did not succeed as expected: %d", rc);
}

static void test_failed_validation(void)
{
	struct operation operation;
	struct operation *op = &operation;
	struct async_notify *np = &op->operation.notify;
	int res = 0;
	int rc = 0;

	reset_service();
	service.validate_rv = -EINVAL;

	operation_init_spinwait(&operation);
	zassert_equal(async_notify_fetch_result(np, &res), -EAGAIN,
		      "failed spinwait unfinalized");

	rc = service_submit(&service, op, 0);
	zassert_equal(rc, service.validate_rv,
		      "submit validation did not fail as expected: %d", rc);
}

static void test_callback_validation(void)
{
	struct operation operation;
	struct operation *op = &operation;
	int expect = -ENOTSUP;
	int rc = 0;

	reset_service();
	vtable.callback = NULL;

	operation_init_callback(&operation, test_callback);
	rc = service_submit(&service, op, 0);
	zassert_equal(rc, expect,
		      "unsupported callback check failed: %d != %d", rc, expect);
}

static void test_priority_validation(void)
{
	struct operation operation;
	struct operation *op = &operation;
	int expect = -EINVAL;
	int rc = 0;

	reset_service();
	vtable.callback = NULL;

	operation_init_callback(&operation, test_callback);
	rc = service_submit(&service, op, 128);
	zassert_equal(rc, expect,
		      "unsupported priority check failed: %d != %d", rc, expect);
}

static void test_cancel_active(void)
{
	struct operation operation;
	struct operation *op = &operation;
	int expect = -EINPROGRESS;
	int rc = 0;

	reset_service();
	service.async = true;
	service.validate_rv = 152;

	operation_init_spinwait(&operation);
	rc = service_submit(&service, op, 0);
	zassert_equal(rc, service.validate_rv,
		      "submit failed: %d != %d", rc, service.validate_rv);

	rc = service_cancel(&service, op);
	zassert_equal(rc, expect,
		      "cancel failed: %d != %d", rc, expect);
}

static void test_cancel_inactive(void)
{
	struct operation operation[2];
	struct async_notify *np[ARRAY_SIZE(operation)];
	struct operation *op1 = &operation[1];
	int res;
	int rc = 0;

	reset_service();
	service.async = true;

	/* Set up two operations, but only submit the first. */
	for (size_t i = 0; i < ARRAY_SIZE(operation); ++i) {
		operation_init_spinwait(&operation[i]);
		np[i] = &operation[i].operation.notify;
		if (i == 0) {
			rc = service_submit(&service, &operation[i], 0);
			zassert_equal(rc, service.validate_rv,
				      "submit failed: %d != %d", rc, service.validate_rv);
		}
	}

	zassert_equal(service.current, &operation[0],
		      "current not op0");

	zassert_equal(async_notify_fetch_result(np[1], &res), -EAGAIN,
		      "op1 finalized!");

	/* Verify attempt to cancel unsubmitted operation. */
	rc = service_cancel(&service, op1);
	zassert_equal(rc, -EINVAL,
		      "cancel failed: %d != %d", rc, -EINVAL);

	/* Submit, then verify cancel succeeds. */
	rc = service_submit(&service, op1, 0);
	zassert_equal(rc, service.validate_rv,
		      "submit failed: %d != %d", rc, service.validate_rv);

	zassert_equal(async_notify_fetch_result(np[1], &res), -EAGAIN,
		      "op1 finalized!");

	rc = service_cancel(&service, op1);
	zassert_equal(rc, 0,
		      "cancel failed: %d", rc);

	zassert_equal(async_notify_fetch_result(np[1], &res), 0,
		      "op1 NOT finalized");
	zassert_equal(res, -ECANCELED,
		      "op1 cancel result unexpected: %d", res);

	service.async = false;
	service_finalize(&service, service.process_rv);
	zassert_equal(service.process_cnt, 1,
		      "too many processed");
}


void test_main(void)
{
	ztest_test_suite(queued_operation_api,
			 ztest_unit_test(test_notification_spinwait),
			 ztest_unit_test(test_notification_signal),
			 ztest_unit_test(test_notification_callback),
			 ztest_unit_test(test_sync_priority),
			 ztest_unit_test(test_resubmit_priority),
			 ztest_unit_test(test_missing_validation),
			 ztest_unit_test(test_success_validation),
			 ztest_unit_test(test_failed_validation),
			 ztest_unit_test(test_callback_validation),
			 ztest_unit_test(test_priority_validation),
			 ztest_unit_test(test_cancel_active),
			 ztest_unit_test(test_cancel_inactive));
	ztest_run_test_suite(queued_operation_api);
}

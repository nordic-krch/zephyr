/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <sys/queued_operation.h>

#define INVALID_ADDR (void *)UINTPTR_MAX

static inline int op_get_priority(const struct queued_operation *op)
{
	return (s8_t)(op->notify.flags >> QUEUED_OPERATION_PRIORITY_POS);
}

static inline int op_set_priority(struct queued_operation *op,
				  int priority)
{
	s8_t prio = (s8_t)priority;
	u32_t mask = (QUEUED_OPERATION_PRIORITY_MASK << QUEUED_OPERATION_PRIORITY_POS);

	if (prio != priority) {
		return -EINVAL;
	}

	op->notify.flags = (op->notify.flags & ~mask)
			   | (mask & (prio << QUEUED_OPERATION_PRIORITY_POS));

	return 0;
}

static void select_next(struct queued_operation_manager *mgr)
{
	struct queued_operation *op;

	do {
		k_spinlock_key_t key = k_spin_lock(&mgr->lock);

		sys_snode_t *node = sys_slist_get(&mgr->operations);

		op = node ?
			CONTAINER_OF(node, struct queued_operation, node) : NULL;

		k_spin_unlock(&mgr->lock, key);

		mgr->vtable->process(mgr, op);

		/* After calling process (and unlocking interrupts) something
		 * might have been added to a queue. Need to check that and
		 * repeat the loop (even if list was empty before).
		 */
		key = k_spin_lock(&mgr->lock);
		if ((op == NULL) && sys_slist_is_empty(&mgr->operations)) {
			mgr->current = NULL;
		} else if (op) {
			mgr->current = op;
		} else {
			/* op is NULL, but list got field in the meantime.
			 * in that case manager stays in finalizing state and
			 * loop will repeat.
			 */
		}

		k_spin_unlock(&mgr->lock, key);	
	} while (mgr->current == INVALID_ADDR);
}

int queued_operation_submit(struct queued_operation_manager *mgr,
			    struct queued_operation *op,
			    int priority)
{
	int rv = 0;

	__ASSERT_NO_MSG(mgr != NULL);
	__ASSERT_NO_MSG(mgr->vtable != NULL);
	__ASSERT_NO_MSG(mgr->vtable->process != NULL);
	__ASSERT_NO_MSG(op != NULL);

	/* Validation is optional; if present, use it. */
	if (mgr->vtable->validate) {
		rv = mgr->vtable->validate(mgr, op);
	}

	/* Set the priority, checking whether it's in range. */
	if (rv == 0) {
		rv = op_set_priority(op, priority);
	}

	/* Reject callback notifications without translation
	 * function.
	 */
	if ((rv == 0)
	    && async_notify_uses_callback(&op->notify)
	    && (mgr->vtable->callback == NULL)) {
		rv = -ENOTSUP;
	}

	if (rv < 0) {
		return rv;
	}

	if (atomic_cas((atomic_t *)&mgr->current, (atomic_t)NULL, (atomic_t)op)) {
		mgr->vtable->process(mgr, op);
		return rv;
	}

	k_spinlock_key_t key = k_spin_lock(&mgr->lock);
	sys_slist_t *list = &mgr->operations;
	struct queued_operation *prev = NULL;
	struct queued_operation *tmp;

	SYS_SLIST_FOR_EACH_CONTAINER(list, tmp, node) {
		if (priority < op_get_priority(tmp)) {
			break;
		}
		prev = tmp;
	}

	if (prev == NULL) {
		sys_slist_prepend(list, &op->node);
	} else {
		sys_slist_insert(list, &prev->node, &op->node);
	}
	k_spin_unlock(&mgr->lock, key);
	
	return rv;
}

static inline void finalize_and_notify(struct queued_operation_manager *mgr,
				       struct queued_operation *op,
				       int res)
{
	async_notify_generic_callback cb = async_notify_finalize(&op->notify, res);

	if (cb != NULL) {
		mgr->vtable->callback(mgr, op, cb);
	}
}

void queued_operation_finalize(struct queued_operation_manager *mgr,
			       struct queued_operation *op,
			       int res)
{

	__ASSERT_NO_MSG(mgr != NULL);
	__ASSERT_NO_MSG(op != NULL);

	/* Set to invalid address to keep it busy. */
	mgr->current = INVALID_ADDR;

	finalize_and_notify(mgr, op, res);

	select_next(mgr);
}

int queued_operation_cancel(struct queued_operation_manager *mgr,
			    struct queued_operation *op)
{
	int rv = 0;
	k_spinlock_key_t key = k_spin_lock(&mgr->lock);

	if (op == mgr->current) {
		rv = -EINPROGRESS;
	} else if (!sys_slist_find_and_remove(&mgr->operations, &op->node)) {
		rv = -EINVAL;
	}

	k_spin_unlock(&mgr->lock, key);

	if (rv == 0) {
		finalize_and_notify(mgr, op, -ECANCELED);
	}

	return rv;
}

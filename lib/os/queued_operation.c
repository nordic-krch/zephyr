/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <sys/queued_operation.h>

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

static inline bool can_start(/*const*/ struct queued_operation_manager *mgr)
{
	/* OK if not busy, not finalizing, and an op is available. */
	return (mgr->current == NULL)
	       && !mgr->finalizing
	       && !sys_slist_is_empty(&mgr->operations);
}

static void select_next_and_unlock(struct queued_operation_manager *mgr,
				   k_spinlock_key_t key)
{
	struct queued_operation *op = NULL;

	/* Track whether the manager is idle, so we only send
	 * notification of entry to idle once.
	 */
	bool in_idle = (mgr->current == NULL);

	do {
		/* Can't select a new operation if one is active. */
		if (mgr->current != NULL) {
			break;
		}

		/* Can't select a new operation until the previous one
		 * completes finalization, lest we pick something that
		 * has too low a priority.
		 */
		if (mgr->finalizing) {
			break;
		}

		sys_snode_t *node = sys_slist_get(&mgr->operations);

		if (node) {
			op = CONTAINER_OF(node, struct queued_operation, node);
			mgr->current = op;
		} else {
			op = NULL;
		}

		k_spin_unlock(&mgr->lock, key);

		/* Only notify the manager if there's an operation, or
		 * if it is to transition to idle.
		 */
		if ((op != NULL) || !in_idle) {
			mgr->vtable->process(mgr, op);
		}
		in_idle = (op == NULL);

		key = k_spin_lock(&mgr->lock);
	} while (op != NULL);

	k_spin_unlock(&mgr->lock, key);
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

	select_next_and_unlock(mgr, key);

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
	k_spinlock_key_t key = k_spin_lock(&mgr->lock);

	__ASSERT_NO_MSG(mgr != NULL);
	__ASSERT_NO_MSG(op != NULL);

	if (mgr->current == op) {
		mgr->finalizing = true;
		mgr->current = NULL;
	}

	k_spin_unlock(&mgr->lock, key);

	finalize_and_notify(mgr, op, res);

	key = k_spin_lock(&mgr->lock);

	mgr->finalizing = false;

	if (can_start(mgr)) {
		select_next_and_unlock(mgr, key);
	} else {
		k_spin_unlock(&mgr->lock, key);
	}
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

/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/qop_mngr.h>

static void op_perform_notify(struct qop_mngr *mngr, int res)
{
	k_spinlock_key_t key = k_spin_lock(&mngr->lock);
	bool trigger = false;
	sys_snode_t *node = sys_slist_get(&mngr->ops);
	struct qop_op *op = CONTAINER_OF(node, struct qop_op, node);
	if (sys_slist_is_empty(&mngr->ops)) {
		trigger = true;
	}
	k_spin_unlock(&mngr->lock, key);

	if (trigger) {
		mngr->op_perform(mngr, op_perform_notify);
	}

	async_client_notify(mngr, op, &op->async_cli, res);
}

static void list_append(sys_slist_t *list, struct qop_op *op)
{
	/* priorities not supported */
	sys_slist_append(list, &op->node);
}

int qop_op_schedule(struct qop_mngr *mngr, struct qop_op *op)
{
	bool trigger = false;
	k_spinlock_key_t key = k_spin_lock(&mngr->lock);

	if (sys_slist_is_empty(&mngr->ops)) {
		trigger = true;
	}

	list_append(&mngr->ops, op);

	k_spin_unlock(&mngr->lock, key);

	return trigger ? mngr->op_perform(mngr, op_perform_notify) : 0;
}

int qop_op_cancel(struct qop_mngr *mngr, struct qop_op *op)
{
	int rv = 0;
	k_spinlock_key_t key = k_spin_lock(&mngr->lock);
	if (sys_slist_peek_head(&mngr->ops) == &op->node) {
		/* head - in progress */
		rv = -EINPROGRESS;
	}
	else if (sys_slist_find_and_remove(&mngr->ops, &op->node) == false) {
		/* not found */
		rv = -EINVAL;
	}

	k_spin_unlock(&mngr->lock, key);
	return rv;
}

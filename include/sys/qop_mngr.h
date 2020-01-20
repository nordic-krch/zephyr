/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SYS_QOP_MNGR_H_
#define ZEPHYR_INCLUDE_SYS_QOP_MNGR_H_

#include <kernel.h>
#include <zephyr/types.h>
#include <sys/async_client.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QOP_MNGR_PRI_LOWEST 0
#define QOP_MNGR_PRI_HIGHEST UINT8_MAX

#define QOP_MNGR_FLAGS_OP_SLEEPS	BIT(0)
#define QOP_MNGR_FLAGS_PRI		BIT(1)

struct qop_mngr;

typedef void (*qop_mngr_notify_fn)(struct qop_mngr *mngr, int res);
typedef int (*qop_mngr_fn)(struct qop_mngr *mngr, qop_mngr_notify_fn notify);

struct qop_mngr {
	sys_slist_t ops;
	qop_mngr_fn op_perform;
	struct k_spinlock lock;
	void *data;
	u16_t flags;
};

struct qop_op;

typedef void (*qop_op_callback)(struct qop_mngr *mngr, struct qop_op *op,
				int res);

struct qop_op {
	/* Links the client into the set of waiting service users. */
	sys_snode_t node;

	struct async_client async_cli;

	void *data;
};

static inline int qop_op_fetch_result(const struct qop_op *op,
					    int *result)
{
	return async_client_fetch_result(&op->async_cli, result);
}

static inline void qop_op_init_spinwait(struct qop_op *op)
{
	async_client_init_spinwait(&op->async_cli);
}

static inline void qop_op_init_signal(struct qop_op *op,
				      struct k_poll_signal *sigp)
{
	async_client_init_signal(&op->async_cli, sigp);
}

static inline void qop_op_init_callback(struct qop_op *op,
					      async_client_callback handler,
					      void *user_data)
{
	async_client_init_callback(&op->async_cli, handler, user_data);
}

int qop_op_init(struct qop_mngr *mngr, qop_mngr_fn perform_fn, u16_t flags);
int qop_op_schedule(struct qop_mngr *mngr, struct qop_op *op);
int qop_op_cancel(struct qop_mngr *mngr, struct qop_op *op);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_QOP_MNGR_H_ */

/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <sys/queued_operation.h>
#include <hal/nrf_gpio.h>

struct test_op {
	struct queued_operation op;
	u8_t id;
};

void process(struct queued_operation_manager *mgr,
		struct queued_operation *op)
{
	//printk("processed\n");
	if (op) {
		//struct test_op *test_op = CONTAINER_OF(op, struct test_op, op);
		//printk("process %d \n", test_op->id);
		queued_operation_finalize(mgr, op, 0);
	} else {
		//printk("termination\n");
	}
}

const struct queued_operation_functions fn = { .process = process };



struct queued_operation_manager qop_mngr = {
	.vtable = &fn
};


void main(void)
{
	printk("Hello World! %s\n", CONFIG_BOARD);

	struct test_op test_op0 = {.id = 0};
	struct test_op test_op1 = {.id = 1};

	async_notify_init_spinwait(&test_op0.op.notify);
	async_notify_init_spinwait(&test_op1.op.notify);

	nrf_gpio_cfg_output(27);
	nrf_gpio_pin_set(27);
	int err = queued_operation_submit(&qop_mngr, &test_op0.op, 0);
	//err = queued_operation_submit(&qop_mngr, &test_op1.op, 0);
	nrf_gpio_pin_clear(27);
	printk("err: %d\n", err);
}

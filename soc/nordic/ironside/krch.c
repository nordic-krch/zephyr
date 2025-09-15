/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <nrf_ironside/krch.h>
#include <nrf_ironside/call.h>

/** Service ID for KRCH service */
#define IRONSIDE_CALL_ID_KRCH_SERVICE_V0 100
#define IRONSIDE_CALL_ID_KRCH_SERVICE_READ_V0 101

/** @brief Request argument indices */
enum {
	/** Target address for write operation */
	IRONSIDE_KRCH_REQ_ADDRESS_IDX,
	/** 32-bit value to write */
	IRONSIDE_KRCH_REQ_VALUE_IDX,
	/** Number of request arguments */
	IRONSIDE_KRCH_REQ_NUM_ARGS,
};

/** @brief Response argument indices */
enum {
	/** Return code */
	IRONSIDE_KRCH_RSP_RETCODE_IDX,
	IRONSIDE_KRCH_RSP_READ_IDX,
	/** Number of response arguments */
	IRONSIDE_KRCH_RSP_NUM_ARGS,
};

int ironside_krch_memory_write(uint32_t address, uint32_t value)
{
	struct ironside_call_buf *buf;
	int err;

	buf = ironside_call_alloc();

	buf->id = IRONSIDE_CALL_ID_KRCH_SERVICE_V0;
	buf->args[IRONSIDE_KRCH_REQ_ADDRESS_IDX] = address;
	buf->args[IRONSIDE_KRCH_REQ_VALUE_IDX] = value;

	ironside_call_dispatch(buf);

	if (buf->status == IRONSIDE_CALL_STATUS_RSP_SUCCESS) {
		err = (int)buf->args[IRONSIDE_KRCH_RSP_RETCODE_IDX];
	} else {
		err = -buf->status;
	}

	ironside_call_release(buf);

	return err;
}

int ironside_krch_memory_read(uint32_t address, uint32_t *value)
{
	struct ironside_call_buf *buf;
	int err;

	if (value == NULL) {
		return -EINVAL;
	}

	buf = ironside_call_alloc();

	buf->id = IRONSIDE_CALL_ID_KRCH_SERVICE_READ_V0;
	buf->args[IRONSIDE_KRCH_REQ_ADDRESS_IDX] = address;

	ironside_call_dispatch(buf);

	if (buf->status == IRONSIDE_CALL_STATUS_RSP_SUCCESS) {
		*value = buf->args[IRONSIDE_KRCH_RSP_READ_IDX];
		err = (int)buf->args[IRONSIDE_KRCH_RSP_RETCODE_IDX];
	} else {
		err = -buf->status;
	}

	ironside_call_release(buf);

	return err;
}

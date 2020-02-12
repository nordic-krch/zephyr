/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <sys/async_notify.h>

int async_notify_validate(struct async_notify *notify)
{
	int rv = 0;

	if (notify == NULL) {
		return -EINVAL;
	}

	/* Validate configuration based on mode */
	switch (async_notify_get_method(notify)) {
	case ASYNC_NOTIFY_METHOD_SPINWAIT:
		break;
	case ASYNC_NOTIFY_METHOD_CALLBACK:
		if (notify->method.callback == NULL) {
			rv = -EINVAL;
		}
		break;
	case ASYNC_NOTIFY_METHOD_SIGNAL:
		if (notify->method.signal == NULL) {
			rv = -EINVAL;
		}
		break;
	default:
		rv = -EINVAL;
		break;
	}

	/* Clear the result here instead of in all callers. */
	if (rv == 0) {
		notify->result = 0;
	}

	return rv;
}

async_notify_generic_callback async_notify_finalize(struct async_notify *notify,
						    int res)
{
	async_notify_generic_callback rv = 0;
	u32_t method = async_notify_get_method(notify);

	/* Store the result, record completion, and notify if requested. */
	notify->result = res;
	notify->flags &= ASYNC_NOTIFY_EXTENSION_MASK;
	switch (method) {
	case ASYNC_NOTIFY_METHOD_SPINWAIT:
		break;
	case ASYNC_NOTIFY_METHOD_CALLBACK:
		rv = notify->method.callback;
		break;
#ifdef CONFIG_POLL
	case ASYNC_NOTIFY_METHOD_SIGNAL:
		k_poll_signal_raise(notify->method.signal, res);
		break;
#endif /* CONFIG_POLL */
	default:
		__ASSERT_NO_MSG(false);
	}

	return rv;
}

/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SYS_ASYNC_NOTIFY_H_
#define ZEPHYR_INCLUDE_SYS_ASYNC_NOTIFY_H_

#include <kernel.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup async_notify_apis Asynchronous Notification APIs
 * @ingroup kernel_apis
 * @{
 */

/* Forward declaration */
struct async_notify;

/*
 * Flag value that overwrites the method field when the operation has
 * completed.
 */
#define ASYNC_NOTIFY_METHOD_COMPLETED 0

/*
 * Indicates that no notification will be provided.
 *
 * Callers must check for completions using
 * async_notify_fetch_result().
 *
 * See async_notify_init_spinwait().
 */
#define ASYNC_NOTIFY_METHOD_SPINWAIT 1

/*
 * Select notification through @ref k_poll signal
 *
 * See async_notify_init_signal().
 */
#define ASYNC_NOTIFY_METHOD_SIGNAL 2

/*
 * Select notification through a user-provided callback.
 *
 * See async_notify_init_callback().
 */
#define ASYNC_NOTIFY_METHOD_CALLBACK 3

#define ASYNC_NOTIFY_METHOD_MASK 0x03
#define ASYNC_NOTIFY_METHOD_POS 0

/**
 * @brief Identify the region of async_notify flags available for
 * containing services.
 *
 * Bits of the flags field of the async_notify structure at and above
 * this position may be used by extensions to the async_notify
 * structure.
 *
 * These bits are intended for use by containing service
 * implementations to record client-specific information.  The bits
 * are cleared by async_notify_validate().  Use of these does not
 * imply that the flags field becomes public API.
 */
#define ASYNC_NOTIFY_EXTENSION_POS 3

/*
 * Mask isolating the bits of async_notify::flags that are available
 * for extension.
 */
#define ASYNC_NOTIFY_EXTENSION_MASK (~(BIT(ASYNC_NOTIFY_EXTENSION_POS) - 1U))

/**
 * @brief Generic signature used to notify of result completion by
 * callback.
 *
 * Functions with this role may be invoked from any context including
 * pre-kernel, ISR, or cooperative or pre-emptible threads.
 * Compatible functions must be isr-ok and not sleep.
 *
 * Parameters that should generally be passed to such functions include:
 *
 * * a pointer to a specific client request structure, i.e. the one
 *   that contains the async_notify structure.
 * * the result of the operation, either as passed to
 *   async_notify_finalize() or extracted afterwards using
 *   async_notify_fetch_result().  Expected values are
 *   service-specific, but the value shall be non-negative if the
 *   operation succeeded, and negative if the operation failed.
 */
typedef void (*async_notify_generic_callback)();

/**
 * @brief State associated with notification for an asynchronous
 * operation.
 *
 * Objects of this type are allocated by a client, which must use an
 * initialization function (e.g. async_notify_init_signal()) to
 * configure them.  Generally the structure is a member of a
 * service-specific client structure, such as onoff_client.
 *
 * Control of the containing object transfers to the service provider
 * when a pointer to the object is passed to a service function that
 * is documented to take control of the object, such as
 * onoff_service_request().  While the service provider controls the
 * object the client must not change any object fields.  Control
 * reverts to the client:
 * * if the call to the service API returns an error;
 * * if the call to the service API succeeds for a no-wait operation;
 * * when operation completion is posted (signalled or callback
 *   invoked).
 *
 * After control has reverted to the client the notify object must be
 * reinitialized for the next operation.
 *
 * The content of this structure is not public API to clients: all
 * configuration and inspection should be done with functions like
 * async_notify_init_callback() and async_notify_fetch_result().
 * However, services that use this structure may access certain
 * fields directly.
 */
struct async_notify {
	union method {
		/* Pointer to signal used to notify client.
		 *
		 * The signal value corresponds to the res parameter
		 * of async_notify_callback.
		 */
		struct k_poll_signal *signal;

		/* Generic callback function for callback notification. */
		async_notify_generic_callback callback;
	} method;

	/*
	 * Flags recording information about the operation.
	 *
	 * Bits below ASYNC_NOTIFY_EXTENSION_POS must not by modified
	 * by extensions.
	 *
	 * Bits at and above ASYNC_NOTIFY_EXTENSION_POS are available
	 * for use by service extensions while the containing object
	 * is managed by the service.  They are not for client use,
	 * and are cleared before completion notfication.
	 */
	u32_t volatile flags;

	/*
	 * The result of the operation.
	 *
	 * This is the value that was (or would be) passed to the
	 * async infrastructure.  This field is the sole record of
	 * success or failure for no-wait synchronous operations.
	 */
	int volatile result;
};

/** @internal */
static inline u32_t async_notify_get_method(const struct async_notify *notify)
{
	u32_t method = notify->flags >> ASYNC_NOTIFY_METHOD_POS;

	return method & ASYNC_NOTIFY_METHOD_MASK;
}

/**
 * @brief Validate and initialize the notify structure.
 *
 * This should be invoked at the start of any service-specific
 * configuration validation.  It ensures that the asynchronous
 * notification configuration is valid and clears the result.
 *
 * Be aware that on return of success all extensible bits of the flags
 * field will have been cleared; the caller must initialize them after
 * invoking this function.
 *
 * @retval 0 on successful validation and reinitialization
 * @retval -EINVAL if the configuration is not valid.
 */
int async_notify_validate(struct async_notify *notify);

/**
 * @brief Record and signal the operation completion.
 *
 * @param notify pointer to the notification state structure.
 *
 * @param res the result of the operation.  Expected values are
 * service-specific, but the value shall be non-negative if the
 * operation succeeded, and negative if the operation failed.
 *
 * @return If the notification is to be done by callback this returns
 * the generic version of the function to be invoked.  The caller must
 * immediately invoke that function with whatever arguments are
 * expected by the callback.  If notification is by spin-wait or
 * signal, the notification has been completed by the point this
 * function returns.
 */
async_notify_generic_callback async_notify_finalize(struct async_notify *notify,
						    int res);

/**
 * @brief Check for and read the result of an asynchronous operation.
 *
 * @param notify pointer to the object used to specify asynchronous
 * function behavior and store completion information.
 *
 * @param result pointer to storage for the result of the operation.
 * The result is stored only if the operation has completed.
 *
 * @retval 0 if the operation has completed.
 * @retval -EAGAIN if the operation has not completed.
 */
static inline int async_notify_fetch_result(const struct async_notify *notify,
					    int *result)
{
	__ASSERT_NO_MSG(notify != NULL);
	__ASSERT_NO_MSG(result != NULL);
	int rv = -EAGAIN;

	if (async_notify_get_method(notify) == ASYNC_NOTIFY_METHOD_COMPLETED) {
		rv = 0;
		*result = notify->result;
	}
	return rv;
}

/**
 * @brief Initialize a notify object for spin-wait notification.
 *
 * Clients that use this initialization receive no asynchronous
 * notification, and instead must periodically check for completion
 * using async_notify_fetch_result().
 *
 * On completion of the operation the client object must be
 * reinitialized before it can be re-used.
 *
 * @param notify pointer to the notification configuration object.
 */
static inline void async_notify_init_spinwait(struct async_notify *notify)
{
	__ASSERT_NO_MSG(notify != NULL);

	*notify = (struct async_notify){
		.flags = ASYNC_NOTIFY_METHOD_SPINWAIT,
	};
}

/**
 * @brief Initialize a notify object for (k_poll) signal notification.
 *
 * Clients that use this initialization will be notified of the
 * completion of operations through the provided signal.
 *
 * On completion of the operation the client object must be
 * reinitialized before it can be re-used.
 *
 * @note
 *   @rst
 *   This capability is available only when :option:`CONFIG_POLL` is
 *   selected.
 *   @endrst
 *
 * @param notify pointer to the notification configuration object.
 *
 * @param sigp pointer to the signal to use for notification.  The
 * value must not be null.  The signal must be reset before the client
 * object is passed to the on-off service API.
 */
static inline void async_notify_init_signal(struct async_notify *notify,
					    struct k_poll_signal *sigp)
{
	__ASSERT_NO_MSG(notify != NULL);
	__ASSERT_NO_MSG(sigp != NULL);

	*notify = (struct async_notify){
#ifdef CONFIG_POLL
		.method = {
			.signal = sigp,
		},
#endif /* CONFIG_POLL */
		.flags = ASYNC_NOTIFY_METHOD_SIGNAL,
	};
}

/**
 * @brief Initialize a notify object for callback notification.
 *
 * Clients that use this initialization will be notified of the
 * completion of operations through the provided callback.  Note that
 * callbacks may be invoked from various contexts depending on the
 * specific service; see @ref async_notify_generic_callback.
 *
 * On completion of the operation the client object must be
 * reinitialized before it can be re-used.
 *
 * @param notify pointer to the notification configuration object.
 *
 * @param handler a function pointer to use for notification.
 */
static inline void async_notify_init_callback(struct async_notify *notify,
					      async_notify_generic_callback handler)
{
	__ASSERT_NO_MSG(notify != NULL);
	__ASSERT_NO_MSG(handler != NULL);

	*notify = (struct async_notify){
		.method = {
			.callback = handler,
		},
		.flags = ASYNC_NOTIFY_METHOD_CALLBACK,
	};
}

/**
 * @brief Detect whether a particular notification uses a callback.
 *
 * The generic handler does not capture the signature expected by the
 * callback, and the translation to a service-specific callback must
 * be provided by the service.  This check allows abstracted services
 * to reject callback notification requests when the service doesn't
 * provide a translation function.
 *
 * @return true if and only if a callback is to be used for notification.
 */
static inline bool async_notify_uses_callback(const struct async_notify *notify)
{
	__ASSERT_NO_MSG(notify != NULL);

	return async_notify_get_method(notify) == ASYNC_NOTIFY_METHOD_CALLBACK;
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_ASYNC_NOTIFY_H_ */

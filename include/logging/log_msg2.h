/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_LOGGING_LOG_MSG2_H_
#define ZEPHYR_INCLUDE_LOGGING_LOG_MSG2_H_

#include <logging/log_instance.h>
#include <sys/mpsc_packet.h>
#include <sys/cbprintf.h>
#include <sys/atomic.h>
#include <sys/util.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_LOG_TIMESTAMP_64BIT
typedef uint64_t log_timestamp_t;
#else
typedef uint32_t log_timestamp_t;
#endif

/**
 * @brief Log message API
 * @defgroup log_msg Log message API
 * @ingroup logger
 * @{
 */

#define Z_LOG_MSG2_LOG 0
#define Z_LOG_MSG2_TRACE 1

#define LOG_MSG2_GENERIC_HDR \
	MPSC_PBUF_HDR;\
	uint32_t type:1

struct log_msg2_desc {
	LOG_MSG2_GENERIC_HDR;
	uint32_t domain:3;
	uint32_t level:3;
	uint32_t package_len:10;
	uint32_t data_len:12;
	uint32_t reserved:1;
};

struct log_msg2_trace_hdr {
	LOG_MSG2_GENERIC_HDR;
	uint32_t evt_id:5;
#if CONFIG_LOG_TRACE_SHORT_TIMESTAMP
	uint32_t timestamp:24;
#else
	log_timestamp_t timestamp;
#endif
};

union log_msg2_source {
	const struct log_source_const_data *fixed;
	struct log_source_dynamic_data *dynamic;
	void *raw;
};

struct log_msg2_hdr {
	struct log_msg2_desc desc;
	void *source;
	log_timestamp_t timestamp;
};

struct log_msg2_trace {
	struct log_msg2_trace_hdr hdr;
};

struct log_msg2_trace_ptr {
	struct log_msg2_trace_hdr hdr;
	void *ptr;
};

struct log_msg2 {
	struct log_msg2_hdr hdr;
	uint8_t data[];
};

struct log_msg2_generic_hdr {
	LOG_MSG2_GENERIC_HDR;
};

union log_msg2_generic {
	union mpsc_pbuf_generic buf;
	struct log_msg2_generic_hdr generic;
	struct log_msg2_trace trace;
	struct log_msg2_trace_ptr trace_ptr;
	struct log_msg2 log;
};

enum tracing_log_id {
	TRACING_LOG_THREAD_SWITCHED_OUT,
	TRACING_LOG_ISR_ENTER,
	TRACING_LOG_ISR_EXIT,
	TRACING_LOG_ISR_EXIT_TO_SCHEDULER,
	TRACING_LOG_IDLE,

	TRACING_LOG_SINGLE_WORD = TRACING_LOG_IDLE,

	/* IDs using additional data */
	TRACING_LOG_THREAD_SWITCHED_IN,
	TRACING_LOG_THREAD_PRIO_SET,
	TRACING_LOG_THREAD_CREATE,
	TRACING_LOG_THREAD_ABORT,
	TRACING_LOG_THREAD_SUSPEND,
	TRACING_LOG_THREAD_RESUME,
	TRACING_LOG_THREAD_READY,
	TRACING_LOG_THREAD_PEND,
	TRACING_LOG_THREAD_INFO,
	TRACING_LOG_THREAD_NAME_SET,
	TRACING_LOG_VOID,
	TRACING_LOG_END_CALL,
	TRACING_LOG_SEMAPHORE_INIT,
	TRACING_LOG_SEMAPHORE_TAKE,
	TRACING_LOG_SEMAPHORE_GIVE,
	TRACING_LOG_MUTEX_INIT,
	TRACING_LOG_MUTEX_LOCK,
	TRACING_LOG_MUTEX_UNLOCK
};

#define Z_LOG_MSG_DESC_INITIALIZER(_domain_id, _level, _plen, _dlen) \
{ \
	.type = Z_LOG_MSG2_LOG, \
	.domain = _domain_id, \
	.level = _level, \
	.package_len = _plen, \
	.data_len = _dlen, \
}

#define Z_LOG_MSG2_STACK_CREATE(_domain_id, _source, _level, \
				_data, _dlen, ...) do { \
	CBPRINTF_STATIC_PACKAGE_SIZE_TOKEN(_v, __VA_ARGS__); \
	static const size_t _psize = \
		CBPRINTF_STATIC_PACKAGE_SIZE(_v, __VA_ARGS__); \
	size_t __psize = _psize; \
	uint8_t __buf[_psize]; \
	if (_psize) CBPRINTF_STATIC_PACKAGE(__buf, __psize, __VA_ARGS__); \
	(void)__psize; \
	if (!_dlen) { \
		static const struct log_msg2_desc __desc = \
		     Z_LOG_MSG_DESC_INITIALIZER(_domain_id, _level, \
						_psize, 0);\
		z_log_msg2_static_create((void *)_source, &__desc, \
					  __buf, NULL); \
	} else { \
		struct log_msg2_desc __desc = \
		     Z_LOG_MSG_DESC_INITIALIZER(_domain_id, _level, \
						_psize, _dlen);\
		z_log_msg2_static_create((void *)_source, &__desc, \
					 __buf, _data); \
	} \
} while (0)

#define Z_LOG_MSG2_SIMPLE_CREATE(_domain_id, _source, _level, ...) do { \
	CBPRINTF_STATIC_PACKAGE_SIZE_TOKEN(_v, __VA_ARGS__); \
	static const size_t __psize = \
		CBPRINTF_STATIC_PACKAGE_SIZE(_v, __VA_ARGS__); \
	size_t ___psize; \
	if (__psize) CBPRINTF_STATIC_PACKAGE(NULL, ___psize, __VA_ARGS__); \
	(void)___psize; \
	size_t __msg_len = __psize + sizeof(struct log_msg2_hdr); \
	struct log_msg2 *__msg = z_log_msg2_alloc(__msg_len);\
	if (!__msg) { \
		break; \
	} \
	CBPRINTF_STATIC_PACKAGE(__msg->data, ___psize, __VA_ARGS__); \
	static const struct log_msg2_desc __desc = \
		Z_LOG_MSG_DESC_INITIALIZER(_domain_id, _level, __psize, 0);\
	z_log_msg2_static_finalize(__msg, (void *)_source, &__desc); \
} while (0)

/** @brief Create log message and write it into the logger buffer.
 *
 * Macro handles creation of log message which includes storing log message
 * description, timestamp, arguments, copying string arguments into message and
 * copying user data into the message space. The are 3 modes of message
 * creation:
 * - at compile time message size is determined, message is allocated and
 *   content is written directly to the message. It is the fastest but cannot be
 *   used in user mode. Message size cannot be determined at compile time if it
 *   contains data or string arguments which are string pointers.
 * - at compile time message size is determined, string package is created on
 *   stack, message is created in function call. String package can only be
 *   created on stack if it does not contain unexpected pointers to strings.
 * - string package is created at runtime. This mode has no limitations but
 *   it is significantly slower.
 *
 * @param _try_0cpy If positive then, if possible, message content is written
 * directly to message. If 0 then, if possible, string package is created on
 * the stack and message is created in the function call.
 *
 * @param _mode Used for testing. It is set according to message creation mode
 *		used.
 *
 * @param _cstr_cnt Number of constant strings present in the string. It is
 * used to help detect messages which must be runtime processed, compared to
 * message which can be prebuilt at compile time.
 *
 * @param _domain_id Domain ID.
 *
 * @param _source Pointer to the constant descriptor of the log message source.
 *
 * @param _level Log message level.
 *
 * @param _data Pointer to the data. Can be null.
 *
 * @param _dlen Number of data bytes. 0 if data is not provided.
 *
 * @param ...  String with arguments (fmt, ...).
 */

#define Z_LOG_MSG2_CREATE(_try_0cpy, _mode,  _cstr_cnt, _domain_id, _source,\
	       		  _level, _data, _dlen, ...) do { \
	if (IS_ENABLED(CONFIG_LOG_SPEED) && _try_0cpy && !_dlen && \
	    !(CBPRINTF_MUST_RUNTIME_PACKAGE(_cstr_cnt, __VA_ARGS__))) {\
		/* static msg */ \
		Z_LOG_MSG2_SIMPLE_CREATE(_domain_id, _source, \
					_level, __VA_ARGS__); \
		_mode = 1; \
	} else if (CBPRINTF_MUST_RUNTIME_PACKAGE(_cstr_cnt, __VA_ARGS__)) {\
		z_log_msg2_runtime_create(_domain_id, (void *)_source, _level, \
					  _data, _dlen, __VA_ARGS__); \
		_mode = 2; \
	} else { \
		Z_LOG_MSG2_STACK_CREATE(_domain_id, _source, _level, \
					_data, _dlen, __VA_ARGS__); \
		_mode = 3; \
	} \
	(void)_mode; \
} while (0)

#define Z_TRACING_LOG_HDR_INIT(name, id) \
	struct log_msg2_trace name = { \
		.hdr = { \
			.type = Z_LOG_MSG2_TRACE, \
			.valid = 1, \
			.busy = 0, \
			.evt_id = id, \
		} \
	}

/** @brief Finalize simple message.
 *
 * Finalization includes setting source, fmt and timestamp in the message
 * followed by commiting the message to the buffer.
 *
 * @param msg Message.
 *
 * @param source Address of the source descriptor.
 *
 * @param desc Message descriptor.
 */
void z_log_msg2_static_finalize(struct log_msg2 *msg,
			   	void *source,
				const struct log_msg2_desc *desc);

/** @brief Create simple message from message details and string package.
 *
 * @param source Source.
 *
 * @param desc Message descriptor.
 *
 * @package Package.
 *
 * @oaram data Data.
 */
void z_log_msg2_static_create(void *source,
			      const struct log_msg2_desc *desc,
			      uint8_t *package, uint8_t *data);

void z_log_msg2_runtime_vcreate(uint8_t domain_id, void *source,
			       uint8_t level, uint8_t *data, size_t dlen,
			       const char *fmt, va_list ap);

/** @brief Create message at runtime.
 *
 * Function allows to build any log message based on input data. Processing
 * time is significantly higher than statically message creating.
 *
 * @param domain_id Domain ID.
 *
 * @param source Source.
 *
 * @param level Log level.
 *
 * @param data Data.
 *
 * @param dlen Data length.
 *
 * @param fmt String.
 *
 * @param ... String arguments.
 */
static inline void z_log_msg2_runtime_create(uint8_t domain_id, void *source,
					     uint8_t level, uint8_t *data,
					     size_t dlen, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	z_log_msg2_runtime_vcreate(domain_id, source, level,
				   data, dlen, fmt, ap);
	va_end(ap);
}

static inline bool z_log_item_is_msg(union log_msg2_generic *msg)
{
	return msg->generic.type == Z_LOG_MSG2_LOG;
}
/** @brief Get total length of the log message.
 *
 * @param desc Log message descriptor.
 *
 * @return Length.
 */
static inline size_t log_msg2_get_total_len(const struct log_msg2_desc *desc)
{

	return sizeof(struct log_msg2_hdr) + desc->package_len + desc->data_len;
}

/** @brief Get length of the log item.
 *
 * @param item Item.
 *
 * @return Length.
 */
static inline size_t log_msg2_generic_get_len(union mpsc_pbuf_generic *item)
{
	union log_msg2_generic *generic_msg = (union log_msg2_generic *)item;
	if (z_log_item_is_msg(generic_msg)) {
		struct log_msg2 *msg = (struct log_msg2 *)generic_msg;

		return log_msg2_get_total_len(&msg->hdr.desc);
	} else {
		/* trace TODO */
		return 0;
	}
}

/** @brief Get log message domain ID.
 *
 * @param msg Log message.
 *
 * @return Domain ID
 */
static inline uint8_t log_msg2_get_domain(struct log_msg2 *msg)
{
	return msg->hdr.desc.domain;
}

/** @brief Get log message level.
 *
 * @param msg Log message.
 *
 * @return Log level.
 */
static inline uint8_t log_msg2_get_level(struct log_msg2 *msg)
{
	return msg->hdr.desc.level;
}

/** @brief Get message source data.
 *
 * @param msg Log message.
 *
 * @return Pointer to the source data.
 */
static inline void *log_msg2_get_source(struct log_msg2 *msg)
{
	return msg->hdr.source;
}

/** @brief Get timestamp.
 *
 * @param msg Log message.
 *
 * @return Timestamp.
 */
static inline log_timestamp_t log_msg2_get_timestamp(struct log_msg2 *msg)
{
	return msg->hdr.timestamp;
}

/** @brief Get data buffer.
 *
 * @param msg log message.
 *
 * @param len location where data length is written.
 *
 * @return pointer to the data buffer.
 */
static inline uint8_t *log_msg2_get_data(struct log_msg2 *msg, size_t *len)
{
	*len = msg->hdr.desc.data_len;

	return msg->data + msg->hdr.desc.package_len;
}

/** @brief Get string package.
 *
 * @param msg log message.
 *
 * @param len location where string package length is written.
 *
 * @return pointer to the package.
 */
static inline uint8_t *log_msg2_get_package(struct log_msg2 *msg, size_t *len)
{
	*len = msg->hdr.desc.package_len;

	return msg->data;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_LOGGING_LOG_MSG2_H_ */

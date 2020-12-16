/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SYS_CBPRINTF_INTERNAL_H_
#define ZEPHYR_INCLUDE_SYS_CBPRINTF_INTERNAL_H_

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <toolchain.h>
#include <sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Return 1 if argument is a pointer to char or wchar_t
 *
 * @param x argument.
 *
 * @return 1 if char * or wchar_t *, 0 otherwise.
 */
#define Z_CBPRINTF_IS_PCHAR(x) _Generic((x), \
			char *: 1, \
			const char *: 1, \
			volatile char *: 1, \
			const volatile char *: 1, \
			wchar_t *: 1, \
			const wchar_t *: 1, \
			volatile wchar_t *: 1, \
			const volatile wchar_t *: 1, \
			default: 0)

/** @brief Calculate number of char * or wchar_t * arguments in the arguments.
 *
 * @param fmt string.
 *
 * @param ... string arguments.
 *
 * @return number of arguments which are char * or wchar_t *.
 */
#define Z_CBPRINTF_HAS_PCHAR_ARGS_(fmt, ...) \
	(FOR_EACH(Z_CBPRINTF_IS_PCHAR, (+), __VA_ARGS__))

/**
 * @brief Check if formatted string must be packaged in runtime.
 *
 * @param skip number of char/wchar_t pointers in the argument list which are
 * accepted for static packaging.
 *
 * @param ... String with arguments (fmt, ...).
 *
 * @retval 1 if string must be packaged at runtime.
 * @retval 0 if string can be statically packaged.
 */
#define Z_CBPRINTF_MUST_RUNTIME_PACKAGE(skip, ...) \
	COND_CODE_0(NUM_VA_ARGS_LESS_1(__VA_ARGS__), \
			(0), \
			((Z_CBPRINTF_HAS_PCHAR_ARGS_(__VA_ARGS__) - skip) > 0 ?\
			 1 : 0))

/** @brief Get storage size for given argument.
 *
 * Floats are promoted to double so they use size of double, others int storage
 * or it's own storage size if it is bigger than int. Strings are stored in
 * the package with 1 byte header indicating if string is stored as pointer or
 * by value.
 *
 * @param x argument.
 *
 * @return Number of bytes used for storing the argument.
 */
#define Z_CBPRINTF_ARG_SIZE(x) _Generic((x), \
	float:  sizeof(double), \
        const float:  sizeof(double), \
        volatile float:  sizeof(double), \
        const volatile float:  sizeof(double), \
	void *: sizeof(void *), \
	const char *: (1 + sizeof(const char *)), \
	char *: (1 + sizeof(const char *)), \
	default: MAX(sizeof(int), sizeof((x))))

/** @brief Get storage size in words.
 *
 * @param x argument.
 *
 * @return number of words needed for storage.
 */
#define Z_CBPRINTF_ARG_WSIZE(x) (Z_CBPRINTF_ARG_SIZE(x) / sizeof(int))

/** @brief Packaging of a void pointer. */
#define Z_CBPRINTF_PACK_VOID_PTR(buf, x) do { \
	const void *_p = (void *)_Generic((x), \
				void *: x, \
				volatile void *: x, \
				const void *: x, \
				const volatile void *: x, \
				default: NULL); \
	*(const void **)buf = _p;\
} while (0)

/** @brief Packaging of a float. */
#define Z_CBPRINTF_PACK_FLOAT(buf, x) do { \
	double _x = _Generic((x), \
			float: (x), \
			default: 0.0); \
	*(double *)buf = _x; \
} while (0)

/** @brief packaging of variables smaller than integer size. */
#define Z_CBPRINTF_PACK_SHORT(buf, x) do { \
	int _x = _Generic((x), \
			char: (x), \
			signed char: (x), \
			short: (x), \
			default: 0); \
	*(int *)buf = _x; \
} while (0)

/** @brief Packaging of char pointer. */
#define Z_CBPRINTF_PACK_CHAR_PTR(buf, x) do { \
	uint8_t *_buf = buf; \
	const char *_x = _Generic((x), \
			char *: (x), \
			const char *: (x), \
			default: NULL); \
	_buf[0] = 0x00; \
	*(const char **)&_buf[1] = _x; \
} while (0)

/** @brief Packaging of an argument of a non-special type. */
#define Z_CBPRINTF_PACK_GENERIC(buf, x) do { \
	__auto_type _x = x; \
	if (sizeof(_x) > sizeof(uintptr_t)) { \
		memcpy(buf, (void *)&_x, sizeof(_x)); \
	} else { \
		*(uintptr_t *)buf = (uintptr_t)_x; \
	} \
} while (0)

/** @brief Macro for packaging single argument.
 *
 * Macro handles special cases:
 * - promotions of arguments smaller than int
 * - promotion of float
 * - char * packaging which is prefixed with null byte.
 * - special treatment of void * which would generate warning on arithmetic
 *   operation ((void *)ptr + 0).
 */
#define Z_CBPRINTF_PACK(buf, x) \
	_Generic ((x), \
		char: ({ Z_CBPRINTF_PACK_SHORT(buf, x);}), \
		signed char: ({ Z_CBPRINTF_PACK_SHORT(buf, x);}), \
		short: ({ Z_CBPRINTF_PACK_SHORT(buf, x);}), \
		void *: ({Z_CBPRINTF_PACK_VOID_PTR(buf, x);}), \
		float: ({Z_CBPRINTF_PACK_FLOAT(buf, x);}), \
		char *: ({Z_CBPRINTF_PACK_CHAR_PTR(buf, x);}), \
		const char *: ({Z_CBPRINTF_PACK_CHAR_PTR(buf, x);}), \
		default: ({Z_CBPRINTF_PACK_GENERIC(buf, x);}))

/** @brief Safely package arguments to a buffer.
 *
 * Argument is put into the buffer if capable buffer is provided. Length is
 * incremented even if data is not packaged.
 *
 * @param _buf buffer.
 *
 * @param _idx index. Index is postincremented.
 *
 * @param _max maximum index (buffer capacity).
 *
 * @param _arg argument.
 */
#define Z_CBPRINTF_PACK_ARG(_buf, _idx, _max, _arg) \
	do { \
		if (_buf && _idx < _max) { \
			Z_CBPRINTF_PACK(&_buf[_idx], _arg); \
		} \
		__auto_type _v = _arg; \
		_idx += Z_CBPRINTF_ARG_SIZE(_v); \
	} while (0)

/** @brief Package single argument.
 *
 * Macro is called in a loop for each argument in the string.
 *
 * @param arg argument.
 */
#define Z_CBPRINTF_LOOP_PACK_ARG(arg) \
	do { \
		Z_CBPRINTF_PACK_ARG(__package_buf, __package_len, \
				    __package_max, arg); \
	} while (0)

/** @brief Statically package a formatted string with arguments.
 *
 * @param buf buffer. If null then only length is calculated.
 *
 * @param len buffer capacity on input, package size on output.
 *
 * @param ... String with variable list of arguments.
 */
#define Z_CBPRINTF_STATIC_PACKAGE(buf, len, ... /* fmt, ... */) \
	do { \
		uint8_t *__package_buf = buf; \
		size_t __package_max = (buf != NULL) ? len : SIZE_MAX; \
		size_t __package_len = 0; \
		Z_CBPRINTF_PACK_ARG(__package_buf, __package_len, \
				    __package_max, GET_ARG_N(1, __VA_ARGS__));\
		COND_CODE_0(NUM_VA_ARGS_LESS_1(__VA_ARGS__), (), \
			(FOR_EACH(Z_CBPRINTF_LOOP_PACK_ARG, (;), \
				  GET_ARGS_LESS_N(1,__VA_ARGS__));)) \
		len = __package_len; \
	} while (0)

/** @brief Declare and initilize automatic variable.
 *
 * Unique name is achieved using token and index.
 */
#define Z_CBPRINTF_AUTO_VAR(idx, v, token) __auto_type token##idx = v

/** @brief Create local variables of the same type as the one provided in as
 *	   variable argument list.
 *
 * Local variables are used to handle special case when string literal is
 * provided. Sizeof for a string literal returns size of the string and not
 * the size of the pointer. Autotype variable from string literal is of char *
 * type.
 */
#define Z_CBPRINTF_STATIC_PACKAGE_SIZE_TOKEN(token, ...) \
	COND_CODE_0(NUM_VA_ARGS_LESS_1(__VA_ARGS__), (), \
		(FOR_EACH_IDX_FIXED_ARG(Z_CBPRINTF_AUTO_VAR, (;), token, \
					GET_ARGS_LESS_N(1, __VA_ARGS__))))

#define Z_CBPRINTF_TOKEN_ARG_SIZE(idx, v, token) \
	Z_CBPRINTF_ARG_SIZE(token##idx)

/** @brief Calculate package size. 0 is retuned if only null pointer is given.*/
#define Z_CBPRINTF_STATIC_PACKAGE_SIZE(token, ...) \
	COND_CODE_0(NUM_VA_ARGS_LESS_1(__VA_ARGS__), \
		(GET_ARG_N(1, __VA_ARGS__) == NULL ? \
			 		0 : 1 + sizeof(const char *)), \
		(1 + sizeof(const char *) + \
		 FOR_EACH_IDX_FIXED_ARG(Z_CBPRINTF_TOKEN_ARG_SIZE, \
					(+), token, \
					GET_ARGS_LESS_N(1,__VA_ARGS__))))

#ifdef __cplusplus
}
#endif


#endif /* ZEPHYR_INCLUDE_SYS_CBPRINTF_INTERNAL_H_ */

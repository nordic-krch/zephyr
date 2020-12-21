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
 * @retval positive if string must be packaged at runtime.
 * @retval 0 if string can be statically packaged.
 */
#define Z_CBPRINTF_MUST_RUNTIME_PACKAGE(skip, ...) \
	COND_CODE_0(NUM_VA_ARGS_LESS_1(__VA_ARGS__), \
			(0), \
			(Z_CBPRINTF_HAS_PCHAR_ARGS_(__VA_ARGS__) - skip))

/* Union used for storing an argument. */
union z_cbprintf_types {
	double d;
	long double ld;
	uint32_t u[sizeof(double)/sizeof(uint32_t)];
	uint8_t u8[sizeof(double)];
};

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

/** @brief Promote float to double.
 *
 * @param t storage union.
 *
 * @param x float argument.
 */
#define Z_CBPRINTF_FLOAT_PACK(t, x) \
	do { \
		t.d = _Generic((x), float: x, default: 0.0); \
	} while (0)

/** @brief Pack argument into the buffer.
 *
 * Packing includes promoting floats to double.
 *
 * @param _buf destination buffer.
 *
 * @param x argument to pack.
 */
#define Z_CBPRINTF_PACK(_buf, x) do { \
	union z_cbprintf_types _t; \
	__auto_type _x = x; \
	_Generic((x),\
		float: ({Z_CBPRINTF_FLOAT_PACK(_t, x);}),\
		volatile float: ({Z_CBPRINTF_FLOAT_PACK(_t, x);}),\
		const float: ({Z_CBPRINTF_FLOAT_PACK(_t, x);}),\
		const volatile float: ({Z_CBPRINTF_FLOAT_PACK(_t, x);}),\
		default: ({ \
			  *(__typeof__(_x) *)(&_t) = (x); \
			  })\
	);\
	/* If string is packed then add 1 byte header fixed to 0 */ \
	int offset = _Generic(_x, const char *: 1, char *: 1, default: 0); \
	*(uint8_t *)_buf = 0; \
	int *_wbuf = (int *)&(((uint8_t *)_buf)[offset]); \
	for (int i = 0; i < Z_CBPRINTF_ARG_WSIZE(_x); i++) {\
		((int *)(_wbuf))[i] = _t.u[i]; \
	} \
} while (0)

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

#define Z_CBPRINTF_AUTO_VAR(idx, v, token) __auto_type token##idx = v

#define Z_CBPRINTF_STATIC_PACKAGE_SIZE_TOKEN(token, ...) \
	COND_CODE_0(NUM_VA_ARGS_LESS_1(__VA_ARGS__), (), \
		(FOR_EACH_IDX_FIXED_ARG(Z_CBPRINTF_AUTO_VAR, (;), token, \
					GET_ARGS_LESS_N(1, __VA_ARGS__))))

#define Z_CBPRINTF_TOKEN_ARG_SIZE(idx, v, token) \
	Z_CBPRINTF_ARG_SIZE(token##idx)

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

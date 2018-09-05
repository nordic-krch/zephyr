/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LOG_FRONTEND_H
#define LOG_FRONTEND_H

#include <logging/log_msg.h>
#include <logging/log_instance.h>
#include <misc/util.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !CONFIG_LOG
#define CONFIG_LOG_DEFAULT_LEVEL 0
#define CONFIG_LOG_DOMAIN_ID 0
#define CONFIG_LOG_MAX_LEVEL 0
#endif

/** @brief Macro for returning local level value if defined or default.
 *
 * Check @ref IS_ENABLED macro for detailed explanation of the trick.
 */
#define _LOG_RESOLVED_LEVEL(_level, _default) \
	_LOG_RESOLVED_LEVEL1(_level, _default)

#define _LOG_RESOLVED_LEVEL1(_level, _default) \
	__LOG_RESOLVED_LEVEL2(_LOG_XXXX##_level, _level, _default)

#define _LOG_XXXX0 _LOG_YYYY,
#define _LOG_XXXX1 _LOG_YYYY,
#define _LOG_XXXX2 _LOG_YYYY,
#define _LOG_XXXX3 _LOG_YYYY,
#define _LOG_XXXX4 _LOG_YYYY,

#define __LOG_RESOLVED_LEVEL2(one_or_two_args, _level, _default) \
	__LOG_ARG_2(one_or_two_args _level, _default)

#define LOG_DEBRACKET(...) __VA_ARGS__

#define __LOG_ARG_2(ignore_this, val, ...) val
#define __LOG_ARG_2_DEBRACKET(ignore_this, val, ...) LOG_DEBRACKET val

/**
 * @brief Macro for conditional code generation if provided log level allows.
 *
 * Macro behaves similarly to standard #if #else #endif clause. The difference is
 * that it is evaluated when used and not when header file is included.
 *
 * @param _eval_level Evaluated level. If level evaluates to one of existing log
 *		      log level (1-4) then macro evaluates to _iftrue.
 * @param _iftrue     Code that should be inserted when evaluated to true. Note,
 *		      that parameter must be provided in brackets.
 * @param _iffalse    Code that should be inserted when evaluated to false.
 *		      Note, that parameter must be provided in brackets.
 */
#define _LOG_EVAL(_eval_level, _iftrue, _iffalse) \
	_LOG_EVAL1(_eval_level, _iftrue, _iffalse)

#define _LOG_EVAL1(_eval_level, _iftrue, _iffalse) \
	_LOG_EVAL2(_LOG_ZZZZ##_eval_level, _iftrue, _iffalse)

#define _LOG_ZZZZ1 _LOG_YYYY,
#define _LOG_ZZZZ2 _LOG_YYYY,
#define _LOG_ZZZZ3 _LOG_YYYY,
#define _LOG_ZZZZ4 _LOG_YYYY,

#define _LOG_EVAL2(one_or_two_args, _iftrue, _iffalse) \
	__LOG_ARG_2_DEBRACKET(one_or_two_args _iftrue, _iffalse)

/** @brief Macro for getting log level for given module.
 *
 * It is evaluated to LOG_LEVEL if defined. Otherwise CONFIG_LOG_DEFAULT_LEVEL
 * is used.
 */
#define _LOG_LEVEL() _LOG_RESOLVED_LEVEL(LOG_LEVEL, CONFIG_LOG_DEFAULT_LEVEL)

/**
 *  @def LOG_CONST_ID_GET
 *  @brief Macro for getting ID of the element of the section.
 *
 *  @param _addr Address of the element.
 */
#define LOG_CONST_ID_GET(_addr)						       \
	_LOG_EVAL(							       \
	  _LOG_LEVEL(),							       \
	  (log_const_source_id((const struct log_source_const_data *)_addr)),  \
	  (0)								       \
	)

/**
 * @def LOG_CURRENT_MODULE_ID
 * @brief Macro for getting ID of current module.
 */
#define LOG_CURRENT_MODULE_ID()						\
	_LOG_EVAL(							\
	  _LOG_LEVEL(),							\
	  (log_const_source_id(&LOG_ITEM_CONST_DATA(LOG_MODULE_NAME))),	\
	  (0)								\
	)

/**
 * @def LOG_CURRENT_DYNAMIC_DATA_ADDR
 * @brief Macro for getting address of dynamic structure of current module.
 */
#define LOG_CURRENT_DYNAMIC_DATA_ADDR()			\
	_LOG_EVAL(					\
	  _LOG_LEVEL(),					\
	  (&LOG_ITEM_DYNAMIC_DATA(LOG_MODULE_NAME)),	\
	  ((struct log_source_dynamic_data *)0)		\
	)

/** @brief Macro for getting ID of the element of the section.
 *
 *  @param _addr Address of the element.
 */
#define LOG_DYNAMIC_ID_GET(_addr)					     \
	_LOG_EVAL(							     \
	  _LOG_LEVEL(),							     \
	  (log_dynamic_source_id((struct log_source_dynamic_data *)_addr)),  \
	  (0)								     \
	)

#define LOG_INTERNAL_STRDUP(arg) arg
#define LOG_INTERNAL_STR(arg) arg

#define IS_LOG_STRDUP(arg) _IS_LOG_STRDUP1(arg)
#define _IS_LOG_STRDUP1(arg) _IS_LOG_STRDUP2(_XXXX##arg)
#define _XXXXLOG_INTERNAL_STRDUP(x) _YYYY,
#define _IS_LOG_STRDUP2(one_or_two_args) __LOG_ARG_2(one_or_two_args 1, 0)


/******************************************************************************/
/****************** Internal macros for log frontend **************************/
/******************************************************************************/
#define _LOG_INTERNAL_X(N, ...)  UTIL_CAT(_LOG_INTERNAL_, N)(__VA_ARGS__)

#define __LOG_INTERNAL(_metadata, ...)			 \
	_LOG_INTERNAL_X(NUM_VA_ARGS_LESS_1(__VA_ARGS__), \
			_metadata, __VA_ARGS__)

#define _LOG_INTERNAL_0(_metadata, _str) \
	log_0(_str, _metadata)

#define _LOG_1(...) log_1(__VA_ARGS__)
#define _LOG_2(...) log_2(__VA_ARGS__)
#define _LOG_3(...) log_3(__VA_ARGS__)
#define _LOG_4(...) log_4(__VA_ARGS__)
#define _LOG_5(...) log_5(__VA_ARGS__)
#define _LOG_6(...) log_6(__VA_ARGS__)
#define _LOG_7(...) log_7(__VA_ARGS__)
#define _LOG_8(...) log_8(__VA_ARGS__)
#define _LOG_9(...) log_9(__VA_ARGS__)

#define _LOG_INTERNAL_1(_metadata, _str, _arg0) 			     \
	do {								     \
		u32_t mask = IS_LOG_STRDUP(_arg0) ? (1 << 16) : 0;	     \
									     \
		_LOG_1(_str, (u32_t)(_arg0), _metadata | mask);		     \
	} while(0)

#define _LOG_INTERNAL_2(_metadata, _str, _arg0, _arg1)	\
	do {								     \
		u32_t mask = (IS_LOG_STRDUP(_arg0) ? (1 << 16) : 0) |	     \
			     (IS_LOG_STRDUP(_arg1) ? (1 << 17) : 0);	     \
									     \
		_LOG_2(_str, (u32_t)(_arg0), (u32_t)(_arg1),		     \
		       _metadata | mask);				     \
	} while(0)

#define _LOG_INTERNAL_3(_metadata, _str, _arg0, _arg1, _arg2) \
	do {								     \
		u32_t mask = (IS_LOG_STRDUP(_arg0) ? (1 << 16) : 0) |	     \
			     (IS_LOG_STRDUP(_arg1) ? (1 << 17) : 0) |	     \
			     (IS_LOG_STRDUP(_arg2) ? (1 << 18) : 0);	     \
									     \
		_LOG_3(_str, (u32_t)(_arg0), (u32_t)(_arg1), (u32_t)(_arg2), \
		       _metadata | mask);				     \
	} while(0)

#define __LOG_ARG_CAST(_x) (u32_t)(_x),

#define __LOG_ARGUMENTS(...) MACRO_MAP(__LOG_ARG_CAST, __VA_ARGS__)

#define _LOG_INTERNAL_LONG(_metadata, _str, ...)		 \
	do {							 \
		u32_t args[] = {__LOG_ARGUMENTS(__VA_ARGS__)};	 \
		log_n(_str, args, ARRAY_SIZE(args), _metadata); \
	} while (0)

#define _LOG_INTERNAL_4(_metadata, _str, _arg0, _arg1, _arg2, _arg3)	\
	do {								\
		u32_t mask = (IS_LOG_STRDUP(_arg0) ? (1 << 16) : 0) |	\
			     (IS_LOG_STRDUP(_arg1) ? (1 << 17) : 0) |	\
			     (IS_LOG_STRDUP(_arg2) ? (1 << 18) : 0) |	\
			     (IS_LOG_STRDUP(_arg3) ? (1 << 19) : 0);	\
									\
		_LOG_INTERNAL_LONG(_metadata | mask, _str, _arg0, _arg1,\
				   _arg2, _arg3);			\
	} while (0)

#define _LOG_INTERNAL_5(_metadata, _str, _arg0, _arg1, _arg2, _arg3,	\
			_arg4)						\
	do {								\
		u32_t mask = (IS_LOG_STRDUP(_arg0) ? (1 << 16) : 0) |	\
			     (IS_LOG_STRDUP(_arg1) ? (1 << 17) : 0) |	\
			     (IS_LOG_STRDUP(_arg2) ? (1 << 18) : 0) |	\
			     (IS_LOG_STRDUP(_arg3) ? (1 << 19) : 0) |	\
			     (IS_LOG_STRDUP(_arg4) ? (1 << 20) : 0);	\
									\
		_LOG_INTERNAL_LONG(_metadata | mask, _str, _arg0, _arg1,\
				   _arg2, _arg3, _arg4);		\
	} while (0)

#define _LOG_INTERNAL_6(_metadata, _str, _arg0, _arg1, _arg2, _arg3,	\
			_arg4, _arg5)					\
	do {								\
		u32_t mask = (IS_LOG_STRDUP(_arg0) ? (1 << 16) : 0) |	\
			     (IS_LOG_STRDUP(_arg1) ? (1 << 17) : 0) |	\
			     (IS_LOG_STRDUP(_arg2) ? (1 << 18) : 0) |	\
			     (IS_LOG_STRDUP(_arg3) ? (1 << 19) : 0) |	\
			     (IS_LOG_STRDUP(_arg4) ? (1 << 20) : 0) |	\
			     (IS_LOG_STRDUP(_arg5) ? (1 << 21) : 0);	\
									\
		_LOG_INTERNAL_LONG(_metadata | mask, _str, _arg0, _arg1,\
				   _arg2, _arg3, _arg4, _arg5);		\
	} while (0)

#define _LOG_INTERNAL_7(_metadata, _str, _arg0, _arg1, _arg2, _arg3,	\
			_arg4, _arg5, _arg6)				\
	do {								\
		u32_t mask = (IS_LOG_STRDUP(_arg0) ? (1 << 16) : 0) |	\
			     (IS_LOG_STRDUP(_arg1) ? (1 << 17) : 0) |	\
			     (IS_LOG_STRDUP(_arg2) ? (1 << 18) : 0) |	\
			     (IS_LOG_STRDUP(_arg3) ? (1 << 19) : 0) |	\
			     (IS_LOG_STRDUP(_arg4) ? (1 << 20) : 0) |	\
			     (IS_LOG_STRDUP(_arg5) ? (1 << 21) : 0) |	\
			     (IS_LOG_STRDUP(_arg6) ? (1 << 22) : 0);	\
									\
		_LOG_INTERNAL_LONG(_metadata | mask, _str, _arg0, _arg1,\
				   _arg2, _arg3, _arg4, _arg5, _arg6);	\
	} while (0)

#define _LOG_INTERNAL_8(_metadata, _str, _arg0, _arg1, _arg2, _arg3,	\
			_arg4, _arg5, _arg6, _arg7)			\
	do {								\
		u32_t mask = (IS_LOG_STRDUP(_arg0) ? (1 << 16) : 0) |	\
			     (IS_LOG_STRDUP(_arg1) ? (1 << 17) : 0) |	\
			     (IS_LOG_STRDUP(_arg2) ? (1 << 18) : 0) |	\
			     (IS_LOG_STRDUP(_arg3) ? (1 << 19) : 0) |	\
			     (IS_LOG_STRDUP(_arg4) ? (1 << 20) : 0) |	\
			     (IS_LOG_STRDUP(_arg5) ? (1 << 21) : 0) |	\
			     (IS_LOG_STRDUP(_arg6) ? (1 << 22) : 0) |	\
			     (IS_LOG_STRDUP(_arg7) ? (1 << 23) : 0);	\
									\
		_LOG_INTERNAL_LONG(_metadata | mask, _str, _arg0, _arg1,\
				   _arg2, _arg3, _arg4, _arg5, _arg6,	\
				   _arg7);				\
	} while (0)

#define _LOG_INTERNAL_9(_metadata, _str, _arg0, _arg1, _arg2, _arg3,	\
			_arg4, _arg5, _arg6, _arg7, _arg8)		\
	do {								\
		u32_t mask = (IS_LOG_STRDUP(_arg0) ? (1 << 16) : 0) |	\
			     (IS_LOG_STRDUP(_arg1) ? (1 << 17) : 0) |	\
			     (IS_LOG_STRDUP(_arg2) ? (1 << 18) : 0) |	\
			     (IS_LOG_STRDUP(_arg3) ? (1 << 19) : 0) |	\
			     (IS_LOG_STRDUP(_arg4) ? (1 << 20) : 0) |	\
			     (IS_LOG_STRDUP(_arg5) ? (1 << 21) : 0) |	\
			     (IS_LOG_STRDUP(_arg6) ? (1 << 22) : 0) |	\
			     (IS_LOG_STRDUP(_arg7) ? (1 << 23) : 0) |	\
			     (IS_LOG_STRDUP(_arg8) ? (1 << 24) : 0);	\
									\
		_LOG_INTERNAL_LONG(_metadata | mask, _str, _arg0, _arg1,\
				   _arg2, _arg3, _arg4, _arg5, _arg6,	\
				   _arg7, _arg8);			\
	} while (0)

#define _LOG_LEVEL_CHECK(_level, _check_level, _default_level) \
	(_level <= _LOG_RESOLVED_LEVEL(_check_level, _default_level))

#define _LOG_CONST_LEVEL_CHECK(_level)					    \
	(IS_ENABLED(CONFIG_LOG) &&					    \
	(								    \
	_LOG_LEVEL_CHECK(_level, CONFIG_LOG_OVERRIDE_LEVEL, LOG_LEVEL_NONE) \
	||								    \
	(!IS_ENABLED(CONFIG_LOG_OVERRIDE_LEVEL) &&			    \
	_LOG_LEVEL_CHECK(_level, LOG_LEVEL, CONFIG_LOG_DEFAULT_LEVEL) &&    \
	(_level <= CONFIG_LOG_MAX_LEVEL)				    \
	)								    \
	))

#define _LOG_SRC_LEVEL(_id, _level) \
	((_level << 13) | (CONFIG_LOG_DOMAIN_ID << 10) | (_id))

/* Set of macros to detect if log_strdup() function was used in parameters.
 * Macro is preparing a bit mask with bit set for any argument which used
 * log_strdup(). Trick requires that parameters which are explicit strings
 * (e.g. "message") are wrapped into LOG_STR() macro.
 *
 * As the result macro call like:
 * MACRO_MAP_FOR(_LOG_STRDUP_MASK, 1, log_strdup(mystr), LOG_STR("hello"))
 *
 * will result in: | (0 << 0) | (1 << 1) | (0 << 2)
 *
 */
#define _LOG_STRDUP_PREFIX_log_strdup(x) x,1
#define _LOG_STRDUP_PREFIX_LOG_STR(x) x
#define _LOG_IS_STRDUP(arg) _LOG_IS_STRDUP2(_LOG_STRDUP_PREFIX_##arg)

#define _LOG_STRDUP_PREFIX_ (u32_t)

#define _LOG_IS_STRDUP2(one_or_two_args) \
	GET_VA_ARG_1(GET_ARGS_AFTER_1(one_or_two_args, 0))

#define _LOG_STRDUP_MASK(arg, i) | (_LOG_IS_STRDUP(arg) << (i + 16))

#define _LOG_STD_METADATA_STRDUP_MASK_GET(_metadata) (_metadata >> 16)
#define _LOG_STD_METADATA_SOURCE_ID_GET(_metadata) (_metadata)
#define _LOG_STD_METADATA_DOMAIN_ID_GET(_metadata) (_metadata >> 10)
#define _LOG_STD_METADATA_LEVEL_GET(_metadata) (_metadata >> 13)

#define _LOG_STD_METADATA(_id, _level)	\
	_LOG_SRC_LEVEL(_id, _level)

#define _LOG_HEXDUMP_METADATA(_id, _level) _LOG_SRC_LEVEL(_id, _level)

/* Set of macros to resolve defered macros (LOG_STRDUP and LOG_STR). */
#define _LOG_PRINTF_ARG_CHECKER(...) __LOG_PRINTF_ARG_CHECKER(__VA_ARGS__)
#define __LOG_PRINTF_ARG_CHECKER(...) ___LOG_PRINTF_ARG_CHECKER(__VA_ARGS__)
#define ___LOG_PRINTF_ARG_CHECKER(...) ____LOG_PRINTF_ARG_CHECKER(__VA_ARGS__)
#define ____LOG_PRINTF_ARG_CHECKER(...) log_printf_arg_checker(__VA_ARGS__)

/******************************************************************************/
/****************** Macros for standard logging *******************************/
/******************************************************************************/
#define __LOG(_level, _id, _filter, ...)				       \
	do {								       \
		if (_LOG_CONST_LEVEL_CHECK(_level) &&			       \
		    (_level <= LOG_RUNTIME_FILTER(_filter))) {		       \
			u32_t metadata;					       \
			metadata = _LOG_SRC_LEVEL(_id, _level);		       \
									       \
			__LOG_INTERNAL(metadata, __VA_ARGS__);		       \
		} else if (0) {						       \
			/* Arguments checker present but never evaluated.*/    \
			/* Placed here to ensure that __VA_ARGS__ are*/        \
			/* evaluated once when log is enabled.*/	       \
			_LOG_PRINTF_ARG_CHECKER(__VA_ARGS__);		       \
		}							       \
	} while (0)

#define _LOG(_level, ...)			       \
	__LOG(_level,				       \
	      LOG_CURRENT_MODULE_ID(),		       \
	      LOG_CURRENT_DYNAMIC_DATA_ADDR(),	       \
	      __VA_ARGS__)

#define _LOG_INSTANCE(_level, _inst, ...)		 \
	__LOG(_level,					 \
	      IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ? \
	      LOG_DYNAMIC_ID_GET(_inst) :		 \
	      LOG_CONST_ID_GET(_inst),			 \
	      _inst,					 \
	      __VA_ARGS__)


/******************************************************************************/
/****************** Macros for hexdump logging ********************************/
/******************************************************************************/
#define __LOG_HEXDUMP(_level, _id, _filter, _data, _length, _str)      \
	do {							       \
		if (_LOG_CONST_LEVEL_CHECK(_level) &&		       \
		    (_level <= LOG_RUNTIME_FILTER(_filter))) {	       \
			u32_t metadata;				       \
								       \
			metadata = _LOG_HEXDUMP_METADATA(_id, _level); \
			log_hexdump(_str, _data, _length, metadata);   \
		}						       \
	} while (0)

#define _LOG_HEXDUMP(_level, _data, _length, _str)	       \
	__LOG_HEXDUMP(_level,				       \
		      LOG_CURRENT_MODULE_ID(),		       \
		      LOG_CURRENT_DYNAMIC_DATA_ADDR(),	       \
		      _data, _length, _str)

#define _LOG_HEXDUMP_INSTANCE(_level, _inst, _data, _length, _str) \
	__LOG_HEXDUMP(_level,					   \
		      IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ?   \
		      LOG_DYNAMIC_ID_GET(_inst) :		   \
		      LOG_CONST_ID_GET(_inst),			   \
		      _inst,					   \
		      _data,					   \
		      _length,					   \
		      _str)

/******************************************************************************/
/****************** Filtering macros ******************************************/
/******************************************************************************/

/** @brief Number of bits used to encode log level. */
#define LOG_LEVEL_BITS 3

/** @brief Filter slot size. */
#define LOG_FILTER_SLOT_SIZE LOG_LEVEL_BITS

/** @brief Number of slots in one word. */
#define LOG_FILTERS_NUM_OF_SLOTS (32 / LOG_FILTER_SLOT_SIZE)

/** @brief Slot mask. */
#define LOG_FILTER_SLOT_MASK ((1 << LOG_FILTER_SLOT_SIZE) - 1)

/** @brief Bit offset of a slot.
 *
 *  @param _id Slot ID.
 */
#define LOG_FILTER_SLOT_SHIFT(_id) (LOG_FILTER_SLOT_SIZE * (_id))

#define LOG_FILTER_SLOT_GET(_filters, _id) \
	((*(_filters) >> LOG_FILTER_SLOT_SHIFT(_id)) & LOG_FILTER_SLOT_MASK)

#define LOG_FILTER_SLOT_SET(_filters, _id, _filter)		     \
	do {							     \
		*(_filters) &= ~(LOG_FILTER_SLOT_MASK <<	     \
				 LOG_FILTER_SLOT_SHIFT(_id));	     \
		*(_filters) |= ((_filter) & LOG_FILTER_SLOT_MASK) << \
			       LOG_FILTER_SLOT_SHIFT(_id);	     \
	} while (0)

#define LOG_FILTER_AGGR_SLOT_IDX 0

#define LOG_FILTER_AGGR_SLOT_GET(_filters) \
	LOG_FILTER_SLOT_GET(_filters, LOG_FILTER_AGGR_SLOT_IDX)

#define LOG_FILTER_FIRST_BACKEND_SLOT_IDX 1

#if CONFIG_LOG_RUNTIME_FILTERING
#define LOG_RUNTIME_FILTER(_filter) \
	LOG_FILTER_SLOT_GET(&(_filter)->filters, LOG_FILTER_AGGR_SLOT_IDX)
#else
#define LOG_RUNTIME_FILTER(_filter) LOG_LEVEL_DBG
#endif

extern const char * log_strdup_fail_msg;

extern struct log_source_const_data __log_const_start[0];
extern struct log_source_const_data __log_const_end[0];

/** @brief Get name of the log source.
 *
 * @param source_id Source ID.
 * @return Name.
 */
static inline const char *log_name_get(u32_t source_id)
{
	return __log_const_start[source_id].name;
}

/** @brief Get compiled level of the log source.
 *
 * @param source_id Source ID.
 * @return Level.
 */
static inline u8_t log_compiled_level_get(u32_t source_id)
{
	return __log_const_start[source_id].level;
}

/** @brief Get index of the log source based on the address of the constant data
 *         associated with the source.
 *
 * @param data Address of the constant data.
 *
 * @return Source ID.
 */
static inline u32_t log_const_source_id(
				const struct log_source_const_data *data)
{
	return ((void *)data - (void *)__log_const_start)/
			sizeof(struct log_source_const_data);
}

/** @brief Get number of registered sources. */
static inline u32_t log_sources_count(void)
{
	return log_const_source_id(__log_const_end);
}

extern struct log_source_dynamic_data __log_dynamic_start[0];
extern struct log_source_dynamic_data __log_dynamic_end[0];

/** @brief Creates name of variable and section for runtime log data.
 *
 *  @param _name Name.
 */
#define LOG_ITEM_DYNAMIC_DATA(_name) UTIL_CAT(log_dynamic_, _name)

#define LOG_INSTANCE_DYNAMIC_DATA(_module_name, _inst) \
	LOG_ITEM_DYNAMIC_DATA(LOG_INSTANCE_FULL_NAME(_module_name, _inst))

/** @brief Get pointer to the filter set of the log source.
 *
 * @param source_id Source ID.
 *
 * @return Pointer to the filter set.
 */
static inline u32_t *log_dynamic_filters_get(u32_t source_id)
{
	return &__log_dynamic_start[source_id].filters;
}

/** @brief Get index of the log source based on the address of the dynamic data
 *         associated with the source.
 *
 * @param data Address of the dynamic data.
 *
 * @return Source ID.
 */
static inline u32_t log_dynamic_source_id(struct log_source_dynamic_data *data)
{
	return ((void *)data - (void *)__log_dynamic_start)/
			sizeof(struct log_source_dynamic_data);
}

/** @brief Dummy function to trigger log messages arguments type checking. */
static inline __printf_like(1, 2)
void log_printf_arg_checker(const char *fmt, ...)
{

}

/** @brief Standard log with no arguments.
 *
 * @param str           String.
 * @param metadata	Log identification.
 */
void log_0(const char *str, u32_t metadata);

/** @brief Standard log with one argument.
 *
 * @param str		String.
 * @param arg1		First argument.
 * @param metadata	Log identification.
 */
void log_1(const char *str,
	   u32_t arg1,
	   u32_t metadata);

/** @brief Standard log with two arguments.
 *
 * @param str		String.
 * @param arg1		First argument.
 * @param arg2		Second argument.
 * @param metadata	Log identification.
 */
void log_2(const char *str,
	   u32_t arg1,
	   u32_t arg2,
	   u32_t metadata);

/** @brief Standard log with three arguments.
 *
 * @param str		String.
 * @param arg1		First argument.
 * @param arg2		Second argument.
 * @param arg3		Third argument.
 * @param metadata	Log identification.
 */
void log_3(const char *str,
	   u32_t arg1,
	   u32_t arg2,
	   u32_t arg3,
	   u32_t metadata);

/** @brief Standard log with arguments list.
 *
 * @param str		String.
 * @param args		Array with arguments.
 * @param narg		Number of arguments in the array.
 * @param metadata	Log identification.
 */
void log_n(const char *str,
	   u32_t *args,
	   u32_t narg,
	   u32_t metadata);

/** @brief Hexdump log.
 *
 * @param str		String.
 * @param data		Data.
 * @param length	Data length.
 * @param metadata	Log identification.
 */
void log_hexdump(const char *str,
		 const u8_t *data,
		 u32_t length,
		 u32_t metadata);

/** @brief Format and put string into log message.
 *
 * @param fmt	String to format.
 * @param ap	Variable list of arguments.
 *
 * @return Number of bytes processed.
 */
int log_printk(const char *fmt, va_list ap);

/**
 * @brief Writes a generic log message to the log.
 *
 * @note This function is intended to be used when porting other log systems.
 */
void log_generic(u32_t metadata, const char *fmt, va_list ap);

/** @brief Frees buffer allocated for string duplication.
 *
 * @param strdup Duplicated string.
 */
void log_strdup_free(void *strdup);

#ifdef __cplusplus
}
#endif

#endif /* LOG_FRONTEND_H */

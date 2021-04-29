#ifndef INCLUDE_TRACING_TRACING_POST_KERNEL_H
#define INCLUDE_TRACING_TRACING_POST_KERNEL_H

#define TRACE_ENTER(fname, ...) printk("calling: %s\n", #fname)
#define TRACE_EXIT(fname, rv, ...) printk("%s returned: %d\n", #fname, rv);

#define trace_func_int_return(fname, ...) ({ \
	int _rv; \
	TRACE_ENTER(fname, __VA_ARGS__); \
	_rv = trace_##fname(__VA_ARGS__); \
	TRACE_EXIT(fname, _rv, __VA_ARGS__); \
	_rv; \
})

#if 1 /* CONFIG_TRACE_MUTEX */

#undef k_mutex_init
#define k_mutex_init(...) trace_func_int_return(k_mutex_init, __VA_ARGS__)

#undef k_mutex_lock
#define k_mutex_lock(...) trace_func_int_return(k_mutex_lock, __VA_ARGS__)

#undef k_mutex_unlock
#define k_mutex_unlock(...) trace_func_int_return(k_mutex_unlock, __VA_ARGS__)

#endif /* CONFIG_TRACE_MUTEX */

#endif /* INCLUDE_TRACING_TRACING_POST_KERNEL_H */

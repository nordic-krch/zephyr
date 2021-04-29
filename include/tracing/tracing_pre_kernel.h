#ifndef INCLUDE_TRACING_TRACING_PRE_KERNEL_H
#define INCLUDE_TRACING_TRACING_PRE_KERNEL_H

#if 1 /* CONFIG_TRACE_MUTEX */
#define k_mutex_init trace_k_mutex_init
#define k_mutex_lock trace_k_mutex_lock
#define k_mutex_unlock trace_k_mutex_unlock
#endif

#endif /* INCLUDE_TRACING_TRACING_PRE_KERNEL_H */

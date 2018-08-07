/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CLI_H__
#define CLI_H__

#include <zephyr.h>
#include <shell/cli_types.h>
#include <shell/shell_history.h>
#include <shell/shell_fprintf.h>
#include <shell/shell_log_backend.h>
#include <logging/log_backend.h>
#include <logging/log_instance.h>
#include <logging/log.h>
#include <misc/util.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHELL_RX_BUFF_SIZE 16

#ifndef CONFIG_SHELL_CMD_BUFF_SIZE
#define CONFIG_SHELL_CMD_BUFF_SIZE 0
#endif

#ifndef CONFIG_SHELL_PRINTF_BUFF_SIZE
#define CONFIG_SHELL_PRINTF_BUFF_SIZE 0
#endif

#define SHELL_CMD_ROOT_LVL		(0u)

/**
 * @defgroup shell Shell
 * @ingroup subsys
 *
 * @brief Module for providing shell.
 *
 * @{
 */

struct shell_static_entry;

/**
 * @brief Shell dynamic command descriptor.
 *
 * @details Function shall fill the received @ref shell_static_entry structure
 * 	    with requested (idx) dynamic subcommand data. If there is more than
 * 	    one dynamic subcommand available, the function shall ensure that the
 * 	    returned commands: entry->syntax are sorted in alphabetical order.
 * 	    If idx exceeds the available dynamic subcommands, the function must
 * 	    write to entry->syntax NULL value. This will indicate to the CLI
 * 	    module that there are no more dynamic commands to read.
 */
typedef void (*shell_dynamic_get)(size_t idx,
				  struct shell_static_entry * entry);

/**
 * @brief CLI command descriptor.
 */
struct shell_cmd_entry {
	bool is_dynamic;
	union {
		/*!< Pointer to function returning dynamic commands.*/
		shell_dynamic_get dynamic_get;

		/*!< Pointer to array of static commands. */
		const struct shell_static_entry *entry;
	} u;
};

struct shell;

/**
 * @brief CLI command handler prototype.
 */
typedef void (*shell_cmd_handler)(struct shell const * shell,
				  size_t argc, char **argv);

/**
 * @brief CLI static command descriptor.
 */
struct shell_static_entry {
	char const *syntax; /*!< Command syntax strings. */
	char const *help; /*!< Command help string. */
	struct shell_cmd_entry const *subcmd; /*!< Pointer to subcommand. */
	shell_cmd_handler handler; /*!< Command handler. */
};

#define SHELL_CMD_NAME(name) UTIL_CAT(shell_cmd_,name)
/**
 * @brief Macro for defining and adding a root command (level 0).
 *
 * @note Each root command shall have unique syntax.
 *
 * @param[in] syntax  Command syntax (for example: history).
 * @param[in] subcmd  Pointer to a subcommands array.
 * @param[in] help    Pointer to a command help string.
 * @param[in] handler Pointer to a function handler.
 */
#define SHELL_CMD_REGISTER(syntax, subcmd, help, handler)		       \
	static const struct shell_static_entry UTIL_CAT(shell_, syntax) =      \
	SHELL_CMD(syntax, subcmd, help, handler);			       \
	static const struct shell_cmd_entry UTIL_CAT(shell_cmd_, syntax)       \
	__attribute__ ((section("."					       \
			STRINGIFY(UTIL_CAT(shell_root_cmd_, syntax)))))	       \
	__attribute__((used)) = {					       \
		.is_dynamic = false,					       \
		.u.entry = &UTIL_CAT(shell_, syntax)			       \
	};

/**
 * @brief Macro for creating a subcommand set. It must be used outside of any function body.
 *
 * @param[in] name  Name of the subcommand set.
 */
#define SHELL_CREATE_STATIC_SUBCMD_SET(name)				     \
	static const struct shell_static_entry shell_##name[];		     \
	static const struct shell_cmd_entry name = {			     \
		.is_dynamic = false,					     \
		.u.entry = shell_##name					     \
	};								     \
	static const struct shell_static_entry shell_##name[] =

/**
 * @brief Define ending subcommands set.
 *
 */
#define SHELL_SUBCMD_SET_END {NULL}

/**
 * @brief Macro for creating a dynamic entry.
 *
 * @param[in] name  Name of the dynamic entry.
 * @param[in] get Pointer to the function returning dynamic commands array @ref
 * 	      shell_dynamic_get.
 */
#define SHELL_CREATE_DYNAMIC_CMD(name, get) \
	static const struct shell_cmd_entry const name = {	\
		.is_dynamic = true,				\
		.u.dynamic_get = get 				\
	}

/**
 * @brief Initializes a shell command (@ref shell_static_entry).
 *
 * @param[in] _syntax  Command syntax (for example: history).
 * @param[in] _subcmd  Pointer to a subcommands array.
 * @param[in] _help    Pointer to a command help string.
 * @param[in] _handler Pointer to a function handler.
 */
#define SHELL_CMD(_syntax, _subcmd, _help, _handler) {	\
	.syntax = (const char *)STRINGIFY(_syntax),	\
	.subcmd = _subcmd,				\
	.help  = (const char *)_help,			\
	.handler = _handler				\
}

/**
 * @internal @brief Internal shell state in response to data received from the terminal.
 */
enum shell_receive_state {
	SHELL_RECEIVE_DEFAULT,
	SHELL_RECEIVE_ESC,
	SHELL_RECEIVE_ESC_SEQ,
SHELL_RECEIVE_TILDE_EXP
};


/**
 * @internal @brief Internal shell state.
 */
enum shell_state
{
	SHELL_STATE_UNINITIALIZED,
	SHELL_STATE_INITIALIZED,
	SHELL_STATE_ACTIVE,
	SHELL_STATE_PANIC_MODE_ACTIVE, /*!< Panic activated.*/
	SHELL_STATE_PANIC_MODE_INACTIVE /*!< Panic requested, not supported.*/
};

/** @brief Shell transport event. */
enum shell_transport_evt
{
    SHELL_TRANSPORT_EVT_RX_RDY,
    SHELL_TRANSPORT_EVT_TX_RDY
};

typedef void (*shell_transport_handler_t)(enum shell_transport_evt evt,
					  void *context);

struct shell_transport;

/**
 * @brief Unified CLI transport interface.
 */
struct shell_transport_api
{
	/**
	 * @brief Function for initializing the CLI transport interface.
	 *
	 * @param[in] transport   Pointer to the transfer instance.
	 * @param[in] config      Pointer to instance configuration.
	 * @param[in] evt_handler   Event handler.
	 * @param[in] context     Pointer to the context passed to event handler.
	 *
	 * @return Standard error code.
	 */
	int (*init)(const struct shell_transport *transport,
		    void const *config,
		    shell_transport_handler_t evt_handler,
		    void *context);

	/**
	 * @brief Function for uninitializing the CLI transport interface.
	 *
	 * @param[in] transport  Pointer to the transfer instance.
	 *
	 * @return Standard error code.
	 */
	int (*uninit)(const struct shell_transport *transport);

	/**
	 * @brief Function for reconfiguring the transport to work in blocking mode.
	 *
	 * @param transport  Pointer to the transfer instance.
	 * @param blocking   If true, the transport is enabled in blocking mode.
	 *
	 * @return NRF_SUCCESS on successful enabling, error otherwise (also if not supported).
	 */
	int (*enable)(const struct shell_transport *transport, bool blocking);

	/**
	 * @brief Function for writing data to the transport interface.
	 *
	 * @param[in] transport  Pointer to the transfer instance.
	 * @param[in] data       Pointer to the source buffer.
	 * @param[in] length     Source buffer length.
	 * @param[in] cnt        Pointer to the sent bytes counter.
	 *
	 * @return Standard error code.
	 */
	int (*write)(const struct shell_transport *transport,
		     const void *data, size_t length, size_t *cnt);

	/**
	 * @brief Function for reading data from the transport interface.
	 *
	 * @param[in] p_transport  Pointer to the transfer instance.
	 * @param[in] p_data       Pointer to the destination buffer.
	 * @param[in] length       Destination buffer length.
	 * @param[in] cnt          Pointer to the received bytes counter.
	 *
	 * @return Standard error code.
	 */
	int (*read)(const struct shell_transport *transport,
		    void *data, size_t length, size_t *cnt);

};

struct shell_transport
{
	const struct shell_transport_api *api;
	void *ctx;
};

/**
 * @brief CLI history object header.
 */
struct shell_history_item_hdr
{
    struct shell_history_item_hdr *prev; /*!< Pointer to the next object.*/
    struct shell_history_item_hdr * next; /*!< Pointer to the previous object.*/
    u16_t cmd_len; /*!< Command length.*/
};


struct shell_stats
{
    u32_t log_lost_cnt;  /*!< Lost log counter.*/
};

#if CONFIG_SHELL_STATS
#define SHELL_STATS_DEFINE(_name) static struct shell_stats _name##_stats;
#define SHELL_STATS_PTR(_name) &_name##_stats
#else
#define SHELL_STATS_DEFINE(_name)
#define SHELL_STATS_PTR(_name) NULL
#endif /* CONFIG_SHELL_STATS */

/**
 * @internal @brief Flags for internal CLI usage.
 */
struct shell_flags
{
    u32_t insert_mode :1; /*!< Controls insert mode for text introduction.*/
    u32_t show_help   :1; /*!< Shows help if -h or --help parameter present.*/
    u32_t use_colors  :1; /*!< Controls colored syntax.*/
    u32_t echo        :1; /*!< Controls CLI echo.*/
    u32_t processing  :1; /*!< CLI is executing process function.*/
    u32_t tx_rdy      :1;
} ;

_Static_assert(sizeof(struct shell_flags) == sizeof(u32_t), "Must fit in 32b.");

/**
 * @internal @brief Union for internal shell usage.
 */
union shell_internal
{
	u32_t value;
	struct shell_flags flags;
};

enum shell_signal {
	SHELL_SIGNAL_RXRDY,
	SHELL_SIGNAL_TXDONE,
	SHELL_SIGNAL_LOG_MSG,
	SHELL_SIGNAL_KILL,
	SHELL_SIGNALS
};
/**
 * @brief CLI instance context.
 */
struct shell_ctx
{
	enum shell_state state; /*!< Internal module state.*/
	enum shell_receive_state receive_state;/*!< Escape sequence indicator.*/

	/*!< Currently executed command.*/
	const struct shell_static_entry *current_stcmd;

	/*!< VT100 color and cursor position, terminal width.*/
	struct shell_vt100_ctx vt100_ctx;

	u16_t cmd_buff_len;/*!< Command length.*/
	u16_t cmd_buff_pos; /*!< Command buffer cursor position.*/

	u16_t cmd_tmp_buff_len; /*!< Command length in tmp buffer.*/

	/*!< Command input buffer.*/
	char cmd_buff[CONFIG_SHELL_CMD_BUFF_SIZE];

	/*!< Command temporary buffer.*/
	char temp_buff[CONFIG_SHELL_CMD_BUFF_SIZE];

	/*!< Printf buffer size.*/
	char printf_buff[CONFIG_SHELL_PRINTF_BUFF_SIZE];

	volatile union shell_internal internal;   /*!< Internal CLI data.*/

	struct k_poll_signal signals[SHELL_SIGNALS];
	struct k_poll_event events[SHELL_SIGNALS];
};

extern const struct log_backend_api log_backend_shell_api;

/**
 * @brief Shell instance internals.*/
struct shell {
	char const *const name; //!< Terminal name.

	const struct shell_transport *iface; /*!< Transport interface.*/
	struct shell_ctx *ctx; /*!< Internal context.*/

	struct shell_history *history;

	const struct shell_fprintf *fprintf_ctx;

	struct shell_stats *stats;

	const struct shell_log_backend *log_backend;

	LOG_INSTANCE_PTR_DECLARE(log);

	/*!< New line character, only allowed values: \\n and \\r.*/
	char const newline_char;

	void *stack;
	struct k_thread *thread;
};

/**
 * @brief Macro for defining a shell instance.
 *
 * @param[in] name              Instance name.
 * @param[in] cli_prefix        CLI prefix string.
 * @param[in] transport_iface   Pointer to the transport interface.
 * @param[in] newline_ch        New line character - only allowed values are '\\n' or '\\r'.
 * @param[in] log_queue_size    Logger processing queue size.
 */
#define SHELL_DEFINE(_name, cli_prefix, transport_iface,		     \
		  newline_ch, log_queue_size)				     \
	static const struct shell _name;				     \
	static struct shell_ctx UTIL_CAT(_name, _ctx);			     \
	static u8_t _name##_out_buffer[CONFIG_SHELL_PRINTF_BUFF_SIZE];	     \
	SHELL_LOG_BACKEND_DEFINE(_name, _name##_out_buffer,		     \
				 CONFIG_SHELL_PRINTF_BUFF_SIZE);	     \
	SHELL_HISTORY_DEFINE(_name, 128, 8);/*todo*/			     \
	SHELL_FPRINTF_DEFINE(_name## _fprintf, &_name, _name##_out_buffer,   \
			     CONFIG_SHELL_PRINTF_BUFF_SIZE,		     \
			     true, shell_print_stream);			     \
	SHELL_STATS_DEFINE(_name);					     \
	LOG_INSTANCE_REGISTER(shell, _name, LOG_LEVEL_NONE);		     \
	static u32_t _name##_stack[CONFIG_SHELL_STACK_SIZE/sizeof(u32_t)];   \
	static struct k_thread _name##_thread;				     \
	static const struct shell _name = {				     \
		.name = cli_prefix,					     \
		.iface = transport_iface,				     \
		.ctx = &UTIL_CAT(_name, _ctx),				     \
		.log_backend = &_name##_log_backend,			     \
		.history = SHELL_HISTORY_PTR(_name),			     \
		.fprintf_ctx = &_name##_fprintf,			     \
		.stats = SHELL_STATS_PTR(_name),			     \
		.log_backend = SHELL_LOG_BACKEND_PTR(_name),		     \
		LOG_INSTANCE_PTR_INIT(log, shell, _name)		     \
		.newline_char = newline_ch,				     \
		.stack = _name##_stack,					     \
		.thread = &_name##_thread				     \
	}

/**
 * @brief Function for initializing a transport layer and internal CLI state.
 *
 * @param[in] shell            Pointer to CLI instance.
 * @param[in] transport_config Transport configuration during initialization.
 * @param[in] use_colors       Enables colored prints.
 * @param[in] log_backend      If true, the console will be used as logger backend.
 * @param[in] init_log_level   Default severity level for the logger.
 *
 * @return Standard error code.
 */
int shell_init(const struct shell *shell, void const *transport_config,
	       bool use_colors, bool log_backend, u32_t init_log_level);

/**
 * @brief Function for uninitializing a transport layer and internal CLI state.
 *
 * @param shell Pointer to shell instance.
 *
 * @return Standard error code.
 */
int shell_uninit(const struct shell *shell);

/**
 * @brief Function for starting shell processing.
 *
 * @param shell Pointer to the shell instance.
 *
 * @return Standard error code.
 */
int shell_start(const struct shell *shell);

/**
 * @brief Function for stopping shell processing.
 *
 * @param shell Pointer to shell instance.
 *
 * @return Standard error code.
 */
int shell_stop(const struct shell *shell);

/**
 * @brief CLI colors for @ref nrf_cli_fprintf function.
 */
#define SHELL_DEFAULT	SHELL_VT100_COLOR_DEFAULT
#define SHELL_NORMAL 	SHELL_VT100_COLOR_WHITE
#define SHELL_INFO	SHELL_VT100_COLOR_GREEN
#define SHELL_OPTION	SHELL_VT100_COLOR_CYAN
#define SHELL_WARNING	SHELL_VT100_COLOR_YELLOW
#define SHELL_ERROR	SHELL_VT100_COLOR_RED

/**
 * @brief Printf-like function which sends formatted data stream to the shell.
 *	  This function shall not be used outside of the shell command context.
 *
 * @param[in] shell Pointer to the shell instance.
 * @param[in] color Printf color.
 * @param[in] p_fmt Format string.
 * @param[in] ...   List of parameters to print.
 */
void shell_fprintf(const struct shell *shell, enum shell_vt100_color  color,
		   char const *p_fmt, ...);

/**
 * @brief Process function, which should be executed when data is ready in the
 *	  transport interface. To be used if shell thread is disabled.
 *
 * @param[in] shell Pointer to the shell instance.
 */
void shell_process(const struct shell *shell);

/**
 * @brief Option descriptor.
 */
struct shell_getopt_option
{
	char const *optname; /*!< Option long name.*/
	char const *optname_short; /*!< Option short name.*/
	char const *optname_help; /*!< Option help string.*/
};

/**
 * @brief Option structure initializer @ref nrf_cli_getopt_option.
 *
 * @param[in] _optname    Option name long.
 * @param[in] _shortname  Option name short.
 * @param[in] _help       Option help string.
 */
#define NRF_CLI_OPT(_optname, _shortname, _help) {	\
	.optname = _optname,				\
	.optname_short = _shortname,			\
	.optname_help = _help,				\
	}

/**
 * @brief Informs that a command has been called with -h or --help option.
 *
 * @param[in] shell Pointer to the shell instance.
 *
 * @return True if help has been requested.
 */
static inline bool shell_help_requested(const struct shell *shell)
{
	return shell->ctx->internal.flags.show_help;
}

/**
 * @brief Prints the current command help.
 *
 * Function will print a help string with: the currently entered command, its
 * options,and subcommands (if they exist).
 *
 * @param[in] shell      Pointer to the shell instance.
 * @param[in] opt        Pointer to the optional option array.
 * @param[in] opt_len    Option array size.
 */
void shell_help_print(const struct shell *shell,
		      const struct shell_getopt_option *opt, size_t opt_len);

/**
 * @brief Prints help if request and prints error message on wrong argument
 * 	  count.
 *
 * 	  Optionally, printing help on wrong argument count can be enabled.
 *
 * @param[in] shell   Pointer to the shell instance.
 * @param[in] arg_cnt_ok Flag indicating valid number of arguments.
 * @param[in] opt     Pointer to the optional option array.
 * @param[in] opt_len Option array size.
 *
 * @return True if check passed, false otherwise.
 */
bool shell_cmd_precheck(const struct shell * shell,
			bool arg_cnt_nok,
		 	struct shell_getopt_option const *opt,
		 	size_t opt_len);

/**
 * @internal @brief This function shall not be used directly, it is required by
 * 		    the fprintf module.
 *
 * @param[in] p_user_ctx    Pointer to the context for the CLI instance.
 * @param[in] p_data        Pointer to the data buffer.
 * @param[in] data_len      Data buffer size.
 */
void shell_print_stream(void const *user_ctx, char const *data,
			size_t data_len);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* CLI_H__ */

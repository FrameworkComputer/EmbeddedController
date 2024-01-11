/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Debug console for Chrome EC */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#ifndef __CROS_EC_CONSOLE_H
#define __CROS_EC_CONSOLE_H

#include "common.h"
#include "config.h"

#include <stdbool.h>

#ifdef CONFIG_ZEPHYR
#include "zephyr_console_shim.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Define uart_shell_stop(), uart_shell_start(), and
 * uart_shell_rx_bypass(enable) functions to start/stop/bypass the running
 * shell. To avoid having a guard on the build type, non-Zephyr builds will
 * have a stubbed function for these which is safe to call. These functions
 * will stop/start/stop the Zephyr shell from processing, they should be used
 * for briefly taking control of the uart.
 */
#ifdef CONFIG_ZEPHYR
int uart_shell_stop(void);
void uart_shell_start(void);
void uart_shell_rx_bypass(bool enable);

#ifdef TEST_BUILD
/* Gets the pointer to the zephyr shell, since it might not always be
 * the uart backend.
 */
const struct shell *get_ec_shell(void);
#endif
#else
static inline int uart_shell_stop(void)
{
	return 0;
}
static inline void uart_shell_start(void)
{
}
static inline void uart_shell_rx_bypass(bool enable)
{
}
#endif

/*
 * The EC code base has been using %h to print a hex buffer. Encode the
 * parameters to do that in a pointer to a structure that's passed as the
 * printf argument. This is done rather than something like %.123ph because
 * the C standard doesn't allow flags, precision, and field width on %p.
 */
struct hex_buffer_params {
	const void *buffer;
	uint16_t size;
};

#define HEX_BUF(_buffer, _size)                                  \
	(&(const struct hex_buffer_params){ .buffer = (_buffer), \
					    .size = (_size) })

#define PRINTF_TIMESTAMP_NOW NULL

/* Console command; used by DECLARE_CONSOLE_COMMAND macro. */
struct console_command {
	/* Command name.  Case-insensitive. */
	const char *name;
	/* Handler for the command.  argv[0] will be the command name. */
	int (*handler)(int argc, const char **argv);
#ifdef CONFIG_CONSOLE_CMDHELP
	/* Description of args */
	const char *argdesc;
	/* Short help for command */
	const char *help;
#endif
#ifdef CONFIG_CONSOLE_COMMAND_FLAGS
	const uint32_t flags;
#endif
};

/* Flag bits for when CONFIG_CONSOLE_COMMAND_FLAGS is enabled */
#define CMD_FLAG_RESTRICTED 0x00000001

/* The default .flags value can be overridden in board.h */
#ifndef CONFIG_CONSOLE_COMMAND_FLAGS_DEFAULT
#define CONFIG_CONSOLE_COMMAND_FLAGS_DEFAULT 0
#endif

#ifdef CONFIG_RESTRICTED_CONSOLE_COMMANDS
/*
 * This must be implemented somewhere. A true return value means that all
 * CMD_FLAG_RESTRICTED commands are disabled.
 */
int console_is_restricted(void);
#else
static inline int console_is_restricted(void)
{
	return 0;
}
#endif

/* Console channels */
enum console_channel {
#define CONSOLE_CHANNEL(enumeration, string) enumeration,
#include "console_channel.inc"
#undef CONSOLE_CHANNEL

	/* Channel count; not itself a channel */
	CC_CHANNEL_COUNT
};

/* Mask in channel_mask for a particular channel */
#define CC_MASK(channel) (1U << (channel))

/* Mask to use to enable all channels */
#define CC_ALL 0xffffffffU

/**
 * Enable a console channel by name
 *
 * @param name		Console channel name
 */
void console_channel_enable(const char *name);

/**
 * Disable a console channel by name
 *
 * @param name		Console channel name
 */
void console_channel_disable(const char *name);

/**
 * Check if channel is disabled.
 *
 * @param channel	Output channel
 *
 * @return true if channel is disabled, false if not.
 */
#ifdef CONFIG_CONSOLE_CHANNEL
bool console_channel_is_disabled(enum console_channel channel);
#else
static inline bool console_channel_is_disabled(enum console_channel channel)
{
	return false;
}
#endif

#ifdef CONFIG_PIGWEED_LOG_TOKENIZED_LIB
/**
 * Buffer size in bytes large enough to hold the largest possible timestamp.
 */
#define PRINTF_TIMESTAMP_BUF_SIZE 22
int snprintf_timestamp_now(char *str, size_t size);
#if 0
/* TODO(b/289215486)
 * console_channel_is_disabled checks don't give space savings,
 * See if we can add this but still get tokenized space savings
 */
#define cputs(channel, outstr)                               \
	do {                                                 \
		if (!console_channel_is_disabled(channel)) { \
			PW_LOG_INFO("%s", outstr);           \
		}                                            \
	} while (false)

#define cprintf(channel, format, ...)                        \
	do {                                                 \
		if (!console_channel_is_disabled(channel)) { \
			PW_LOG_INFO(format, ##__VA_ARGS__);  \
		}                                            \
	} while (false)

#define cprints(channel, format, ...)                                   \
	do {                                                            \
		if (!console_channel_is_disabled(channel)) {            \
			char ts_str[PRINTF_TIMESTAMP_BUF_SIZE];         \
			snprintf_timestamp_now(ts_str, sizeof(ts_str)); \
			PW_LOG_INFO("[%s " format "]\n", ts_str,        \
				    ##__VA_ARGS__);                     \
		}                                                       \
	} while (false)
#endif

#define cputs(channel, outstr) PW_LOG_INFO(outstr)

#define cprintf(channel, format, ...) PW_LOG_INFO(format, ##__VA_ARGS__)

#define cprints(channel, format, ...)                                    \
	do {                                                             \
		char ts_str[PRINTF_TIMESTAMP_BUF_SIZE];                  \
		snprintf_timestamp_now(ts_str, sizeof(ts_str));          \
		PW_LOG_INFO("[%s " format "]\n", ts_str, ##__VA_ARGS__); \
	} while (false)
#else

/**
 * Put a string to the console channel.
 *
 * @param channel	Output chanel
 * @param outstr	String to write
 *
 * @return non-zero if output was truncated.
 */
int cputs(enum console_channel channel, const char *outstr);

/**
 * Print formatted output to the console channel.
 *
 * @param channel	Output chanel
 * @param format	Format string; see printf.h for valid formatting codes
 *
 * @return non-zero if output was truncated.
 */
__attribute__((__format__(__printf__, 2, 3))) int
cprintf(enum console_channel channel, const char *format, ...);

/**
 * Print formatted output with timestamp. This is like:
 *   cprintf(channel, "[<TIMESTAMP> " + format + "]\n", ...)
 *
 * @param channel	Output channel
 * @param format	Format string; see printf.h for valid formatting codes
 *
 * @return non-zero if output was truncated.
 */
__attribute__((__format__(__printf__, 2, 3))) int
cprints(enum console_channel channel, const char *format, ...);
#endif /* CONFIG_PIGWEED_LOG_TOKENIZED_LIB */

/**
 * Flush the console output for all channels.
 */
void cflush(void);

/* Convenience macros for printing to the command channel.
 *
 * Modules may define similar macros in their .c files for their own use; it is
 * recommended those module-specific macros be named CPUTS and CPRINTF. */
#define ccputs(outstr) cputs(CC_COMMAND, outstr)
/* gcc allows variable arg lists in macros; see
 * http://gcc.gnu.org/onlinedocs/gcc/Variadic-Macros.html */
#define ccprintf(format, args...) cprintf(CC_COMMAND, format, ##args)
#define ccprints(format, args...) cprints(CC_COMMAND, format, ##args)

/**
 * Called by UART when a line of input is pending.
 */
void console_has_input(void);

/**
 * Register a console command handler.
 *
 * @param name          Command name; must not be the beginning of another
 *                      existing command name.  Must be less than 15 characters
 *                      long (excluding null terminator).  Note this is NOT in
 *                      quotes so it can be concatenated to form a struct name.
 * @param routine       Command handling routine, of the form
 *                      int handler(int argc, const char **argv)
 * @param argdesc       String describing arguments to command; NULL if none.
 * @param help          String with one-line description of command, or NULL.
 * @param flags         Per-command flags, if needed.
 */
#if !defined(HAS_TASK_CONSOLE) && !defined(CONFIG_ZEPHYR)
#define DECLARE_CONSOLE_COMMAND(NAME, ROUTINE, ARGDESC, HELP) \
	static int(ROUTINE)(int argc, const char **argv) __attribute__((unused))
#define DECLARE_SAFE_CONSOLE_COMMAND(NAME, ROUTINE, ARGDESC, HELP) \
	static int(ROUTINE)(int argc, const char **argv) __attribute__((unused))
#define DECLARE_CONSOLE_COMMAND_FLAGS(NAME, ROUTINE, ARGDESC, HELP, FLAGS) \
	static int(ROUTINE)(int argc, const char **argv) __attribute__((unused))
#elif defined(HAS_TASK_CONSOLE)

/* We always provde help args, but we may discard them to save space. */
#if defined(CONFIG_CONSOLE_CMDHELP)
#define _HELP_ARGS(A, H) .argdesc = A, .help = H,
#else
#define _HELP_ARGS(A, H)
#endif

/* We may or may not have a .flags field */
#ifdef CONFIG_CONSOLE_COMMAND_FLAGS
#define _FLAG_ARGS(F) .flags = F,
#else
#define _FLAG_ARGS(F)
#endif

/* This macro takes all possible args and discards the ones we don't use */
#define _DCL_CON_CMD_ALL(NAME, ROUTINE, ARGDESC, HELP, FLAGS)       \
	static int(ROUTINE)(int argc, const char **argv);           \
	static const char __con_cmd_label_##NAME[] = #NAME;         \
	_STATIC_ASSERT(sizeof(__con_cmd_label_##NAME) < 16,         \
		       "command name '" #NAME "' is too long");     \
	const struct console_command __keep __no_sanitize_address   \
		__con_cmd_##NAME                                    \
		__attribute__((section(".rodata.cmds." #NAME))) = { \
			.name = __con_cmd_label_##NAME,             \
			.handler = ROUTINE,                         \
			_HELP_ARGS(ARGDESC, HELP) _FLAG_ARGS(FLAGS) \
		}

/*
 * If the .flags field exists, we can use this to specify its value. If not,
 * the value will be discarded so it doesn't matter.
 */
#define DECLARE_CONSOLE_COMMAND_FLAGS(NAME, ROUTINE, ARGDESC, HELP, FLAGS) \
	_DCL_CON_CMD_ALL(NAME, ROUTINE, ARGDESC, HELP, FLAGS)

/* This works as before, for the same reason. */
#define DECLARE_CONSOLE_COMMAND(NAME, ROUTINE, ARGDESC, HELP) \
	_DCL_CON_CMD_ALL(NAME, ROUTINE, ARGDESC, HELP,        \
			 CONFIG_CONSOLE_COMMAND_FLAGS_DEFAULT)

/*
 * This can be used to ensure that whatever default flag bits are set (if any),
 * the command is never restricted. BE CAREFUL! You should only use this for
 * commands that either do nothing or that do only safe things.
 */
#define DECLARE_SAFE_CONSOLE_COMMAND(NAME, ROUTINE, ARGDESC, HELP) \
	_DCL_CON_CMD_ALL(NAME, ROUTINE, ARGDESC, HELP,             \
			 (CONFIG_CONSOLE_COMMAND_FLAGS_DEFAULT &   \
			  ~CMD_FLAG_RESTRICTED))

#endif /* HAS_TASK_CONSOLE */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_CONSOLE_H */

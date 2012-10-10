/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Debug console for Chrome EC */

#ifndef __CROS_EC_CONSOLE_H
#define __CROS_EC_CONSOLE_H

#include "board.h"
#include "common.h"

/* Console command; used by DECLARE_CONSOLE_COMMAND macro. */
struct console_command {
	/* Command name.  Case-insensitive. */
	const char *name;
	/* Handler for the command.  argv[0] will be the command name. */
	int (*handler)(int argc, char **argv);
#ifdef CONFIG_CONSOLE_CMDHELP
	/* Description of args */
	const char *argdesc;
	/* Short help for command */
	const char *shorthelp;
#endif
};


/* Console channels */
enum console_channel {
	CC_COMMAND = 0,  /* Console command (interactive I/O).  Use this only
			  * inside a console command routine. */
	CC_CHARGER,
	CC_CHIPSET,
	CC_DMA,
	CC_EVENTS,
	CC_GPIO,
	CC_HOSTCMD,
	CC_I2C,
	CC_I8042,
	CC_KEYBOARD,
	CC_KEYSCAN,
	CC_LIGHTBAR,
	CC_LPC,
	CC_PORT80,
	CC_POWERBTN,
	CC_PWM,
	CC_SPI,
	CC_SYSTEM,
	CC_TASK,
	CC_THERMAL,
	CC_USBCHARGE,
	CC_VBOOT,
	/* Channel count; not itself a channel */
	CC_CHANNEL_COUNT
};

/* Mask in channel_mask for a particular channel */
#define CC_MASK(channel)	(1UL << (channel))

/* Mask to use to enable all channels */
#define CC_ALL			0xffffffffUL


/* Put a string to the console channel. */
int cputs(enum console_channel channel, const char *outstr);

/* Print formatted output to the console channel.
 *
 * See printf.h for valid formatting codes. */
int cprintf(enum console_channel channel, const char *format, ...);

/* Flush the console output for all channels. */
void cflush(void);

/* Convenience macros for printing to the command channel.
 *
 * Modules may define similar macros in their .c files for their own use; it is
 * recommended those module-specific macros be named CPUTS and CPRINTF. */
#define ccputs(outstr) cputs(CC_COMMAND, outstr)
/* gcc allows variable arg lists in macros; see
 * http://gcc.gnu.org/onlinedocs/gcc/Variadic-Macros.html */
#define ccprintf(format, args...) cprintf(CC_COMMAND, format, ## args)


/* Called by UART when a line of input is pending. */
void console_has_input(void);


/*
 * Register a console command handler. Note that `name' must never be a
 * beginning of another existing command name.
 */
#ifdef CONFIG_CONSOLE_CMDHELP
#define DECLARE_CONSOLE_COMMAND(name, routine, argdesc, shorthelp, longhelp) \
	static const char __con_cmd_label_##name[] = #name;		\
	const struct console_command __con_cmd_##name			\
	__attribute__((section(".rodata.cmds." #name)))			\
	     = {__con_cmd_label_##name, routine, argdesc, shorthelp}
#else
#define DECLARE_CONSOLE_COMMAND(name, routine, argdesc, shorthelp, longhelp) \
	static const char __con_cmd_label_##name[] = #name;		\
	const struct console_command __con_cmd_##name			\
	__attribute__((section(".rodata.cmds." #name)))			\
	     = {__con_cmd_label_##name, routine}
#endif

#endif  /* __CROS_EC_CONSOLE_H */

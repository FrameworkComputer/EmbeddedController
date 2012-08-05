/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Panic handling, including displaying a message on the panic reporting
 * device, which is currently the UART.
 */

#ifndef __PANIC_H
#include <stdarg.h>

/**
 * Write a character to the panic reporting device
 *
 * This function will not return until the character has left the UART
 * data register. Any previously queued UART traffic is displayed first.
 *
 * @param ch	Character to write
 */
void panic_putc(int ch);

/**
 * Write a string to the panic reporting device
 *
 * This function will not return until the string has left the UART
 * data register. Any previously queued UART traffic is displayed first.
 *
 * @param ch	Character to write
 */
void panic_puts(const char *s);

/**
 * Very basic vprintf() for use in panic situations
 *
 * We only support %s and %nx where n is the number of hex digits to display.
 * Currently we don't even support %d, and it is aimed at small code size.
 *
 * TODO(sjg@chromium.org): Really what we need is a vsnprintf() that is
 * shared between the console UART and panic (and is also available as an
 * snprintf()). The only downside is that we would then require a large
 * printf() implementation to be always present, whereas presumably now we
 * can turn it off.
 *
 * @param format	printf-style format string
 * @param args		List of arguments to process
 */
void panic_vprintf(const char *format, va_list args);

/**
 * Very basic printf() for use in panic situations
 *
 * See panic_vprintf() for full details
 *
 * @param format	printf-style format string
 * @param ...		Arguments to process
 */
void panic_printf(const char *format, ...);


/**
 * Report an assertion failure and reset
 *
 * @param msg		Assertion expression or other message
 * @param func		Function name where assertion happened
 * @param fname		File name where assertion happened
 * @param linenum	Line number where assertion happened
 */
void panic_assert_fail(const char *msg, const char *func, const char *fname,
		       int linenum);

/**
 * Display a panic message and reset
 *
 * @param msg	Panic message
 */
void panic(const char *msg);

/**
 * Report a panic to the panic reporting device
 *
 * This is exported only to permit use from assembler.
 *
 * @param msg		Panic message
 * @param lregs		Registers from the exception: psp, ipsr, lr, r4-r11
 */
void report_panic(const char *msg, uint32_t *lregs);

/**
 * Enable/disable bus fault handler
 *
 * @param ignored	Non-zero if ignoring bus fault
 */
void ignore_bus_fault(int ignored);

#endif

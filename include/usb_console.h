/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB serial console module */

#ifndef __USB_CONSOLE_H
#define __USB_CONSOLE_H

#ifdef CONFIG_USB_CONSOLE

#include <stdarg.h>

/**
 * Put a null-terminated string to the USB console, like fputs().
 *
 * @return EC_SUCCESS, or non-zero if output was truncated.
 */
int usb_puts(const char *outstr);

/**
 * Print formatted output to the USB console, like vprintf().
 *
 * See printf.h for valid formatting codes.
 *
 * @return EC_SUCCESS, or non-zero if output was truncated.
 */
int usb_vprintf(const char *format, va_list args);

/**
 * Put a single character to the USB console, like putchar().
 *
 * @param c		Character to put
 * @return EC_SUCCESS, or non-zero if output was truncated.
 */
int usb_putc(int c);

/**
 * Read a single character of input, similar to fgetc().
 *
 * @return the character, or -1 if no input waiting.
 */
int usb_getc(void);

/**
 * Enable and Disable the USB console.
 *
 * By default the console is enabled, this should not be a problem since it
 * is not accessible until the USB peripheral is also initialized, which can
 * be delayed.
 */
void usb_console_enable(int enabled);

#define usb_va_start va_start
#define usb_va_end va_end
#else
#define usb_puts(x) EC_SUCCESS
#define usb_vprintf(x, y) EC_SUCCESS
#define usb_putc(x) EC_SUCCESS
#define usb_getc(x) (-1)
#define usb_va_start(x, y)
#define usb_va_end(x)
#endif

#endif /* __USB_CONSOLE_H */

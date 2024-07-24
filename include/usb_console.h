/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB serial console module */

#ifndef __CROS_EC_USB_CONSOLE_H
#define __CROS_EC_USB_CONSOLE_H

#if defined(CONFIG_USB_CONSOLE) || defined(CONFIG_USB_CONSOLE_STREAM)

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

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
 * Reset the usb console output crc32 accumulator.
 */
void usb_console_crc_init(void);

/**
 * Get the current usb console output crc32 accumulator.
 */
uint32_t usb_console_crc(void);

/**
 * Enable and Disable the USB console.
 *
 * By default the console is enabled, this should not be a problem since it
 * is not accessible until the USB peripheral is also initialized, which can
 * be delayed.
 */
void usb_console_enable(int enabled, int readonly);

/**
 * Is USB TX queue blocked?
 *
 * Return 1, if USB console is enabled and USB TX Queue does not have enough
 *           space for the next packet.
 *        0, otherwise.
 */
int usb_console_tx_blocked(void);

#else
#define usb_puts(x) EC_SUCCESS
#define usb_vprintf(x, y) EC_SUCCESS
#define usb_putc(x) EC_SUCCESS
#define usb_getc(x) (-1)
#define usb_console_tx_blocked() (0)

#ifdef __cplusplus
}
#endif

#endif

#endif /* __CROS_EC_USB_CONSOLE_H */

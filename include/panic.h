/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Panic handling, including displaying a message on the panic reporting
 * device, which is currently the UART.
 */

#ifndef __CROS_EC_PANIC_H
#define __CROS_EC_PANIC_H

#include <stdarg.h>

/* Data saved across reboots */
struct panic_data {
	uint8_t arch;             /* Architecture (PANIC_ARCH_*) */
	uint8_t struct_version;   /* Structure version (currently 1) */
	uint8_t flags;            /* Flags (PANIC_DATA_FLAG_*) */
	uint8_t reserved;         /* Reserved; set 0 */

	uint32_t regs[11];        /* psp, ipsr, lr, r4-r11 */
	uint32_t frame[8];        /* r0-r3, r12, lr, pc, xPSR */

	uint32_t mmfs;
	uint32_t bfar;
	uint32_t mfar;
	uint32_t shcsr;
	uint32_t hfsr;
	uint32_t dfsr;

	/*
	 * These fields go at the END of the struct so we can find it at the
	 * end of memory.
	 */
	uint32_t struct_size;     /* Size of this struct */
	uint32_t magic;           /* PANIC_SAVE_MAGIC if valid */
};

#define PANIC_DATA_MAGIC 0x21636e50  /* "Pnc!" */
#define PANIC_ARCH_CORTEX_M 1        /* Cortex-M architecture */

/* Flags for panic_data.flags */
/* panic_data.frame is valid */
#define PANIC_DATA_FLAG_FRAME_VALID (1 << 0)
/* Already printed at console */
#define PANIC_DATA_FLAG_OLD_CONSOLE (1 << 1)
/* Already returned via host command */
#define PANIC_DATA_FLAG_OLD_HOSTCMD (1 << 2)

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
 * Enable/disable bus fault handler
 *
 * @param ignored	Non-zero if ignoring bus fault
 */
void ignore_bus_fault(int ignored);

/**
 * Return a pointer to the saved data from a previous panic.
 *
 * @param pointer to the panic data, or NULL if none available (for example,
 * the last reboot was not caused by a panic).
 */
struct panic_data *panic_get_data(void);

#endif  /* __CROS_EC_PANIC_H */

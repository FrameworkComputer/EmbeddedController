/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Panic handling, including displaying a message on the panic reporting
 * device, which is currently the UART.
 */

#ifndef __CROS_EC_PANIC_H
#define __CROS_EC_PANIC_H

#include "software_panic.h"

#include <stdarg.h>

/* ARM Cortex-Mx registers saved on panic */
struct cortex_panic_data {
	uint32_t regs[12];        /* psp, ipsr, msp, r4-r11, lr(=exc_return).
				   * In version 1, that was uint32_t regs[11] =
				   * psp, ipsr, lr, r4-r11
				   */
	uint32_t frame[8];        /* r0-r3, r12, lr, pc, xPSR */

	uint32_t mmfs;
	uint32_t bfar;
	uint32_t mfar;
	uint32_t shcsr;
	uint32_t hfsr;
	uint32_t dfsr;
};

/* Data saved across reboots */
struct panic_data {
	uint8_t arch;             /* Architecture (PANIC_ARCH_*) */
	uint8_t struct_version;   /* Structure version (currently 2) */
	uint8_t flags;            /* Flags (PANIC_DATA_FLAG_*) */
	uint8_t reserved;         /* Reserved; set 0 */

	/* core specific panic data */
	union {
		struct cortex_panic_data cm; /* Cortex-Mx registers */
	};

	/*
	 * These fields go at the END of the struct so we can find it at the
	 * end of memory.
	 */
	uint32_t struct_size;     /* Size of this struct */
	uint32_t magic;           /* PANIC_SAVE_MAGIC if valid */
};

#define PANIC_DATA_MAGIC 0x21636e50  /* "Pnc!" */
#define PANIC_ARCH_CORTEX_M 1        /* Cortex-M architecture */

/*
 * Panic data goes at the end of RAM.  This is safe because we don't context
 * switch away from the panic handler before rebooting, and stacks and data
 * start at the beginning of RAM.
 */
#define PANIC_DATA_PTR ((struct panic_data *)\
	(CONFIG_RAM_BASE + CONFIG_RAM_SIZE - sizeof(struct panic_data)))

/* Flags for panic_data.flags */
/* panic_data.frame is valid */
#define PANIC_DATA_FLAG_FRAME_VALID    (1 << 0)
/* Already printed at console */
#define PANIC_DATA_FLAG_OLD_CONSOLE    (1 << 1)
/* Already returned via host command */
#define PANIC_DATA_FLAG_OLD_HOSTCMD    (1 << 2)
/* Already reported via host event */
#define PANIC_DATA_FLAG_OLD_HOSTEVENT  (1 << 3)

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
 * Very basic printf() for use in panic situations
 *
 * See panic_vprintf() for full details
 *
 * @param format	printf-style format string
 * @param ...		Arguments to process
 */
void panic_printf(const char *format, ...);

/*
 * Print saved panic information
 *
 * @param pdata pointer to saved panic data
 */
void panic_data_print(const struct panic_data *pdata);

/**
 * Report an assertion failure and reset
 *
 * @param msg		Assertion expression or other message
 * @param func		Function name where assertion happened
 * @param fname		File name where assertion happened
 * @param linenum	Line number where assertion happened
 */
#ifdef CONFIG_DEBUG_ASSERT_BRIEF
void panic_assert_fail(const char *fname, int linenum);
#else
void panic_assert_fail(const char *msg, const char *func, const char *fname,
		       int linenum);
#endif

/**
 * Display a custom panic message and reset
 *
 * @param msg	Panic message
 */
void panic(const char *msg);

/**
 * Display a default message and reset
 */
void panic_reboot(void);

#ifdef CONFIG_SOFTWARE_PANIC
/**
 * Store a panic log and halt the system for a software-related reason, such as
 * stack overflow or assertion failure.
 */
void software_panic(uint32_t panic_reason, uint32_t panic_info);

/**
 * Log a watchdog panic in the panic log. Called on the subsequent reboot after
 * the watchdog fires.
 */
void panic_log_watchdog(void);
#endif

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

/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Panic handling, including displaying a message on the panic reporting
 * device, which is currently the UART.
 */

#ifndef __CROS_EC_PANIC_H
#define __CROS_EC_PANIC_H

#include "common.h"
#include "panic_defs.h"
#include "software_panic.h"

#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_RO_PANIC_DATA_SIZE
BUILD_ASSERT(sizeof(struct panic_data) == CONFIG_RO_PANIC_DATA_SIZE);
#endif

/* Use PANIC_DATA_PTR to refer to the persistent storage location */
#define PANIC_DATA_PTR ((struct panic_data *)CONFIG_PANIC_DATA_BASE)

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
__attribute__((__format__(__printf__, 1, 2))) void
panic_printf(const char *format, ...);

/*
 * Print saved panic information
 *
 * @param pdata pointer to saved panic data
 */
void panic_data_print(const struct panic_data *pdata);

/*
 * Print saved panic information on console channel to observe panic
 * information
 *
 * @param pdata pointer to saved panic data
 */
void panic_data_ccprint(const struct panic_data *pdata);

/**
 * Report an assertion failure and reset
 *
 * @param msg		Assertion expression or other message
 * @param func		Function name where assertion happened
 * @param fname		File name where assertion happened
 * @param linenum	Line number where assertion happened
 */
#ifdef CONFIG_DEBUG_ASSERT_BRIEF
#if !(defined(TEST_FUZZ) || defined(CONFIG_ZTEST))
__noreturn
#endif
	void
	panic_assert_fail(const char *fname, int linenum);
#else
#if !(defined(TEST_FUZZ) || defined(CONFIG_ZTEST))
__noreturn
#endif
	void
	panic_assert_fail(const char *msg, const char *func, const char *fname,
			  int linenum);
#endif

/**
 * Display a custom panic message and reset
 *
 * @param msg	Panic message
 */
#if !(defined(TEST_FUZZ) || defined(CONFIG_ZTEST))
__noreturn
#endif
	void
	panic(const char *msg);

/**
 * Display a default message and reset
 */
#if !(defined(TEST_FUZZ) || defined(CONFIG_ZTEST))
__noreturn
#endif
	void
	panic_reboot(void);

/**
 * Store a panic log and halt the system for a software-related reason, such as
 * stack overflow or assertion failure.
 */
#if !(defined(TEST_FUZZ) || defined(CONFIG_ZTEST))
__noreturn
#endif
	void
	software_panic(uint32_t reason, uint32_t info);

/**
 * Log a panic in the panic log, but don't halt the system. Normally
 * called on the subsequent reboot after panic detection.
 */
void panic_set_reason(uint32_t reason, uint32_t info, uint8_t exception);

/**
 * Retrieve the currently stored panic reason + info.
 */
void panic_get_reason(uint32_t *reason, uint32_t *info, uint8_t *exception);

#ifdef CONFIG_ZEPHYR
/**
 * Zephyr utility for architecture specific logic to run when setting panic
 * reason.
 */
__override_proto void arch_panic_set_reason(uint32_t reason, uint32_t info,
					    uint8_t exception);
#endif /* CONFIG_ZEPHYR */

/**
 * Enable/disable bus fault handler
 *
 * @param ignored	Non-zero if ignoring bus fault
 */
void ignore_bus_fault(int ignored);

/**
 * Return a pointer to the saved data from a previous panic that can be
 * safely interpreted
 *
 * @param pointer to the valid panic data, or NULL if none available (for
 * example, the last reboot was not caused by a panic).
 */
struct panic_data *panic_get_data(void);

/**
 * Return a pointer to the beginning of panic data. This function can be
 * used to obtain pointer which can be used to calculate place of other
 * structures (eg. jump_data). This function should not be used to get access
 * to panic_data structure as it might not be valid
 *
 * @param pointer to the beginning of panic_data, or NULL if there is no
 * panic_data
 */
uintptr_t get_panic_data_start(void);

#ifdef CONFIG_BOARD_NATIVE_POSIX
/**
 * @brief Test-only function for accessing the pdata_ptr object.
 *
 * @return struct panic_data* pdata_ptr
 */
struct panic_data *test_get_panic_data_pointer(void);
#endif

/**
 * Return a pointer to panic_data structure that can be safely written.  Please
 * note that this function can move jump data and jump tags.  It can also delete
 * panic data from previous boot, so this function should be used when we are
 * sure that we don't need it.
 *
 * NOTE: Invoking this function without subsequently setting the rest of the
 * panic data is unsafe because it leaves the panic data in an unfinished state
 * that may be inappropriately reported to the AP.
 * TODO(b/274661193): Finalize panic data with panic magic.
 *
 * @param pointer to panic_data structure that can be safely written
 */
struct panic_data *get_panic_data_write(void);

/**
 * Return a pointer to the stack of the process that caused the panic.
 * The implementation of this function will depend on the architecture.
 */
uint32_t get_panic_stack_pointer(const struct panic_data *pdata);

/**
 * Chip-specific implementation for backing up panic data to persistent
 * storage. This function is used to ensure that the panic data can survive loss
 * of VCC power rail.
 *
 * There is no generic restore function provided since every chip can decide
 * when it is safe to restore panic data during the system initialization step.
 */
void chip_panic_data_backup(void);

#ifdef __cplusplus
}
#endif

#ifdef TEST_BUILD
/**
 * @brief Wrapper for accessing the command_crash() console command
 * implementation directly in unit tests. It cannot be called normally through
 * the shell interface because it upsets the shell's internal state when the
 * command doesn't return after a crash. command_crash() cannot be marked
 * test_export_static directly due to an implementation detail in
 * DECLARE_CONSOLE_COMMAND().
 *
 * @param argc Number of CLI args in `argv`
 * @param argv CLI arguments
 * @return int Return value
 */
int test_command_crash(int argc, const char **argv);
#endif /* TEST_BUILD*/

#endif /* __CROS_EC_PANIC_H */

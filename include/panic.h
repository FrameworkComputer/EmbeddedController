/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Panic handling, including displaying a message on the panic reporting
 * device, which is currently the UART.
 */

#ifndef __CROS_EC_PANIC_H
#define __CROS_EC_PANIC_H

#include <stdarg.h>
#include <stdint.h>
#include <stdnoreturn.h>

#include "software_panic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ARM Cortex-Mx registers saved on panic */
struct cortex_panic_data {
	uint32_t regs[12];        /* psp, ipsr, msp, r4-r11, lr(=exc_return).
				   * In version 1, that was uint32_t regs[11] =
				   * psp, ipsr, lr, r4-r11
				   */
	uint32_t frame[8];        /* r0-r3, r12, lr, pc, xPSR */

	uint32_t cfsr;
	uint32_t bfar;
	uint32_t mfar;
	uint32_t shcsr;
	uint32_t hfsr;
	uint32_t dfsr;
};

/* NDS32 N8 registers saved on panic */
struct nds32_n8_panic_data {
	uint32_t itype;
	uint32_t regs[16];        /* r0-r10, r15, fp, gp, lp, sp */
	uint32_t ipc;
	uint32_t ipsw;
};

/* RISC-V RV32I registers saved on panic */
struct rv32i_panic_data {
	uint32_t regs[31];        /* sp, ra, gp, tp, a0-a7, t0-t6 s0-s11 */
	uint32_t mepc;            /* mepc */
	uint32_t mcause;          /* mcause */
};

/* x86 registers saved on panic */
struct x86_panic_data {
	uint32_t vector;          /* Exception vector number */

	/* Data pushed when exception handler called */
	uint32_t error_code;
	uint32_t eip;
	uint32_t cs;
	uint32_t eflags;

	/* General purpose registers */
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t esi;
	uint32_t edi;

	/* Task id at time of panic */
	uint8_t task_id;
};

/* Data saved across reboots */
struct panic_data {
	uint8_t arch;             /* Architecture (PANIC_ARCH_*) */
	uint8_t struct_version;   /* Structure version (currently 2) */
	uint8_t flags;            /* Flags (PANIC_DATA_FLAG_*) */
	uint8_t reserved;         /* Reserved; set 0 */

	/* core specific panic data */
	union {
		struct cortex_panic_data cm;       /* Cortex-Mx registers */
		struct nds32_n8_panic_data nds_n8; /* NDS32 N8 registers */
		struct x86_panic_data x86;         /* Intel x86 */
#ifndef CONFIG_DO_NOT_INCLUDE_RV32I_PANIC_DATA
		struct rv32i_panic_data riscv;     /* RISC-V RV32I */
#endif
	};

	/*
	 * These fields go at the END of the struct so we can find it at the
	 * end of memory.
	 */
	uint32_t struct_size;     /* Size of this struct */
	uint32_t magic;           /* PANIC_SAVE_MAGIC if valid */
};

#define PANIC_DATA_MAGIC 0x21636e50  /* "Pnc!" */
enum panic_arch {
	PANIC_ARCH_CORTEX_M = 1,     /* Cortex-M architecture */
	PANIC_ARCH_NDS32_N8 = 2,     /* NDS32 N8 architecture */
	PANIC_ARCH_X86 = 3,          /* Intel x86 */
#ifndef CONFIG_DO_NOT_INCLUDE_RV32I_PANIC_DATA
	PANIC_ARCH_RISCV_RV32I = 4,  /* RISC-V RV32I */
#endif
};

/* Use PANIC_DATA_PTR to refer to the persistent storage location */
#define PANIC_DATA_PTR ((struct panic_data *)CONFIG_PANIC_DATA_BASE)

/* Flags for panic_data.flags */
/* panic_data.frame is valid */
#define PANIC_DATA_FLAG_FRAME_VALID    BIT(0)
/* Already printed at console */
#define PANIC_DATA_FLAG_OLD_CONSOLE    BIT(1)
/* Already returned via host command */
#define PANIC_DATA_FLAG_OLD_HOSTCMD    BIT(2)
/* Already reported via host event */
#define PANIC_DATA_FLAG_OLD_HOSTEVENT  BIT(3)

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
__attribute__((__format__(__printf__, 1, 2)))
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
noreturn void panic_assert_fail(const char *fname, int linenum);
#else
noreturn void panic_assert_fail(const char *msg, const char *func,
				const char *fname, int linenum);
#endif

/**
 * Display a custom panic message and reset
 *
 * @param msg	Panic message
 */
noreturn void panic(const char *msg);

/**
 * Display a default message and reset
 */
noreturn void panic_reboot(void);

#ifdef CONFIG_SOFTWARE_PANIC
/**
 * Store a panic log and halt the system for a software-related reason, such as
 * stack overflow or assertion failure.
 */
noreturn void software_panic(uint32_t reason, uint32_t info);

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
#endif /* CONFIG_SOFTWARE_PANIC */

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
 * @param pointer to the valid panic data, or NULL if none available (for example,
 * the last reboot was not caused by a panic).
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

/*
 * Return a pointer to panic_data structure that can be safely written.
 * Please note that this function can move jump data and jump tags.
 * It can also delete panic data from previous boot, so this function
 * should be used when we are sure that we don't need it.
 *
 * @param pointer to panic_data structure that can be safely written
 */
struct panic_data *get_panic_data_write(void);

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

#endif  /* __CROS_EC_PANIC_H */

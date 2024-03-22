/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Panic handling macros and structures.
 */

#ifndef __CROS_EC_PANIC_DEFS_H
#define __CROS_EC_PANIC_DEFS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum cortex_panic_frame_registers {
	CORTEX_PANIC_FRAME_REGISTER_R0 = 0,
	CORTEX_PANIC_FRAME_REGISTER_R1,
	CORTEX_PANIC_FRAME_REGISTER_R2,
	CORTEX_PANIC_FRAME_REGISTER_R3,
	CORTEX_PANIC_FRAME_REGISTER_R12,
	CORTEX_PANIC_FRAME_REGISTER_LR,
	CORTEX_PANIC_FRAME_REGISTER_PC,
	CORTEX_PANIC_FRAME_REGISTER_PSR,
	NUM_CORTEX_PANIC_FRAME_REGISTERS
};

enum cortex_panic_registers_v1 {
	CORTEX_PANIC_REGISTER_PSP_V1 = 0,
	CORTEX_PANIC_REGISTER_IPSR_V1,
	CORTEX_PANIC_REGISTER_LR_V1,
	CORTEX_PANIC_REGISTER_R4_V1,
	CORTEX_PANIC_REGISTER_R5_V1,
	CORTEX_PANIC_REGISTER_R6_V1,
	CORTEX_PANIC_REGISTER_R7_V1,
	CORTEX_PANIC_REGISTER_R8_V1,
	CORTEX_PANIC_REGISTER_R9_V1,
	CORTEX_PANIC_REGISTER_R10_V1,
	CORTEX_PANIC_REGISTER_R11_V1,
	NUM_CORTEX_PANIC_REGISTERS_V1
};

enum cortex_panic_registers {
	CORTEX_PANIC_REGISTER_PSP = 0,
	CORTEX_PANIC_REGISTER_IPSR,
	CORTEX_PANIC_REGISTER_MSP,
	CORTEX_PANIC_REGISTER_R4,
	CORTEX_PANIC_REGISTER_R5,
	CORTEX_PANIC_REGISTER_R6,
	CORTEX_PANIC_REGISTER_R7,
	CORTEX_PANIC_REGISTER_R8,
	CORTEX_PANIC_REGISTER_R9,
	CORTEX_PANIC_REGISTER_R10,
	CORTEX_PANIC_REGISTER_R11,
	CORTEX_PANIC_REGISTER_LR,
	NUM_CORTEX_PANIC_REGISTERS
};

/* Version 1 ARM Cortex-Mx registers saved on panic */
struct cortex_panic_data_v1 {
	/* See cortex_panic_registers_v1 enum for information about registers */
	uint32_t regs[NUM_CORTEX_PANIC_REGISTERS_V1];

	/* See cortex_panic_frame_registers enum for more information */
	uint32_t frame[NUM_CORTEX_PANIC_FRAME_REGISTERS];

	uint32_t cfsr;
	uint32_t bfar;
	uint32_t mfar;
	uint32_t shcsr;
	uint32_t hfsr;
	uint32_t dfsr;
};

/* ARM Cortex-Mx registers saved on panic */
struct cortex_panic_data {
	/* See cortex_panic_registers enum for information about registers */
	uint32_t regs[NUM_CORTEX_PANIC_REGISTERS];

	/* See cortex_panic_frame_registers enum for more information */
	uint32_t frame[NUM_CORTEX_PANIC_FRAME_REGISTERS];

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
	uint32_t regs[16]; /* r0-r10, r15, fp, gp, lp, sp */
	uint32_t ipc;
	uint32_t ipsw;
};

/* RISC-V RV32I registers saved on panic */
struct rv32i_panic_data {
	uint32_t regs[31]; /* sp, ra, gp, tp, a0-a7, t0-t6, s0-s11 */
	uint32_t mepc; /* mepc */
	uint32_t mcause; /* mcause */
};

/* x86 registers saved on panic */
struct x86_panic_data {
	uint32_t vector; /* Exception vector number */

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
	uint8_t arch; /* Architecture (PANIC_ARCH_*) */
	uint8_t struct_version; /* Structure version (currently 2) */
	uint8_t flags; /* Flags (PANIC_DATA_FLAG_*) */
	uint8_t reserved; /* Reserved; set 0 */

	/* core specific panic data */
	union {
		struct cortex_panic_data_v1 cm_v1; /* V1 Cortex-Mx registers */
		struct cortex_panic_data cm; /* V2+ Cortex-Mx registers */
		struct nds32_n8_panic_data nds_n8; /* NDS32 N8 registers */
		struct x86_panic_data x86; /* Intel x86 */
#ifndef CONFIG_DO_NOT_INCLUDE_RV32I_PANIC_DATA
		struct rv32i_panic_data riscv; /* RISC-V RV32I */
#endif
	};

	/*
	 * These fields go at the END of the struct so we can find it at the
	 * end of memory.
	 */
	uint32_t struct_size; /* Size of this struct */
	uint32_t magic; /* PANIC_SAVE_MAGIC if valid */
};

#define PANIC_DATA_MAGIC 0x21636e50 /* "Pnc!" */
enum panic_arch {
	PANIC_ARCH_CORTEX_M = 1, /* Cortex-M architecture */
	PANIC_ARCH_NDS32_N8 = 2, /* NDS32 N8 architecture */
	PANIC_ARCH_X86 = 3, /* Intel x86 */
#ifndef CONFIG_DO_NOT_INCLUDE_RV32I_PANIC_DATA
	PANIC_ARCH_RISCV_RV32I = 4, /* RISC-V RV32I */
#endif
};

#define PANIC_ZEPHYR_FATAL_ERROR 0xDEAD6800

/* Flags for panic_data.flags */
/* panic_data.frame is valid */
#define PANIC_DATA_FLAG_FRAME_VALID BIT(0)
/* Already printed at console */
#define PANIC_DATA_FLAG_OLD_CONSOLE BIT(1)
/* Already returned via host command */
#define PANIC_DATA_FLAG_OLD_HOSTCMD BIT(2)
/* Already reported via host event */
#define PANIC_DATA_FLAG_OLD_HOSTEVENT BIT(3)
/* The data was truncated to fit panic info host cmd */
#define PANIC_DATA_FLAG_TRUNCATED BIT(4)
/* System safe mode was started after a panic */
#define PANIC_DATA_FLAG_SAFE_MODE_STARTED BIT(5)
/* System safe mode failed to start */
#define PANIC_DATA_FLAG_SAFE_MODE_FAIL_PRECONDITIONS BIT(6)

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_PANIC_DEFS_H */

/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/arch/cpu.h>
#include <zephyr/fatal.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/kernel.h>

#include "common.h"
#include "panic.h"

/*
 * Arch-specific configuration
 *
 * For each architecture, define:
 * - PANIC_ARCH, which should be the corresponding arch field of the
 *   panic_data struct.
 * - PANIC_REG_LIST, which is a macro that takes a parameter M, and
 *   applies M to 3-tuples of:
 *   - zephyr esf field name
 *   - panic_data struct field name
 *   - human readable name
 */

#if defined(CONFIG_ARM)
#define PANIC_ARCH PANIC_ARCH_CORTEX_M
#if defined(CONFIG_EXTRA_EXCEPTION_INFO)
#define EXTRA_PANIC_REG_LIST(M)                                              \
	M(extra_info.callee->v1, cm.regs[CORTEX_PANIC_REGISTER_R4], v1)      \
	M(extra_info.callee->v2, cm.regs[CORTEX_PANIC_REGISTER_R5], v2)      \
	M(extra_info.callee->v3, cm.regs[CORTEX_PANIC_REGISTER_R6], v3)      \
	M(extra_info.callee->v4, cm.regs[CORTEX_PANIC_REGISTER_R7], v4)      \
	M(extra_info.callee->v5, cm.regs[CORTEX_PANIC_REGISTER_R8], v5)      \
	M(extra_info.callee->v6, cm.regs[CORTEX_PANIC_REGISTER_R9], v6)      \
	M(extra_info.callee->v7, cm.regs[CORTEX_PANIC_REGISTER_R10], v7)     \
	M(extra_info.callee->v8, cm.regs[CORTEX_PANIC_REGISTER_R11], v8)     \
	M(extra_info.callee->psp, cm.regs[CORTEX_PANIC_REGISTER_PSP], psp)   \
	M(extra_info.exc_return, cm.regs[CORTEX_PANIC_REGISTER_LR], exc_rtn) \
	M(extra_info.msp, cm.regs[CORTEX_PANIC_REGISTER_MSP], msp)
/*
 * IPSR is not copied. IPSR is a subset of xPSR, which is already
 * captured in PANIC_REG_LIST.
 */
#else
#define EXTRA_PANIC_REG_LIST(M)
#endif
/* TODO(b/245423691): Copy other status registers (e.g. CFSR) when available. */
#define PANIC_REG_LIST(M)                \
	M(basic.r0, cm.frame[0], a1)     \
	M(basic.r1, cm.frame[1], a2)     \
	M(basic.r2, cm.frame[2], a3)     \
	M(basic.r3, cm.frame[3], a4)     \
	M(basic.r12, cm.frame[4], ip)    \
	M(basic.lr, cm.frame[5], lr)     \
	M(basic.pc, cm.frame[6], pc)     \
	M(basic.xpsr, cm.frame[7], xpsr) \
	EXTRA_PANIC_REG_LIST(M)
#define PANIC_REG_EXCEPTION(pdata) pdata->cm.regs[1]
#define PANIC_REG_REASON(pdata) pdata->cm.regs[3]
#define PANIC_REG_INFO(pdata) pdata->cm.regs[4]
#elif defined(CONFIG_RISCV) && !defined(CONFIG_64BIT)
/*
 * Not all registers are passed in the context from Zephyr
 * (see include/zephyr/arch/riscv/exp.h), in particular
 * the mcause register is not saved (mstatus is saved instead).
 * The assignments must match util/ec_panicinfo.c
 */
#define PANIC_ARCH PANIC_ARCH_RISCV_RV32I
#define PANIC_REG_LIST(M)         \
	M(ra, riscv.regs[29], ra) \
	M(a0, riscv.regs[26], a0) \
	M(a1, riscv.regs[25], a1) \
	M(a2, riscv.regs[24], a2) \
	M(a3, riscv.regs[23], a3) \
	M(a4, riscv.regs[22], a4) \
	M(a5, riscv.regs[21], a5) \
	M(a6, riscv.regs[20], a6) \
	M(a7, riscv.regs[19], a7) \
	M(t0, riscv.regs[18], t0) \
	M(t1, riscv.regs[17], t1) \
	M(t2, riscv.regs[16], t2) \
	M(t3, riscv.regs[15], t3) \
	M(t4, riscv.regs[14], t4) \
	M(t5, riscv.regs[13], t5) \
	M(t6, riscv.regs[12], t6) \
	M(mepc, riscv.mepc, mepc) \
	M(mstatus, riscv.mcause, mstatus)
#define PANIC_REG_EXCEPTION(pdata) (pdata->riscv.mcause)
#define PANIC_REG_REASON(pdata) (pdata->riscv.regs[11])
#define PANIC_REG_INFO(pdata) (pdata->riscv.regs[10])
#else
/* Not implemented for this arch */
#define PANIC_ARCH 0
#define PANIC_REG_LIST(M)
#ifdef CONFIG_PLATFORM_EC_SOFTWARE_PANIC
static uint8_t placeholder_exception_reg;
static uint32_t placeholder_reason_reg;
static uint32_t placeholder_info_reg;
#define PANIC_REG_EXCEPTION(unused) placeholder_exception_reg
#define PANIC_REG_REASON(unused) placeholder_reason_reg
#define PANIC_REG_INFO(unused) placeholder_info_reg
#endif /* CONFIG_PLATFORM_EC_SOFTWARE_PANIC */
#endif

/* Macros to be applied to PANIC_REG_LIST as M */
#define PANIC_COPY_REGS(esf_field, pdata_field, human_name) \
	pdata->pdata_field = esf->esf_field;
#define PANIC_PRINT_REGS(esf_field, pdata_field, human_name) \
	panic_printf("  %-8s = 0x%08X\n", #human_name, pdata->pdata_field);

void panic_data_print(const struct panic_data *pdata)
{
	PANIC_REG_LIST(PANIC_PRINT_REGS);
}

static void copy_esf_to_panic_data(const z_arch_esf_t *esf,
				   struct panic_data *pdata)
{
	pdata->arch = PANIC_ARCH;
	pdata->struct_version = 2;
	pdata->flags = (PANIC_ARCH == PANIC_ARCH_CORTEX_M) ?
			       PANIC_DATA_FLAG_FRAME_VALID :
			       0;
	pdata->reserved = 0;
	pdata->struct_size = sizeof(*pdata);
	pdata->magic = PANIC_DATA_MAGIC;

	PANIC_REG_LIST(PANIC_COPY_REGS);
}

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf)
{
	/*
	 * If CONFIG_LOG is on, the exception details
	 * have already been logged to the console.
	 */
	if (!IS_ENABLED(CONFIG_LOG)) {
		panic_printf("Fatal error: %u\n", reason);
	}

	if (PANIC_ARCH && esf) {
		copy_esf_to_panic_data(esf, get_panic_data_write());
		if (!IS_ENABLED(CONFIG_LOG)) {
			panic_data_print(panic_get_data());
		}
	}

	LOG_PANIC();
	/*
	 * Reboot immediately, don't wait for watchdog, otherwise
	 * the watchdog will overwrite this panic.
	 */
	panic_reboot();
	CODE_UNREACHABLE;
}

#ifdef CONFIG_PLATFORM_EC_SOFTWARE_PANIC
void panic_set_reason(uint32_t reason, uint32_t info, uint8_t exception)
{
	struct panic_data *const pdata = get_panic_data_write();

	/* Setup panic data structure */
	memset(pdata, 0, CONFIG_PANIC_DATA_SIZE);
	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = CONFIG_PANIC_DATA_SIZE;
	pdata->struct_version = 2;
	pdata->arch = PANIC_ARCH;

	/* Log panic cause */
	PANIC_REG_EXCEPTION(pdata) = exception;
	PANIC_REG_REASON(pdata) = reason;
	PANIC_REG_INFO(pdata) = info;

	/* Allow architecture specific logic */
	arch_panic_set_reason(reason, info, exception);
}

void panic_get_reason(uint32_t *reason, uint32_t *info, uint8_t *exception)
{
	struct panic_data *const pdata = panic_get_data();

	if (pdata && pdata->struct_version == 2) {
		*exception = PANIC_REG_EXCEPTION(pdata);
		*reason = PANIC_REG_REASON(pdata);
		*info = PANIC_REG_INFO(pdata);
	} else {
		*exception = *reason = *info = 0;
	}
}

__overridable void arch_panic_set_reason(uint32_t reason, uint32_t info,
					 uint8_t exception)
{
	/* Default implementation, do nothing. */
}
#endif /* CONFIG_PLATFORM_EC_SOFTWARE_PANIC */

/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "cpu.h"
#include "panic.h"
#include "task.h"
#include "util.h"

#ifdef CONFIG_DEBUG_EXCEPTIONS
/**
 * bit[3-0] @ mcause, general exception type information.
 */
static const char *const exc_type[16] = {
	"Instruction address misaligned",
	"Instruction access fault",
	"Illegal instruction",
	"Breakpoint",
	"Load address misaligned",
	"Load access fault",
	"Store/AMO address misaligned",
	"Store/AMO access fault",

	NULL,
	NULL,
	NULL,
	"Environment call from M-mode",
	NULL,
	NULL,
	NULL,
	NULL,
};
#endif /* CONFIG_DEBUG_EXCEPTIONS */

/* General purpose register (s0) for saving software panic reason */
#define SOFT_PANIC_GPR_REASON 11
/* General purpose register (s1) for saving software panic information */
#define SOFT_PANIC_GPR_INFO 10

void software_panic(uint32_t reason, uint32_t info)
{
	asm volatile("mv s0, %0" : : "r"(reason) : "s0");
	asm volatile("mv s1, %0" : : "r"(info) : "s1");
	if (in_interrupt_context())
		asm("j excep_handler");
	else
		asm("ebreak");
	__builtin_unreachable();
}

void panic_set_reason(uint32_t reason, uint32_t info, uint8_t exception)
{
	/*
	 * It is safe to get pointer using get_panic_data_write().
	 * If it was called earlier (eg. when saving riscv.mepc) calling it
	 * once again won't remove any data
	 */
	struct panic_data *const pdata = get_panic_data_write();
	uint32_t *regs;

	regs = pdata->riscv.regs;

	/* Setup panic data structure */
	memset(pdata, 0, CONFIG_PANIC_DATA_SIZE);
	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = CONFIG_PANIC_DATA_SIZE;
	pdata->struct_version = 2;
	pdata->arch = PANIC_ARCH_RISCV_RV32I;

	/* Log panic cause */
	pdata->riscv.mcause = exception;
	regs[SOFT_PANIC_GPR_REASON] = reason;
	regs[SOFT_PANIC_GPR_INFO] = info;
}

void panic_get_reason(uint32_t *reason, uint32_t *info, uint8_t *exception)
{
	struct panic_data *const pdata = panic_get_data();
	uint32_t *regs;

	if (pdata && pdata->struct_version == 2) {
		regs = pdata->riscv.regs;
		*exception = pdata->riscv.mcause;
		*reason = regs[SOFT_PANIC_GPR_REASON];
		*info = regs[SOFT_PANIC_GPR_INFO];
	} else {
		*exception = *reason = *info = 0;
	}
}

static void print_panic_information(uint32_t *regs, uint32_t mcause,
				    uint32_t mepc)
{
	panic_printf("=== EXCEPTION: MCAUSE=%x ===\n", mcause);
	panic_printf("S11 %08x S10 %08x  S9 %08x  S8   %08x\n", regs[0],
		     regs[1], regs[2], regs[3]);
	panic_printf("S7  %08x S6  %08x  S5 %08x  S4   %08x\n", regs[4],
		     regs[5], regs[6], regs[7]);
	panic_printf("S3  %08x S2  %08x  S1 %08x  S0   %08x\n", regs[8],
		     regs[9], regs[10], regs[11]);
	panic_printf("T6  %08x T5  %08x  T4 %08x  T3   %08x\n", regs[12],
		     regs[13], regs[14], regs[15]);
	panic_printf("T2  %08x T1  %08x  T0 %08x  A7   %08x\n", regs[16],
		     regs[17], regs[18], regs[19]);
	panic_printf("A6  %08x A5  %08x  A4 %08x  A3   %08x\n", regs[20],
		     regs[21], regs[22], regs[23]);
	panic_printf("A2  %08x A1  %08x  A0 %08x  TP   %08x\n", regs[24],
		     regs[25], regs[26], regs[27]);
	panic_printf("GP  %08x RA  %08x  SP %08x  MEPC %08x\n", regs[28],
		     regs[29], regs[30], mepc);

#ifdef CONFIG_DEBUG_EXCEPTIONS
	if ((regs[SOFT_PANIC_GPR_REASON] & 0xfffffff0) == PANIC_SW_BASE) {
		panic_printf("Software panic reason: %s\n",
			     panic_sw_reasons[(regs[SOFT_PANIC_GPR_REASON] -
					       PANIC_SW_BASE)]);
		panic_printf("Software panic info:   %d\n",
			     regs[SOFT_PANIC_GPR_INFO]);
	} else {
		panic_printf("Exception type: %s\n", exc_type[(mcause & 0xf)]);
	}
#endif
}

void report_panic(uint32_t *regs)
{
	uint32_t i, mcause, mepc;
	struct panic_data *const pdata = get_panic_data_write();

	mepc = get_mepc();
	mcause = get_mcause();

	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = CONFIG_PANIC_DATA_SIZE;
	pdata->struct_version = 2;
	pdata->arch = PANIC_ARCH_RISCV_RV32I;
	pdata->flags = 0;
	pdata->reserved = 0;

	pdata->riscv.mcause = mcause;
	pdata->riscv.mepc = mepc;
	for (i = 0; i < 31; i++)
		pdata->riscv.regs[i] = regs[i];

	print_panic_information(regs, mcause, mepc);
	panic_reboot();
}

void panic_data_print(const struct panic_data *pdata)
{
	uint32_t *regs, mcause, mepc;

	regs = (uint32_t *)pdata->riscv.regs;
	mcause = pdata->riscv.mcause;
	mepc = pdata->riscv.mepc;
	print_panic_information(regs, mcause, mepc);
}

#ifdef CONFIG_PANIC_CONSOLE_OUTPUT
static void ccprint_panic_information(uint32_t *regs, uint32_t mcause,
				      uint32_t mepc)
{
	ccprintf("=== EXCEPTION: MCAUSE=%x ===\n", mcause);
	ccprintf("S11 %08x S10 %08x  S9 %08x  S8   %08x\n", regs[0], regs[1],
		 regs[2], regs[3]);
	ccprintf("S7  %08x S6  %08x  S5 %08x  S4   %08x\n", regs[4], regs[5],
		 regs[6], regs[7]);
	ccprintf("S3  %08x S2  %08x  S1 %08x  S0   %08x\n", regs[8], regs[9],
		 regs[10], regs[11]);
	ccprintf("T6  %08x T5  %08x  T4 %08x  T3   %08x\n", regs[12], regs[13],
		 regs[14], regs[15]);
	ccprintf("T2  %08x T1  %08x  T0 %08x  A7   %08x\n", regs[16], regs[17],
		 regs[18], regs[19]);
	cflush();

	ccprintf("A6  %08x A5  %08x  A4 %08x  A3   %08x\n", regs[20], regs[21],
		 regs[22], regs[23]);
	ccprintf("A2  %08x A1  %08x  A0 %08x  TP   %08x\n", regs[24], regs[25],
		 regs[26], regs[27]);
	ccprintf("GP  %08x RA  %08x  SP %08x  MEPC %08x\n", regs[28], regs[29],
		 regs[30], mepc);

#ifdef CONFIG_DEBUG_EXCEPTIONS
	if ((regs[SOFT_PANIC_GPR_REASON] & 0xfffffff0) == PANIC_SW_BASE) {
		ccprintf("Software panic reason: %s\n",
			 panic_sw_reasons[(regs[SOFT_PANIC_GPR_REASON] -
					   PANIC_SW_BASE)]);
		ccprintf("Software panic info:   %d\n",
			 regs[SOFT_PANIC_GPR_INFO]);
	} else {
		ccprintf("Exception type: %s\n", exc_type[(mcause & 0xf)]);
	}
#endif /* CONFIG_DEBUG_EXCEPTIONS */
	cflush();
}
void panic_data_ccprint(const struct panic_data *pdata)
{
	uint32_t *regs, mcause, mepc;

	regs = (uint32_t *)pdata->riscv.regs;
	mcause = pdata->riscv.mcause;
	mepc = pdata->riscv.mepc;
	ccprint_panic_information(regs, mcause, mepc);
}
#endif /* CONFIG_PANIC_CONSOLE_OUTPUT */

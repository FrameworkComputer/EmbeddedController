/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "cpu.h"
#include "host_command.h"
#include "panic-internal.h"
#include "panic.h"
#include "printf.h"
#include "system.h"
#include "system_safe_mode.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "watchdog.h"

#define BASE_EXCEPTION_FRAME_SIZE_BYTES (8 * sizeof(uint32_t))
#define FPU_EXCEPTION_FRAME_SIZE_BYTES (18 * sizeof(uint32_t))

/* Whether bus fault is ignored */
static int bus_fault_ignored;

/* Panic data goes at the end of RAM. */
static struct panic_data *const pdata_ptr = PANIC_DATA_PTR;

/* Preceded by stack, rounded down to nearest 64-bit-aligned boundary */
static const uint32_t pstack_addr = ((uint32_t)pdata_ptr) & ~7;

/**
 * Print the name and value of a register
 *
 * This is a convenient helper function for displaying a register value.
 * It shows the register name in a 3 character field, followed by a colon.
 * The register value is regs[index], and this is shown in hex. If regs is
 * NULL, then we display spaces instead.
 *
 * After displaying the value, either a space or \n is displayed depending
 * on the register number, so that (assuming the caller passes all 16
 * registers in sequence) we put 4 values per line like this
 *
 * r0 :0000000b r1 :00000047 r2 :60000000 r3 :200012b5
 * r4 :00000000 r5 :08004e64 r6 :08004e1c r7 :200012a8
 * r8 :08004e64 r9 :00000002 r10:00000000 r11:00000000
 * r12:0000003f sp :200009a0 lr :0800270d pc :0800351a
 *
 * @param regnum	Register number to display (0-15)
 * @param regs		Pointer to array holding the registers, or NULL
 * @param index		Index into array where the register value is present
 */
static void print_reg(int regnum, const uint32_t *regs, int index)
{
	static const char regname[] = "r10r11r12sp lr pc ";
	static char rname[3] = "r  ";
	const char *name;

	rname[1] = '0' + regnum;
	name = regnum < 10 ? rname : &regname[(regnum - 10) * 3];
	panic_printf("%c%c%c:", name[0], name[1], name[2]);
	if (regs)
		panic_printf("%08x", regs[index]);
	else
		panic_puts("        ");
	panic_puts((regnum & 3) == 3 ? "\n" : " ");
}

/*
 * Returns non-zero if the exception frame was created on the main stack, or
 * zero if it's on the process stack.
 *
 * See B1.5.8 "Exception return behavior" of ARM DDI 0403D for details.
 */
static int32_t is_frame_in_handler_stack(const uint32_t exc_return)
{
#ifdef CONFIG_FPU
	return exc_return == 0xfffffff1 || exc_return == 0xfffffff9 ||
	       exc_return == 0xffffffe1 || exc_return == 0xffffffe9;
#else
	return exc_return == 0xfffffff1 || exc_return == 0xfffffff9;
#endif /* CONFIG_FPU */
}

/*
 * Returns the size of the exception frame.
 *
 * See B1.5.7 "Stack alignment on exception entry" of ARM DDI 0403D for details.
 * In short, the exception frame size can be either 0x20, 0x24, 0x68, or 0x6c
 * depending on FPU context and padding for 8-byte alignment.
 */
static uint32_t get_exception_frame_size(const struct panic_data *pdata)
{
	uint32_t frame_size = 0;

	/* base exception frame */
	frame_size += BASE_EXCEPTION_FRAME_SIZE_BYTES;

	/* CPU uses xPSR[9] to indicate whether it padded the stack for
	 * alignment or not.
	 */
	if (pdata->cm.frame[CORTEX_PANIC_FRAME_REGISTER_PSR] & BIT(9))
		frame_size += sizeof(uint32_t);

#ifdef CONFIG_FPU
	/* CPU uses EXC_RETURN[4] to indicate whether it stored extended
	 * frame for FPU or not.
	 */
	if (!(pdata->cm.regs[CORTEX_PANIC_REGISTER_LR] & BIT(4)))
		frame_size += FPU_EXCEPTION_FRAME_SIZE_BYTES;
#endif

	return frame_size;
}

/*
 * Returns the position of the process stack before the exception frame.
 * It computes the size of the exception frame and adds it to psp.
 * If the exception happened in the exception context, it returns psp as is.
 */
uint32_t get_panic_stack_pointer(const struct panic_data *pdata)
{
	uint32_t psp = pdata->cm.regs[CORTEX_PANIC_REGISTER_PSP];

	if (!is_frame_in_handler_stack(
		    pdata->cm.regs[CORTEX_PANIC_REGISTER_LR]))
		psp += get_exception_frame_size(pdata);

	return psp;
}

#ifdef CONFIG_DEBUG_EXCEPTIONS
/*
 * Names for each of the bits in the cfs register, starting at bit 0
 *
 * Note that __builtin_trap will usually cause "Undefined instruction".
 */
static const char *const cfsr_name[32] = {
	/* MMFSR */
	[0] = "Instruction access violation",
	[1] = "Data access violation",
	[3] = "Unstack from exception violation",
	[4] = "Stack from exception violation",

	/* BFSR */
	[8] = "Instruction bus error",
	[9] = "Precise data bus error",
	[10] = "Imprecise data bus error",
	[11] = "Unstack from exception bus fault",
	[12] = "Stack from exception bus fault",

	/* UFSR */
	[16] = "Undefined instructions",
	[17] = "Invalid state",
	[18] = "Invalid PC",
	[19] = "No coprocessor",
	[24] = "Unaligned",
	[25] = "Divide by 0",
};

/* Names for the first 5 bits in the DFSR */
static const char *const dfsr_name[] = {
	"Halt request",		  "Breakpoint",
	"Data watchpoint/trace",  "Vector catch",
	"External debug request",
};

/**
 * Helper function to display a separator after the previous item
 *
 * If items have been displayed already, we display a comma separator.
 * In any case, the count of items displayed is incremeneted.
 *
 * @param count		Number of items displayed so far (0 for none)
 */
static void do_separate(int *count)
{
	if (*count)
		panic_puts(", ");
	(*count)++;
}

/**
 * Show a textual representaton of the fault registers
 *
 * A list of detected faults is shown, with no trailing newline.
 *
 * @param cfsr		Value of Configurable Fault Status
 * @param hfsr		Value of Hard Fault Status
 * @param dfsr		Value of Debug Fault Status
 */
static void show_fault(uint32_t cfsr, uint32_t hfsr, uint32_t dfsr)
{
	unsigned int upto;
	int count = 0;

	for (upto = 0; upto < 32; upto++) {
		if ((cfsr & BIT(upto)) && cfsr_name[upto]) {
			do_separate(&count);
			panic_puts(cfsr_name[upto]);
		}
	}

	if (hfsr & CPU_NVIC_HFSR_DEBUGEVT) {
		do_separate(&count);
		panic_puts("Debug event");
	}
	if (hfsr & CPU_NVIC_HFSR_FORCED) {
		do_separate(&count);
		panic_puts("Forced hard fault");
	}
	if (hfsr & CPU_NVIC_HFSR_VECTTBL) {
		do_separate(&count);
		panic_puts("Vector table bus fault");
	}

	for (upto = 0; upto < 5; upto++) {
		if ((dfsr & BIT(upto))) {
			do_separate(&count);
			panic_puts(dfsr_name[upto]);
		}
	}
}

/*
 * Show extra information that might be useful to understand a panic()
 *
 * We show fault register information, including the fault address registers
 * if valid.
 */
static void panic_show_extra(const struct panic_data *pdata)
{
	show_fault(pdata->cm.cfsr, pdata->cm.hfsr, pdata->cm.dfsr);
	if (pdata->cm.cfsr & CPU_NVIC_CFSR_BFARVALID)
		panic_printf(", bfar = %x", pdata->cm.bfar);
	if (pdata->cm.cfsr & CPU_NVIC_CFSR_MFARVALID)
		panic_printf(", mfar = %x", pdata->cm.mfar);
	panic_printf("\ncfsr = %x, ", pdata->cm.cfsr);
	panic_printf("shcsr = %x, ", pdata->cm.shcsr);
	panic_printf("hfsr = %x, ", pdata->cm.hfsr);
	panic_printf("dfsr = %x\n", pdata->cm.dfsr);
}

/*
 * Prints process stack contents stored above the exception frame.
 */
static void panic_show_process_stack(const struct panic_data *pdata)
{
	panic_printf("\n=========== Process Stack Contents ===========");
	if (pdata->flags & PANIC_DATA_FLAG_FRAME_VALID) {
		uint32_t psp = get_panic_stack_pointer(pdata);
		int i;
		for (i = 0; i < 16; i++) {
			if (psp + sizeof(uint32_t) >
			    CONFIG_RAM_BASE + CONFIG_RAM_SIZE)
				break;
			if (i % 4 == 0)
				panic_printf("\n%08x:", psp);
			panic_printf(" %08x", *(uint32_t *)psp);
			psp += sizeof(uint32_t);
		}
	} else {
		panic_printf("\nBad psp");
	}
}
#endif /* CONFIG_DEBUG_EXCEPTIONS */

/*
 * Print panic data
 */
void panic_data_print(const struct panic_data *pdata)
{
	const uint32_t *lregs = pdata->cm.regs;
	const uint32_t *sregs = NULL;
	const int32_t in_handler = is_frame_in_handler_stack(
		pdata->cm.regs[CORTEX_PANIC_REGISTER_LR]);
	int i;

	if (pdata->flags & PANIC_DATA_FLAG_FRAME_VALID)
		sregs = pdata->cm.frame;

	panic_printf("\n=== %s EXCEPTION: %02x ====== xPSR: %08x ===\n",
		     in_handler ? "HANDLER" : "PROCESS",
		     lregs[CORTEX_PANIC_REGISTER_IPSR] & 0xff,
		     sregs ? sregs[CORTEX_PANIC_FRAME_REGISTER_PSR] : -1);
	for (i = 0; i < 4; i++)
		print_reg(i, sregs, i);
	for (i = 4; i < 10; i++)
		print_reg(i, lregs, i - 1);
	print_reg(10, lregs, CORTEX_PANIC_REGISTER_R10);
	print_reg(11, lregs, CORTEX_PANIC_REGISTER_R11);
	print_reg(12, sregs, CORTEX_PANIC_FRAME_REGISTER_R12);
	print_reg(13, lregs,
		  in_handler ? CORTEX_PANIC_REGISTER_MSP :
			       CORTEX_PANIC_REGISTER_PSP);
	print_reg(14, sregs, CORTEX_PANIC_FRAME_REGISTER_LR);
	print_reg(15, sregs, CORTEX_PANIC_FRAME_REGISTER_PC);

#ifdef CONFIG_DEBUG_EXCEPTIONS
	panic_show_extra(pdata);
#endif
}

/*
 * Handle returning from the exception handler to task context.
 * The task has already been disabled, but may continue to run
 * until the next interrupt. Calling `task_disable_task` again
 * from the task context will force a task switch.
 */
static void exception_return_handler(void)
{
	/* Force a task switch */
	task_disable_task(task_get_current());
	/* Something went wrong, just reboot */
	panic_reboot();
	__builtin_unreachable();
}

void __keep report_panic(void)
{
	/*
	 * Don't need to get pointer via get_panic_data_write()
	 * because memory below pdata_ptr is stack now (see exception_panic())
	 */
	struct panic_data *pdata = pdata_ptr;
	uint32_t sp;

	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = sizeof(*pdata);
	pdata->struct_version = 2;
	pdata->arch = PANIC_ARCH_CORTEX_M;
	pdata->flags = 0;
	pdata->reserved = 0;

	/* Choose the right sp (psp or msp) based on EXC_RETURN value */
	sp = is_frame_in_handler_stack(
		     pdata->cm.regs[CORTEX_PANIC_REGISTER_LR]) ?
		     pdata->cm.regs[CORTEX_PANIC_REGISTER_MSP] :
		     pdata->cm.regs[CORTEX_PANIC_REGISTER_PSP];
	/* If stack is valid, copy exception frame to pdata */
	if ((sp & 3) == 0 && sp >= CONFIG_RAM_BASE &&
	    sp <= CONFIG_RAM_BASE + CONFIG_RAM_SIZE -
			    BASE_EXCEPTION_FRAME_SIZE_BYTES) {
		const uint32_t *sregs = (const uint32_t *)sp;
		int i;

		/* Skip r0-r3 and r12 registers if necessary */
		for (i = CORTEX_PANIC_FRAME_REGISTER_R0;
		     i <= CORTEX_PANIC_FRAME_REGISTER_R12; i++)
			if (IS_ENABLED(CONFIG_PANIC_STRIP_GPR))
				pdata->cm.frame[i] = 0;
			else
				pdata->cm.frame[i] = sregs[i];

		for (i = CORTEX_PANIC_FRAME_REGISTER_LR;
		     i < NUM_CORTEX_PANIC_FRAME_REGISTERS; i++)
			pdata->cm.frame[i] = sregs[i];

		pdata->flags |= PANIC_DATA_FLAG_FRAME_VALID;
	}

	/* Save extra information */
	pdata->cm.cfsr = CPU_NVIC_CFSR;
	pdata->cm.bfar = CPU_NVIC_BFAR;
	pdata->cm.mfar = CPU_NVIC_MFAR;
	pdata->cm.shcsr = CPU_NVIC_SHCSR;
	pdata->cm.hfsr = CPU_NVIC_HFSR;
	pdata->cm.dfsr = CPU_NVIC_DFSR;

#ifdef CONFIG_UART_PAD_SWITCH
	uart_reset_default_pad_panic();
#endif
	panic_data_print(pdata);
#ifdef CONFIG_DEBUG_EXCEPTIONS
	panic_show_process_stack(pdata);
	/*
	 * TODO(crosbug.com/p/23760): Dump main stack contents as well if the
	 * exception happened in a handler's context.
	 */
#endif

	/* Make sure that all changes are saved into RAM */
	if (IS_ENABLED(CONFIG_ARMV7M_CACHE))
		cpu_clean_invalidate_dcache();

	if (IS_ENABLED(CONFIG_CMD_CRASH_NESTED))
		command_crash_nested_handler();

	/* Start safe mode if possible */
	if (IS_ENABLED(CONFIG_SYSTEM_SAFE_MODE)) {
		/* Only start safe mode if panic occurred in thread context */
		if (!is_frame_in_handler_stack(
			    pdata->cm.regs[CORTEX_PANIC_REGISTER_LR]) &&
		    start_system_safe_mode() == EC_SUCCESS) {
			pdata->flags |= PANIC_DATA_FLAG_SAFE_MODE_STARTED;
			/* If not in an interrupt context (e.g. software_panic),
			 * the next highest priority task will immediately
			 * execute when the current task is disabled on the
			 * following line.
			 */
			task_disable_task(task_get_current());
			/* Return from exception on process stack.
			 * The scheduler will switch to a different task
			 * on the next interrupt since the current task has
			 * been disabled.
			 */
			cpu_return_from_exception_psp(exception_return_handler);
			__builtin_unreachable();
		}
		pdata->flags |= PANIC_DATA_FLAG_SAFE_MODE_FAIL_PRECONDITIONS;
	}

	panic_reboot();
}

/**
 * Default exception handler, which reports a panic.
 *
 * Declare this as a naked call so we can extract raw LR and IPSR values.
 */
void exception_panic(void)
{
	/* Save registers and branch directly to panic handler */
	asm volatile(
		"mrs r1, psp\n"
		"mrs r2, ipsr\n"
		"mov r3, sp\n"
#ifdef CONFIG_PANIC_STRIP_GPR
		/*
		 * Check if we are in exception. This is similar to
		 * in_interrupt_context(). Exception bits are 9 LSB, so
		 * we can perform left shift for 23 bits and check if result
		 * is 0 (lsls instruction is setting appropriate flags).
		 */
		"lsls r6, r2, #23\n"
		/*
		 * If this is software panic (shift result == 0) then register
		 * r4 and r5 contain additional info about panic.
		 * Clear r6-r11 always and r4, r5 only if this is exception
		 * panic. To clear r4 and r5, 'movne' conditional instruction
		 * is used. It works only when flags contain information that
		 * result was != 0. Itt is pseudo instruction which is used
		 * to make sure we are using correct conditional instructions.
		 */
		"itt ne\n"
		"movne r4, #0\n"
		"movne r5, #0\n"
		"mov r6, #0\n"
		"mov r7, #0\n"
		"mov r8, #0\n"
		"mov r9, #0\n"
		"mov r10, #0\n"
		"mov r11, #0\n"
#endif
		"stmia %[pregs], {r1-r11, lr}\n"
		"mov sp, %[pstack]\n"
		"bl report_panic\n"
		:
		: [pregs] "r"(pdata_ptr->cm.regs), [pstack] "r"(pstack_addr)
		:
		/* Constraints protecting these from being clobbered.
		 * Gcc should be using r0 & r12 for pregs and pstack. */
		"r1", "r2", "r3", "r4", "r5", "r6",
	/* clang warns that we're clobbering a reserved register:
	 * inline asm clobber list contains reserved registers: R7
	 * [-Werror,-Winline-asm]. The intent of the clobber list is
	 * to force pregs and pstack to be in R0 and R12, which
	 * still holds.
	 */
#ifndef __clang__
		"r7",
#endif
		"r8", "r9", "r10", "r11", "cc", "memory");
}

void software_panic(uint32_t reason, uint32_t info)
{
	__asm__("mov " STRINGIFY(
			SOFTWARE_PANIC_INFO_REG) ", %0\n"
						 "mov " STRINGIFY(
							 SOFTWARE_PANIC_REASON_REG) ", %1\n"
										    "bl exception_panic\n"
		:
		: "r"(info), "r"(reason));
	__builtin_unreachable();
}

void panic_set_reason(uint32_t reason, uint32_t info, uint8_t exception)
{
	struct panic_data *const pdata = get_panic_data_write();
	uint32_t *lregs;

	lregs = pdata->cm.regs;

	/* Setup panic data structure */
	memset(pdata, 0, CONFIG_PANIC_DATA_SIZE);
	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = CONFIG_PANIC_DATA_SIZE;
	pdata->struct_version = 2;
	pdata->arch = PANIC_ARCH_CORTEX_M;

	/* Log panic cause */
	lregs[CORTEX_PANIC_REGISTER_IPSR] = exception;
	lregs[CORTEX_PANIC_REGISTER_R4] = reason;
	lregs[CORTEX_PANIC_REGISTER_R5] = info;
}

void panic_get_reason(uint32_t *reason, uint32_t *info, uint8_t *exception)
{
	struct panic_data *const pdata = panic_get_data();
	uint32_t *lregs;

	if (pdata && pdata->struct_version == 2) {
		lregs = pdata->cm.regs;
		*exception = lregs[CORTEX_PANIC_REGISTER_IPSR];
		*reason = lregs[CORTEX_PANIC_REGISTER_R4];
		*info = lregs[CORTEX_PANIC_REGISTER_R5];
	} else {
		*exception = *reason = *info = 0;
	}
}

void bus_fault_handler(void)
{
	if (!bus_fault_ignored)
		exception_panic();
}

void ignore_bus_fault(int ignored)
{
	/*
	 * According to
	 * https://developer.arm.com/documentation/ddi0403/d/System-Level-Architecture/System-Level-Programmers--Model/Overview-of-system-level-terminology-and-operation/Exceptions?lang=en,
	 * the Imprecise BusFault is an asynchronous fault in ARMv7-M.
	 *
	 * Before re-enabling the bus fault, we use a barrier to make sure that
	 * the fault has been processed.
	 */
	if (ignored == 0)
		asm volatile("dsb; isb");

	/*
	 * Flash code might call this before cpu_init(),
	 * ensure that the bus faults really go through our handler.
	 */
	CPU_NVIC_SHCSR |= CPU_NVIC_SHCSR_BUSFAULTENA;
	bus_fault_ignored = ignored;
}

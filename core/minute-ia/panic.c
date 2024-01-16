/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "cpu.h"
#include "host_command.h"
#include "mia_panic_internal.h"
#include "panic.h"
#include "printf.h"
#include "software_panic.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/*
 * This array maps an interrupt vector number to the corresponding
 * exception name. See see "Intel 64 and IA-32 Architectures Software
 * Developer's Manual", Volume 3A, Section 6.15.
 */
const static char *panic_reason[] = {
	"Divide By Zero",
	"Debug Exception",
	"NMI Interrupt",
	"Breakpoint Exception",
	"Overflow Exception",
	"BOUND Range Exceeded Exception",
	"Invalid Opcode Exception",
	"Device Not Available Exception",
	"Double Fault Exception",
	"Coprocessor Segment Overrun",
	"Invalid TSS Exception",
	"Segment Not Present",
	"Stack Fault Exception",
	"General Protection Fault",
	"Page Fault",
	"Reserved",
	"Math Fault",
	"Alignment Check Exception",
	"Machine Check Exception",
	"SIMD Floating Point Exception",
	"Virtualization Exception",
};

/*
 * Print panic data. This may be called either from the report_panic
 * procedure (below) while handling a panic, or from the panicinfo
 * console command.
 */
void panic_data_print(const struct panic_data *pdata)
{
	if (pdata->x86.vector == PANIC_SW_WATCHDOG)
		panic_printf("Reason: Watchdog Expiration\n");
	else if (pdata->x86.vector <= 20)
		panic_printf("Reason: %s\n", panic_reason[pdata->x86.vector]);
	else if (panic_sw_reason_is_valid(pdata->x86.vector)) {
		panic_printf(
			"Software panic reason %s\n",
			panic_sw_reasons[pdata->x86.vector - PANIC_SW_BASE]);
		panic_printf("Software panic info 0x%x\n",
			     pdata->x86.error_code);
	} else
		panic_printf("Interrupt vector number: 0x%08X (unknown)\n",
			     pdata->x86.vector);
	panic_printf("\n");
	panic_printf("Error Code = 0x%08X\n", pdata->x86.error_code);
	panic_printf("EIP        = 0x%08X\n", pdata->x86.eip);
	panic_printf("CS         = 0x%08X\n", pdata->x86.cs);
	panic_printf("EFLAGS     = 0x%08X\n", pdata->x86.eflags);
	panic_printf("EAX        = 0x%08X\n", pdata->x86.eax);
	panic_printf("EBX        = 0x%08X\n", pdata->x86.ebx);
	panic_printf("ECX        = 0x%08X\n", pdata->x86.ecx);
	panic_printf("EDX        = 0x%08X\n", pdata->x86.edx);
	panic_printf("ESI        = 0x%08X\n", pdata->x86.esi);
	panic_printf("EDI        = 0x%08X\n", pdata->x86.edi);
	panic_printf("EC Task    = %s\n", task_get_name(pdata->x86.task_id));
}

/**
 * Default exception handler, which reports a panic.
 *
 * The first parameter should be pushed by a software routine aware of
 * the interrupt vector number (see DEFINE_EXN_HANDLER macro in
 * interrupts.c).
 *
 * The remaining parameters (error_code, eip, cs, eflags) are in the
 * order pushed to the stack by hardware: see "Intel 64 and IA-32
 * Architectures Software Developer's Manual", Volume 3A, Figure 6-4.
 */
void exception_panic(uint32_t vector, uint32_t error_code, uint32_t eip,
		     uint32_t cs, uint32_t eflags)
{
	/*
	 * If a panic were to occur during the reset procedure, we want
	 * to make sure that this panic will certainly cause a hard
	 * reset, rather than aontaskfw reset. Track if paniced once
	 * already.
	 */
	static int panic_once;
	struct panic_data *pdata;

	register uint32_t eax asm("eax");
	register uint32_t ebx asm("ebx");
	register uint32_t ecx asm("ecx");
	register uint32_t edx asm("edx");
	register uint32_t esi asm("esi");
	register uint32_t edi asm("edi");
	asm(""
	    : "=r"(eax), "=r"(ebx), "=r"(ecx), "=r"(edx), "=r"(esi), "=r"(edi));

	pdata = get_panic_data_write();

	/* Save registers to global panic structure */
	pdata->x86.eax = eax;
	pdata->x86.ebx = ebx;
	pdata->x86.ecx = ecx;
	pdata->x86.edx = edx;
	pdata->x86.esi = esi;
	pdata->x86.edi = edi;

	/*
	 * Convert watchdog timer vector number to be a SW
	 * Watchdog. This is for code that is in
	 * system_common_pre_init
	 */
	if (IS_ENABLED(CONFIG_WATCHDOG) && vector == CONFIG_MIA_WDT_VEC)
		vector = PANIC_SW_WATCHDOG;

	/* Save stack data to global panic structure */
	pdata->x86.vector = vector;
	pdata->x86.error_code = error_code;
	pdata->x86.eip = eip;
	pdata->x86.cs = cs;
	pdata->x86.eflags = eflags;

	/* Save task information */
	pdata->x86.task_id = task_get_current();

	/* Initialize panic data */
	pdata->arch = PANIC_ARCH_X86;
	pdata->struct_version = 2;
	pdata->magic = PANIC_DATA_MAGIC;

	/* Display the panic and reset */
	if (panic_once)
		panic_printf("\nWhile resetting from a panic, another panic"
			     " occurred!");

	panic_printf("\n========== PANIC ==========\n");
	panic_data_print(pdata);
	panic_printf("\n");
	panic_printf("Resetting system...\n");
	panic_printf("===========================\n");

	/*
	 * Post increment panic_once to make sure we only go through
	 * once before we resort to a hard reset
	 */
	if (panic_once++)
		system_reset(SYSTEM_RESET_HARD);
	else if (vector == PANIC_SW_WATCHDOG)
		system_reset(SYSTEM_RESET_AP_WATCHDOG);
	else if (panic_sw_reason_is_valid(vector))
		system_reset(SYSTEM_RESET_MANUALLY_TRIGGERED);
	else
		system_reset(0);

	__builtin_unreachable();
}

__noreturn void software_panic(uint32_t reason, uint32_t info)
{
	uint16_t code_segment;

	/* Get the current code segment */
	__asm__ volatile("movw  %%cs, %0" : "=m"(code_segment));

	exception_panic(reason, info, (uint32_t)__builtin_return_address(0),
			code_segment, 0);

	__builtin_unreachable();
}

void panic_set_reason(uint32_t reason, uint32_t info, uint8_t exception)
{
	struct panic_data *const pdata = get_panic_data_write();

	/* Setup panic data structure */
	memset(pdata, 0, CONFIG_PANIC_DATA_SIZE);
	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = CONFIG_PANIC_DATA_SIZE;
	pdata->struct_version = 2;
	pdata->arch = PANIC_ARCH_X86;

	/* Log panic cause */
	pdata->x86.vector = reason;
	pdata->x86.error_code = info;
	pdata->x86.eflags = exception;
}

void panic_get_reason(uint32_t *reason, uint32_t *info, uint8_t *exception)
{
	struct panic_data *const pdata = panic_get_data();

	if (pdata && pdata->struct_version == 2) {
		*reason = pdata->x86.vector;
		*info = pdata->x86.error_code;
		*exception = pdata->x86.eflags;
	} else {
		*reason = *info = *exception = 0;
	}
}

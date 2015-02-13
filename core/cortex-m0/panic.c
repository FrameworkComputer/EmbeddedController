/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "cpu.h"
#include "host_command.h"
#include "panic.h"
#include "printf.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "watchdog.h"

/* Whether bus fault is ignored */
static int bus_fault_ignored;


/* Panic data goes at the end of RAM. */
static struct panic_data * const pdata_ptr = PANIC_DATA_PTR;

/* Preceded by stack, rounded down to nearest 64-bit-aligned boundary */
static const uint32_t pstack_addr = (CONFIG_RAM_BASE + CONFIG_RAM_SIZE
				     - sizeof(struct panic_data)) & ~7;

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
	return (exc_return & 0xf) == 1 || (exc_return & 0xf) == 9;
}

/*
 * Print panic data
 */
void panic_data_print(const struct panic_data *pdata)
{
	const uint32_t *lregs = pdata->cm.regs;
	const uint32_t *sregs = NULL;
	const int32_t in_handler =
		is_frame_in_handler_stack(pdata->cm.regs[11]);
	int i;

	if (pdata->flags & PANIC_DATA_FLAG_FRAME_VALID)
		sregs = pdata->cm.frame;

	panic_printf("\n=== %s EXCEPTION: %02x ====== xPSR: %08x ===\n",
		     in_handler ? "HANDLER" : "PROCESS",
		     lregs[1] & 0xff, sregs ? sregs[7] : -1);
	for (i = 0; i < 4; i++)
		print_reg(i, sregs, i);
	for (i = 4; i < 10; i++)
		print_reg(i, lregs, i - 1);
	print_reg(10, lregs, 9);
	print_reg(11, lregs, 10);
	print_reg(12, sregs, 4);
	print_reg(13, lregs, in_handler ? 2 : 0);
	print_reg(14, sregs, 5);
	print_reg(15, sregs, 6);
}

void report_panic(void)
{
	struct panic_data *pdata = pdata_ptr;
	uint32_t sp;

	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = sizeof(*pdata);
	pdata->struct_version = 2;
	pdata->arch = PANIC_ARCH_CORTEX_M;
	pdata->flags = 0;
	pdata->reserved = 0;

	/* Choose the right sp (psp or msp) based on EXC_RETURN value */
	sp = is_frame_in_handler_stack(pdata->cm.regs[11])
		? pdata->cm.regs[2] : pdata->cm.regs[0];
	/* If stack is valid, copy exception frame to pdata */
	if ((sp & 3) == 0 &&
	    sp >= CONFIG_RAM_BASE &&
	    sp <= CONFIG_RAM_BASE + CONFIG_RAM_SIZE - 8 * sizeof(uint32_t)) {
		const uint32_t *sregs = (const uint32_t *)sp;
		int i;
		for (i = 0; i < 8; i++)
			pdata->cm.frame[i] = sregs[i];
		pdata->flags |= PANIC_DATA_FLAG_FRAME_VALID;
	}

	panic_data_print(pdata);
	panic_reboot();
}

/**
 * Default exception handler, which reports a panic.
 *
 * Declare this as a naked call so we can extract raw LR and IPSR values.
 */
void exception_panic(void) __attribute__((naked));
void exception_panic(void)
{
	/* Save registers and branch directly to panic handler */
	asm volatile(
		"mov r0, %[pregs]\n"
		"mrs r1, psp\n"
		"mrs r2, ipsr\n"
		"mov r3, sp\n"
		"stmia r0!, {r1-r7}\n"
		"mov r1, r8\n"
		"mov r2, r9\n"
		"mov r3, r10\n"
		"mov r4, r11\n"
		"mov r5, lr\n"
		"stmia r0!, {r1-r5}\n"
		"mov sp, %[pstack]\n"
		"b report_panic\n" : :
			[pregs] "r" (pdata_ptr->cm.regs),
			[pstack] "r" (pstack_addr) :
			/* Constraints protecting these from being clobbered.
			 * Gcc should be using r0 & r12 for pregs and pstack. */
			"r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9",
			"r10", "r11", "cc", "memory"
		);
}

void software_panic(uint32_t panic_reason, uint32_t panic_info)
{
	__asm__("mov " STRINGIFY(SOFTWARE_PANIC_INFO_REG) ", %0\n"
		"mov " STRINGIFY(SOFTWARE_PANIC_REASON_REG) ", %1\n"
		"bl exception_panic\n"
		: : "r"(panic_info), "r"(panic_reason));
}

void panic_log_watchdog(void)
{
	uint32_t *lregs = pdata_ptr->cm.regs;

	/* Watchdog reset, log panic cause */
	memset(pdata_ptr, 0, sizeof(*pdata_ptr));
	pdata_ptr->magic = PANIC_DATA_MAGIC;
	pdata_ptr->struct_size = sizeof(*pdata_ptr);
	pdata_ptr->struct_version = 2;
	pdata_ptr->arch = PANIC_ARCH_CORTEX_M;

	lregs[3] = PANIC_SW_WATCHDOG;
}

void bus_fault_handler(void)
{
	if (!bus_fault_ignored)
		exception_panic();
}

void ignore_bus_fault(int ignored)
{
	bus_fault_ignored = ignored;
}

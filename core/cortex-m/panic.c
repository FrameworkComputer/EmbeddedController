/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
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

/*
 * Panic data goes at the end of RAM.  This is safe because we don't context
 * switch away from the panic handler before rebooting, and stacks and data
 * start at the beginning of RAM.
 */
static struct panic_data * const pdata_ptr =
	(struct panic_data *)(CONFIG_RAM_BASE + CONFIG_RAM_SIZE
			     - sizeof(struct panic_data));

/* Preceded by stack, rounded down to nearest 64-bit-aligned boundary */
static const uint32_t pstack_addr = (CONFIG_RAM_BASE + CONFIG_RAM_SIZE
				     - sizeof(struct panic_data)) & ~7;

/**
 * Add a character directly to the UART buffer.
 *
 * @param context	Context; ignored.
 * @param c		Character to write.
 * @return 0 if the character was transmitted, 1 if it was dropped.
 */
static int panic_txchar(void *context, int c)
{
	if (c == '\n')
		panic_txchar(context, '\r');

	/* Wait for space in transmit FIFO */
	while (!uart_tx_ready())
		;

	/* Write the character directly to the transmit FIFO */
	uart_write_char(c);

	return 0;
}

void panic_puts(const char *outstr)
{
	/* Flush the output buffer */
	uart_flush_output();

	/* Put all characters in the output buffer */
	while (*outstr)
		panic_txchar(NULL, *outstr++);

	/* Flush the transmit FIFO */
	uart_tx_flush();
}

void panic_printf(const char *format, ...)
{
	va_list args;

	/* Flush the output buffer */
	uart_flush_output();

	va_start(args, format);
	vfnprintf(panic_txchar, NULL, format, args);
	va_end(args);

	/* Flush the transmit FIFO */
	uart_tx_flush();
}

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

#ifdef CONFIG_DEBUG_EXCEPTIONS
/* Names for each of the bits in the mmfs register, starting at bit 0 */
static const char * const mmfs_name[32] = {
	"Instruction access violation",
	"Data access violation",
	NULL,
	"Unstack from exception violation",
	"Stack from exception violation",
	NULL,
	NULL,
	NULL,

	"Instruction bus error",
	"Precise data bus error",
	"Imprecise data bus error",
	"Unstack from exception bus fault",
	"Stack from exception bus fault",
	NULL,
	NULL,
	NULL,

	"Undefined instructions",
	"Invalid state",
	"Invalid PC",
	"No coprocessor",
	NULL,
	NULL,
	NULL,
	NULL,

	"Unaligned",
	"Divide by 0",
	NULL,
	NULL,

	NULL,
	NULL,
	NULL,
	NULL,
};

/* Names for the first 5 bits in the DFSR */
static const char * const dfsr_name[] = {
	"Halt request",
	"Breakpoint",
	"Data watchpoint/trace",
	"Vector catch",
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
 * @param mmfs		Value of Memory Manage Fault Status
 * @param hfsr		Value of Hard Fault Status
 * @param dfsr		Value of Debug Fault Status
 */
static void show_fault(uint32_t mmfs, uint32_t hfsr, uint32_t dfsr)
{
	unsigned int upto;
	int count = 0;

	for (upto = 0; upto < 32; upto++) {
		if ((mmfs & (1 << upto)) && mmfs_name[upto]) {
			do_separate(&count);
			panic_puts(mmfs_name[upto]);
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
		if ((dfsr & (1 << upto))) {
			do_separate(&count);
			panic_puts(dfsr_name[upto]);
		}
	}
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
	frame_size += 8 * sizeof(uint32_t);

	/* CPU uses xPSR[9] to indicate whether it padded the stack for
	 * alignment or not. */
	if (pdata->frame[7] & (1 << 9))
		frame_size += sizeof(uint32_t);

#ifdef CONFIG_FPU
	/* CPU uses EXC_RETURN[4] to indicate whether it stored extended
	 * frame for FPU or not. */
	if (!(pdata->regs[11] & (1 << 4)))
		frame_size += 18 * sizeof(uint32_t);
#endif

	return frame_size;
}

/*
 * Returns the position of the process stack before the exception frame.
 * It computes the size of the exception frame and adds it to psp.
 * If the exception happened in the exception context, it returns psp as is.
 */
static uint32_t get_process_stack_position(const struct panic_data *pdata)
{
	uint32_t psp = pdata->regs[0];

	if (!is_frame_in_handler_stack(pdata->regs[11]))
		psp += get_exception_frame_size(pdata);

	return psp;
}

/*
 * Show extra information that might be useful to understand a panic()
 *
 * We show fault register information, including the fault address registers
 * if valid.
 */
static void panic_show_extra(const struct panic_data *pdata)
{
	show_fault(pdata->mmfs, pdata->hfsr, pdata->dfsr);
	if (pdata->mmfs & CPU_NVIC_MMFS_BFARVALID)
		panic_printf(", bfar = %x", pdata->bfar);
	if (pdata->mmfs & CPU_NVIC_MMFS_MFARVALID)
		panic_printf(", mfar = %x", pdata->mfar);
	panic_printf("\nmmfs = %x, ", pdata->mmfs);
	panic_printf("shcsr = %x, ", pdata->shcsr);
	panic_printf("hfsr = %x, ", pdata->hfsr);
	panic_printf("dfsr = %x\n", pdata->dfsr);
}

/*
 * Prints process stack contents stored above the exception frame.
 */
static void panic_show_process_stack(const struct panic_data *pdata)
{
	panic_printf("\n=========== Process Stack Contents ===========");
	if (pdata->flags & PANIC_DATA_FLAG_FRAME_VALID) {
		uint32_t psp = get_process_stack_position(pdata);
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

/**
 * Display a message and reboot
 */
static void panic_reboot(void)
{
	panic_puts("\n\nRebooting...\n");
	system_reset(0);
}

/*
 * Print panic data
 */
static void panic_print(const struct panic_data *pdata)
{
	const uint32_t *lregs = pdata->regs;
	const uint32_t *sregs = NULL;
	const int32_t in_handler = is_frame_in_handler_stack(pdata->regs[11]);
	int i;

	if (pdata->flags & PANIC_DATA_FLAG_FRAME_VALID)
		sregs = pdata->frame;

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

#ifdef CONFIG_DEBUG_EXCEPTIONS
	panic_show_extra(pdata);
#endif
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
	sp = is_frame_in_handler_stack(pdata->regs[11])
		? pdata->regs[2] : pdata->regs[0];
	/* If stack is valid, copy exception frame to pdata */
	if ((sp & 3) == 0 &&
	    sp >= CONFIG_RAM_BASE &&
	    sp <= CONFIG_RAM_BASE + CONFIG_RAM_SIZE - 8 * sizeof(uint32_t)) {
		const uint32_t *sregs = (const uint32_t *)sp;
		int i;
		for (i = 0; i < 8; i++)
			pdata->frame[i] = sregs[i];
		pdata->flags |= PANIC_DATA_FLAG_FRAME_VALID;
	}

	/* Save extra information */
	pdata->mmfs = CPU_NVIC_MMFS;
	pdata->bfar = CPU_NVIC_BFAR;
	pdata->mfar = CPU_NVIC_MFAR;
	pdata->shcsr = CPU_NVIC_SHCSR;
	pdata->hfsr = CPU_NVIC_HFSR;
	pdata->dfsr = CPU_NVIC_DFSR;

	panic_print(pdata);
#ifdef CONFIG_DEBUG_EXCEPTIONS
	panic_show_process_stack(pdata);
	/* TODO: Dump main stack contents as well if the exception happened
	 * in a handler's context. */
#endif
	panic_reboot();
}

/* Default exception handler, which reports a panic.
 * Naked call so we can extract raw LR and IPSR. */
void exception_panic(void) __attribute__((naked));
void exception_panic(void)
{
	/* Save registers and branch directly to panic handler */
	asm volatile(
		"mov r0, %[pregs]\n"
		"mrs r1, psp\n"
		"mrs r2, ipsr\n"
		"mov r3, sp\n"
		"stmia r0, {r1-r11, lr}\n"
		"mov sp, %[pstack]\n"
		"b report_panic\n" : :
			[pregs] "r" (pdata_ptr->regs),
			[pstack] "r" (pstack_addr) :
			/* Constraints protecting these from being clobbered.
			 * Gcc should be using r0 & r12 for pregs and pstack. */
			"r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9",
			"r10", "r11", "cc", "memory"
		);
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

#ifdef CONFIG_DEBUG_ASSERT_REBOOTS
void panic_assert_fail(const char *msg, const char *func, const char *fname,
		       int linenum)
{
	panic_printf("\nASSERTION FAILURE '%s' in %s() at %s:%d\n",
		     msg, func, fname, linenum);

	panic_reboot();
}
#endif

void panic(const char *msg)
{
	panic_printf("\n** PANIC: %s\n", msg);
	panic_reboot();
}

struct panic_data *panic_get_data(void)
{
	return pdata_ptr->magic == PANIC_DATA_MAGIC ? pdata_ptr : NULL;
}

/*****************************************************************************/
/* Console commands */

static int command_crash(int argc, char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM1;

	if (!strcasecmp(argv[1], "divzero")) {
		int a = 1, b = 0;

		cflush();
		ccprintf("%08x", a / b);
	} else if (!strcasecmp(argv[1], "unaligned")) {
		cflush();
		ccprintf("%08x", *(int *)0xcdef);
	} else {
		return EC_ERROR_PARAM1;
	}

	/* Everything crashes, so shouldn't get back here */
	return EC_ERROR_UNKNOWN;
}
DECLARE_CONSOLE_COMMAND(crash, command_crash,
			"[divzero | unaligned]",
			"Crash the system (for testing)",
			NULL);

static int command_panicinfo(int argc, char **argv)
{
	if (pdata_ptr->magic == PANIC_DATA_MAGIC) {
		ccprintf("Saved panic data:%s\n",
			 (pdata_ptr->flags & PANIC_DATA_FLAG_OLD_CONSOLE ?
			  "" : " (NEW)"));

		panic_print(pdata_ptr);

		/* Data has now been printed */
		pdata_ptr->flags |= PANIC_DATA_FLAG_OLD_CONSOLE;
	} else {
		ccprintf("No saved panic data available.\n");
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(panicinfo, command_panicinfo,
			NULL,
			"Print info from a previous panic",
			NULL);

/*****************************************************************************/
/* Host commands */

int host_command_panic_info(struct host_cmd_handler_args *args)
{
	if (pdata_ptr->magic == PANIC_DATA_MAGIC) {
		ASSERT(pdata_ptr->struct_size <= args->response_max);
		memcpy(args->response, pdata_ptr, pdata_ptr->struct_size);
		args->response_size = pdata_ptr->struct_size;

		/* Data has now been returned */
		pdata_ptr->flags |= PANIC_DATA_FLAG_OLD_HOSTCMD;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PANIC_INFO,
		     host_command_panic_info,
		     EC_VER_MASK(0));

/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdarg.h>

#include "config.h"
#include "console.h"
#include "cpu.h"
#include "panic.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "watchdog.h"


/* This is the size of our private panic stack, if we have one */
#define STACK_SIZE_WORDS	64

/* Whether bus fault is ignored */
static int bus_fault_ignored;

/* Data saved across reboots */
struct panic_data {
	uint8_t arch;             /* Architecture (PANIC_ARCH_*) */
	uint8_t struct_version;   /* Structure version (currently 1) */
	uint8_t flags;            /* Flags (PANIC_DATA_FLAG_*) */
	uint8_t reserved;         /* Reserved; set 0 */

	uint32_t regs[11];        /* psp, ipsr, lr, r4-r11 */
	uint32_t frame[8];        /* r0-r3, r12, lr, pc, xPSR */

	uint32_t mmfs;
	uint32_t bfar;
	uint32_t mfar;
	uint32_t shcsr;
	uint32_t hfsr;
	uint32_t dfsr;

	/*
	 * These fields go at the END of the struct so we can find it at the
	 * end of memory.
	 */
	uint32_t struct_size;     /* Size of this struct */
	uint32_t magic;           /* PANIC_SAVE_MAGIC if valid */
};

#define PANIC_DATA_MAGIC 0x21636e50  /* "Pnc!" */

#define PANIC_ARCH_CORTEX_M 1

/* Flags for panic_data.flags */
#define PANIC_DATA_FLAG_FRAME_VALID (1 << 0)  /* panic_data.frame is valid */

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

void panic_putc(int ch)
{
	uart_emergency_flush();
	if (ch == '\n')
		panic_putc('\r');
	uart_write_char(ch);
	uart_tx_flush();
}


void panic_puts(const char *s)
{
	while (*s)
		panic_putc(*s++);
}


void panic_vprintf(const char *format, va_list args)
{
	int pad_width;

	while (*format) {
		int c = *format++;

		/* Copy normal characters */
		if (c != '%') {
			panic_putc(c);
			continue;
		}

		/* Get first format character */
		c = *format++;

		/* Handle %c */
		if (c == 'c') {
			c = va_arg(args, int);
			panic_putc(c);
			continue;
		}

		/* Count padding length (only supported for hex) */
		pad_width = 0;
		while (c >= '0' && c <= '9') {
			pad_width = (10 * pad_width) + c - '0';
			c = *format++;
		}

		if (c == 's') {
			char *vstr;

			vstr = va_arg(args, char *);
			panic_puts(vstr ? vstr : "(null)");
		} else { /* assume 'x' */
			uint32_t v, shift;
			int i;

			v = va_arg(args, uint32_t);
			if (!pad_width)
				pad_width = 8;
			shift = pad_width * 4 - 4;
			for (i = 0; i < pad_width; i++) {
				int ch = '0' + ((v >> shift) & 0xf);

				if (ch > '9')
					ch += 'a' - '9' - 1;
				panic_putc(ch);
				shift -= 4;
			}
		}
	}
}


void panic_printf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	panic_vprintf(format, args);
	va_end(args);
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
		panic_printf("%8x", regs[index]);
	else
		panic_puts("        ");
	panic_putc((regnum & 3) == 3 ? '\n' : ' ');
}

#ifdef CONFIG_PANIC_HELP
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


/**
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
	panic_putc('\n');
	panic_printf("mmfs = %x, ", pdata->mmfs);
	panic_printf("shcsr = %x, ", pdata->shcsr);
	panic_printf("hfsr = %x, ", pdata->hfsr);
	panic_printf("dfsr = %x", pdata->dfsr);
}
#endif /* CONFIG_PANIC_HELP */


/**
 * Display a message and reboot
 */
static void panic_reboot(void)
{
	panic_puts("\n\nRebooting...\n");
	system_reset(0);
}

/**
 * Print panic data
 */
static void panic_print(const struct panic_data *pdata)
{
	const uint32_t *lregs = pdata->regs;
	const uint32_t *sregs = NULL;
	int i;

	if (pdata->flags & PANIC_DATA_FLAG_FRAME_VALID)
		sregs = pdata->frame;

	panic_printf("\n=== EXCEPTION: %2x ====== xPSR: %8x ===========\n",
		     lregs[1] & 7, sregs ? sregs[7] : -1);
	for (i = 0; i < 4; i++)
		print_reg(i, sregs, i);
	for (i = 4; i < 10; i++)
		print_reg(i, lregs, i - 1);
	print_reg(10, lregs, 9);
	print_reg(11, lregs, 10);
	print_reg(12, sregs, 4);
	print_reg(13, lregs, 0);
	print_reg(14, sregs, 5);
	print_reg(15, sregs, 6);

#ifdef CONFIG_PANIC_HELP
	panic_show_extra(pdata);
#endif
}

void report_panic(void)
{
	struct panic_data *pdata = pdata_ptr;
	const uint32_t psp = pdata->regs[0];

	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = sizeof(*pdata);
	pdata->struct_version = 1;
	pdata->arch = PANIC_ARCH_CORTEX_M;
	pdata->flags = 0;

	/* If stack is valid, save exception frame */
	if (psp >= CONFIG_RAM_BASE &&
	    psp <= CONFIG_RAM_BASE + CONFIG_RAM_SIZE + 8 * sizeof(uint32_t)) {
		const uint32_t *sregs = (const uint32_t *)psp;
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
	panic_reboot();
}

/* Default exception handler, which reports a panic */
void exception_panic(void) __attribute__((naked));
void exception_panic(void)
{
	/* Naked call so we can extract raw LR and IPSR */

#ifdef CONFIG_PANIC_NEW_STACK
	asm volatile(
		/*
		 * This instruction will generate ldr rx, [pc, #offset]
		 * followed by a mov sp, rx.  See below for more explanation.
		 */
		"mov sp, %[pstack]\n" : :
			[pstack] "r" (pstack_addr)
		);
#endif

	asm volatile(
		/*
		 * This instruction will generate ldr rx, [pc, #offset]
		 * followed by a mov r0, rx. It would clearly be better if
		 * we could get ldr r0, [pc, #offset] but that doesn't seem
		 * to be supported. Nor does gcc seem to define which
		 * temporary register it uses. Therefore we put this
		 * instruction first so that it matters less.
		 *
		 * If you see a failure in the panic handler, please check
		 * the final assembler output here.
		 */
		"mov r0, %[pregs]\n"
		"mrs r1, psp\n"
		"mrs r2, ipsr\n"
		"mov r3, lr\n"
		"stmia r0, {r1-r11}\n"
		"b report_panic" : :
			[pregs] "r" (pdata_ptr->regs)
		);
}


void bus_fault_handler(void) __attribute__((naked));
void bus_fault_handler(void)
{
	if (!bus_fault_ignored)
		exception_panic();
}


void ignore_bus_fault(int ignored)
{
	bus_fault_ignored = ignored;
}


#ifdef CONFIG_ASSERT_HELP
void panic_assert_fail(const char *msg, const char *func, const char *fname,
		       int linenum)
{
	panic_printf("\nASSERTION FAILURE '%s' in %s() at %s:%d\n", msg, func,
		     fname, linenum);

	panic_reboot();
}
#endif


void panic(const char *msg)
{
	panic_printf("\n** PANIC: %s\n", msg);
	panic_reboot();
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

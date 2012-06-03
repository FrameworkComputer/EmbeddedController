/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdarg.h>

#include "config.h"
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

/* We save registers here for display by report_panic() */
static struct save_area
{
#ifdef CONFIG_PANIC_NEW_STACK
	uint32_t stack[STACK_SIZE_WORDS];
#endif
	uint32_t saved_regs[11];	/* psp, ipsr, lr, r4-r11 */
} save_area __attribute__((aligned(8)));


void panic_putc(int ch)
{
	uart_emergency_flush();
	if (ch == '\n')
		panic_putc('\r');
	uart_write_char(ch);
	while (uart_tx_ready())
		;
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
static void print_reg(int regnum, uint32_t *regs, int index)
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


/**
 * Display a message and reboot
 */
static void panic_reboot(void)
{
	panic_puts("\n\nRebooting...\n");
	system_reset(0);
}


void report_panic(const char *msg, uint32_t *lregs)
{
	if (msg) {
		panic_printf("\n** PANIC: %s\n", msg);
	} else if (lregs) {
		uint32_t *sregs = NULL;
		uint32_t psp;
		int i;

		psp = lregs[0];
		if (psp >= CONFIG_RAM_BASE
				&& psp < CONFIG_RAM_BASE + CONFIG_RAM_SIZE)
			sregs = (uint32_t *)psp;
		panic_printf("\n=== EXCEPTION: %2x ====== xPSR: %8x "
			"===========\n", lregs[1] & 7, sregs ? sregs[7] : -1);
		for (i = 0; i < 4; i++)
			print_reg(i, sregs, i);
		for (i = 4; i < 10; i++)
			print_reg(i, lregs, i - 1);
		print_reg(10, lregs, 9);
		print_reg(11, lregs, 10);
		print_reg(12, sregs, 4);
		print_reg(13, &psp, 0);
		print_reg(14, sregs, 5);
		print_reg(15, sregs, 6);
	}

	panic_reboot();
}

/* Default exception handler, which reports a panic */
void exception_panic(void) __attribute__((naked));
void exception_panic(void)
{
	/* Naked call so we can extract raw LR and IPSR */
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
		"mov r0, %[save_area]\n"
		"mrs r1, psp\n"
		"mrs r2, ipsr\n"
		"mov r3, lr\n"
		"stmia r0, {r1-r11}\n"
#ifdef CONFIG_PANIC_NEW_STACK
		"mov sp, r0\n"
#endif
		"mov r1, r0\n"
		"mov r0, #0\n"
		"b report_panic" : :
			[save_area] "r" (save_area.saved_regs)
		);
}

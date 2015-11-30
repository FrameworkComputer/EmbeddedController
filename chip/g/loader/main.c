/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "debug_printf.h"
#include "printf.h"
#include "registers.h"
#include "setup.h"
#include "system.h"
#include "trng.h"
#include "uart.h"

/*
 * This file is a proof of concept stub which will be extended and split into
 * appropriate pieces sortly, when full blown support for cr50 bootrom is
 * introduced.
 */
uint32_t sleep_mask;

timestamp_t get_time(void)
{
	timestamp_t ret;

	ret.val = 0;

	return ret;
}

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
	/* Put all characters in the output buffer */
	while (*outstr)
		panic_txchar(NULL, *outstr++);
}

void panic_printf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfnprintf(panic_txchar, NULL, format, args);
	va_end(args);
}

int main(void)
{
	init_trng();
	uart_init();
	debug_printf("\n\n%s bootloader, %8u_%u@%u, %sUSB, %s crypto\n",
		     STRINGIFY(BOARD), GREG32(SWDP, BUILD_DATE),
		     GREG32(SWDP, BUILD_TIME), GREG32(SWDP, P4_LAST_SYNC),
		     (GREG32(SWDP, FPGA_CONFIG) &
		      GC_CONST_SWDP_FPGA_CONFIG_USB_8X8CRYPTO) ? "" : "no ",
		     (GREG32(SWDP, FPGA_CONFIG) &
		      GC_CONST_SWDP_FPGA_CONFIG_NOUSB_CRYPTO) ? "full" : "8x8");
	unlockFlashForRW();

	/* Trying RW A only for now */
	tryLaunch(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RW_MEM_OFF,
		  CONFIG_FLASH_SIZE/2 - CONFIG_RW_MEM_OFF);
	debug_printf("No valid image found, not sure what to do...\n");
	halt();
	return 1;
}

void panic_reboot(void)
{
	panic_puts("\n\nRebooting...\n");
	system_reset(0);
}

void interrupt_disable(void)
{
	asm("cpsid i");
}

static int printchar(void *context, int c)
{
	if (c == '\n')
		uart_write_char('\r');
	uart_write_char(c);

	return 0;
}

void debug_printf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfnprintf(printchar, NULL, format, args);
	va_end(args);
}

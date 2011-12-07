/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include <stdint.h>

#include "board.h"
#include "clock.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "registers.h"
#include "util.h"

/**
 * Idle task
 * executed when no task are ready to be scheduled
 */
void __idle(void)
{
	while (1) {
		/* wait for the irq event */
		asm("wfi");
		/* TODO more power management here */
	}
}

/* simple busy waiting before clocks are initialized */
static void wait_cycles(uint32_t cycles)
{
	asm("1: subs %0, #1\n"
	    "   bne 1b\n" :: "r"(cycles));
}

/**
 * Function to measure baseline for power consumption.
 *
 * Levels :
 *   0 : CPU running in tight loop
 *   1 : CPU running in tight loop but peripherals gated
 *   2 : CPU in sleep mode
 *   3 : CPU in sleep mode and peripherals gated
 *   4 : CPU in deep sleep mode
 *   5 : CPU in deep sleep mode and peripherals gated
 */
static int command_sleep(int argc, char **argv)
{
	int level = 0;
	int clock = 0;
	uint32_t uartibrd = 0;
	uint32_t uartfbrd = 0;

	if (argc >= 2) {
		level = strtoi(argv[1], NULL, 10);
	}
	if (argc >= 3) {
		clock = strtoi(argv[2], NULL, 10);
	}
	/* remove LED current sink  */
	gpio_set(EC_GPIO_DEBUG_LED, 0);

	uart_printf("Going to sleep : level %d clock %d...\n", level, clock);
	uart_flush_output();

	/* clock setting */
	if (clock) {
		/* Use ROM code function to set the clock */
		void **func_table = (void **)*(uint32_t *)0x01000044;
		void (*rom_clock_set)(uint32_t rcc) = func_table[23];

		/* disable interrupts */
		asm volatile("cpsid i");

		switch (clock) {
		case 1: /* 16MHz IOSC */
			uartibrd = 17;
			uartfbrd = 23;
			rom_clock_set(0x00000d51);
			break;
		case 2: /* 1MHz IOSC */
			uartibrd = 1;
			uartfbrd = 5;
			rom_clock_set(0x07C00d51);
			break;
		case 3: /* 30 kHz */
			uartibrd = 0;
			uartfbrd = 0;
			rom_clock_set(0x00000d71);
			break;
		}

		/* TODO: move this to the UART module; ugly to have
		   UARTisms here.  Also note this only fixes UART0,
		   not UART1. */
		if (uartfbrd) {
			/* Disable the port via UARTCTL and add HSE */
			LM4_UART_CTL(0) = 0x0320;
			/* Set the baud rate divisor */
			LM4_UART_IBRD(0) = uartibrd;
			LM4_UART_FBRD(0) = uartfbrd;
			/* Poke UARTLCRH to make the new divisor take effect. */
			LM4_UART_LCRH(0) = LM4_UART_LCRH(0);
			/* Enable the port */
			LM4_UART_CTL(0) |= 0x0001;
		}
		asm volatile("cpsie i");
	}

	if (uartfbrd) {
		uart_printf("We are still alive. RCC=%08x\n", LM4_SYSTEM_RCC);
		uart_flush_output();
	}

	asm volatile("cpsid i");

	/* gate peripheral clocks */
	if (level & 1) {
		LM4_SYSTEM_RCGCTIMER = 0;
		LM4_SYSTEM_RCGCGPIO = 0;
		LM4_SYSTEM_RCGCDMA = 0;
		LM4_SYSTEM_RCGCUART = 0;
		LM4_SYSTEM_RCGCLPC = 0;
		LM4_SYSTEM_RCGCWTIMER = 0;
	}
	/* set deep sleep bit */
	if (level >= 4)
		LM4_SCB_SYSCTRL |= 0x4;
	/* go to low power mode (forever ...) */
	if (level > 1)
		while (1)
			asm("wfi");
	else
		while(1);

	return EC_SUCCESS;
}


static const struct console_command clock_commands[] = {
	{"sleep", command_sleep}
};
static const struct console_group clock_group = {
	"Clock", clock_commands, ARRAY_SIZE(clock_commands)
};

static void clock_init_pll(uint32_t value)
{
	/**
	 * at startup, OSCSRC is PIOSC (precision internal oscillator)
	 * PLL and PLL2 are in power-down
	 */

	/* PLL already setup */
	if (LM4_SYSTEM_PLLSTAT & 1)
		return;

	/* Put a bypass on the system clock PLLs, no divider */
	LM4_SYSTEM_RCC = (LM4_SYSTEM_RCC | 0x800) & ~0x400000;
	LM4_SYSTEM_RCC2 = (LM4_SYSTEM_RCC2 | 0x800) & ~0x80000000;
	/* Enable main and precision internal oscillators */
	LM4_SYSTEM_RCC &= ~0x3;
	/* wait 1 million CPU cycles */
	wait_cycles(512 * 1024);

	/* clear PLL lock flag (aka PLLLMIS) */
	LM4_SYSTEM_MISC = 0x40;
	/* clear powerdown / set XTAL frequency and divider */
	LM4_SYSTEM_RCC = (LM4_SYSTEM_RCC & ~0x07c027c0) | (value & 0x07c007c0);
	/* wait 32 CPU cycles */
	wait_cycles(16);
	/* wait for PLL to lock */
	while (!(LM4_SYSTEM_RIS & 0x40));

	/* Remove bypass on PLL and set oscillator source to main */
	LM4_SYSTEM_RCC = LM4_SYSTEM_RCC & ~0x830;
}

int clock_init(void)
{
	/* Use 66.667Mhz clock from PLL */
	BUILD_ASSERT(CPU_CLOCK == 66666667);
	/* CPU clock = PLL/3 ; System clock = PLL
	 * Osc source = main OSC ; external crystal = 16 Mhz
	 */
	clock_init_pll(0x01400540);

#ifdef CONFIG_DEBUG
	/* Register our internal commands */
	console_register_commands(&clock_group);
#endif

	return EC_SUCCESS;
}

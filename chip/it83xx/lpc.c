/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC module for Chrome EC */

#include "acpi.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_protocol.h"
#include "lpc.h"
#include "port80.h"
#include "pwm.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "irq_chip.h"

static uint8_t acpi_ec_memmap[EC_MEMMAP_SIZE] __aligned(4);

uint8_t *lpc_get_memmap_range(void)
{
	return (uint8_t *)acpi_ec_memmap;
}

int lpc_keyboard_has_char(void)
{
	/* OBE or OBF */
	return IT83XX_KBC_KBHISR & 0x01;
}

int lpc_keyboard_input_pending(void)
{
	/* IBE or IBF */
	return IT83XX_KBC_KBHISR & 0x02;
}

void lpc_keyboard_put_char(uint8_t chr, int send_irq)
{
	/* Clear programming data bit 7-4 */
	IT83XX_KBC_KBHISR &= 0x0F;

	/* keyboard */
	IT83XX_KBC_KBHISR |= 0x10;

	/*
	 * bit0 = 0, The IRQ1 is controlled by the IRQ1B bit in KBIRQR.
	 * bit1 = 0, The IRQ12 is controlled by the IRQ12B bit in KBIRQR.
	 */
	IT83XX_KBC_KBHICR &= 0xFC;

	/*
	 * Enable the interrupt to keyboard driver in the host processor
	 * via SERIRQ when the output buffer is full.
	 */
	if (send_irq)
		IT83XX_KBC_KBHICR |= 0x01;

	udelay(16);

	/* The data output to the KBC Data Output Register. */
	IT83XX_KBC_KBHIKDOR = chr;
}

void lpc_keyboard_clear_buffer(void)
{
	/* --- (not implemented yet) --- */
}

void lpc_keyboard_resume_irq(void)
{
	if (lpc_keyboard_has_char()) {
		/* The IRQ1 is controlled by the IRQ1B bit in KBIRQR. */
		IT83XX_KBC_KBHICR &= ~0x01;

		/*
		 * When the OBFKIE bit in KBC Host Interface Control Register
		 * (KBHICR) is 0, the bit directly controls the IRQ1 signal.
		 */
		IT83XX_KBC_KBIRQR |= 0x01;

		task_clear_pending_irq(IT83XX_IRQ_KBC_OUT);

		task_enable_irq(IT83XX_IRQ_KBC_OUT);
	}
}

void lpc_set_host_event_state(uint32_t mask)
{
	/* --- (not implemented yet) --- */
}

int lpc_query_host_event_state(void)
{
	/* --- (not implemented yet) --- */
	return -1;
}

void lpc_set_host_event_mask(enum lpc_host_event_type type, uint32_t mask)
{
	/* --- (not implemented yet) --- */
}

uint32_t lpc_get_host_event_mask(enum lpc_host_event_type type)
{
	/* --- (not implemented yet) --- */
	return 0;
}

int lpc_get_pltrst_asserted(void)
{
	return !gpio_get_level(GPIO_PCH_PLTRST_L);
}

/* KBC and PMC control modules */
void lpc_kbc_ibf_interrupt(void)
{
	if (lpc_keyboard_input_pending()) {
		keyboard_host_write(IT83XX_KBC_KBHIDIR,
			(IT83XX_KBC_KBHISR & 0x08) ? 1 : 0);
	}

	task_clear_pending_irq(IT83XX_IRQ_KBC_IN);
}

void lpc_kbc_obe_interrupt(void)
{
	task_disable_irq(IT83XX_IRQ_KBC_OUT);

	task_clear_pending_irq(IT83XX_IRQ_KBC_OUT);

	if (!(IT83XX_KBC_KBHICR & 0x01)) {
		IT83XX_KBC_KBIRQR &= ~0x01;

		IT83XX_KBC_KBHICR |= 0x01;
	}
}

static void lpc_init(void)
{
	IT83XX_GPIO_GCR = 0x06;

	/* The register pair to access PNPCFG is 002Eh and 002Fh */
	IT83XX_GCTRL_BADRSEL = 0x00;

	/* Disable KBC IRQ */
	IT83XX_KBC_KBIRQR = 0x00;

	/*
	 * bit2, Output Buffer Empty CPU Interrupt Enable.
	 * bit3, Input Buffer Full CPU Interrupt Enable.
	 */
	IT83XX_KBC_KBHICR |= 0x0C;

	/* Input Buffer Full Interrupt Enable */
	IT83XX_PMC_PM1CTL |= 0x01;

	memset(lpc_get_memmap_range(), 0, EC_MEMMAP_SIZE);

	task_clear_pending_irq(IT83XX_IRQ_KBC_OUT);

	task_disable_irq(IT83XX_IRQ_KBC_OUT);

	task_clear_pending_irq(IT83XX_IRQ_KBC_IN);

	task_enable_irq(IT83XX_IRQ_KBC_IN);
}
/*
 * Set prio to higher than default; this way LPC memory mapped data is ready
 * before other inits try to initialize their memmap data.
 */
DECLARE_HOOK(HOOK_INIT, lpc_init, HOOK_PRIO_INIT_LPC);

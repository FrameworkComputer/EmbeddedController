/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros. */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)

static int freq;

struct clock_gate_ctrl {
	volatile uint8_t *reg;
	uint8_t mask;
};

void clock_init(void)
{
#if PLL_CLOCK == 48000000
	/* Set PLL frequency to 48MHz. */
	IT83XX_ECPM_PLLFREQR = 0x04;
	freq = PLL_CLOCK;
#else
#error "Support only for PLL clock speed of 48MHz."
#endif

	/* Set EC Clock Frequency to PLL frequency. */
	IT83XX_ECPM_SCDCR3 &= 0xf0;

	/*
	 * The VCC power status is treated as power-on.
	 * The VCC supply of LPC and related functions (EC2I,
	 * KBC, SWUC, PMC, CIR, SSPI, UART, BRAM, and PECI).
	 * It means VCC (pin 11) should be logic high before using
	 * these functions, or firmware treats VCC logic high
	 * as following setting.
	 */
	IT83XX_GCTRL_RSTS = (IT83XX_GCTRL_RSTS & 0x3F) + 0x40;

	/* Turn off auto clock gating. */
	IT83XX_ECPM_AUTOCG = 0x00;
}

int clock_get_freq(void)
{
	return freq;
}

/**
 * Enable clock to specified peripheral
 *
 * @param offset Should be element of clock_gate_offsets enum.
 *               Bits 8-15 specify the ECPM offset of the specific clock reg.
 *               Bits 0-7 specify the mask for the clock register.
 * @param mask   Unused
 * @param mode   Unused
 */
void clock_enable_peripheral(uint32_t offset, uint32_t mask, uint32_t mode)
{
	volatile uint8_t *reg = (volatile uint8_t *)
			(IT83XX_ECPM_BASE + (offset >> 8));
	uint8_t reg_mask = offset & 0xff;

	/*
	 * Note: CGCTRL3R, bit 6, must always write 1, but since there is no
	 * offset argument that addresses this bit, then we are guaranteed
	 * that this line will write a 1 to that bit.
	 */
	*reg &= ~reg_mask;
}

/**
 * Disable clock to specified peripheral
 *
 * @param offset Should be element of clock_gate_offsets enum.
 *               Bits 8-15 specify the ECPM offset of the specific clock reg.
 *               Bits 0-7 specify the mask for the clock register.
 * @param mask   Unused
 * @param mode   Unused
 */
void clock_disable_peripheral(uint32_t offset, uint32_t mask, uint32_t mode)
{
	volatile uint8_t *reg = (volatile uint8_t *)
			(IT83XX_ECPM_BASE + (offset >> 8));
	uint8_t reg_mask = offset & 0xff;
	uint8_t tmp_mask = 0;

	/* CGCTRL3R, bit 6, must always write a 1. */
	tmp_mask |= ((offset >> 8) == IT83XX_ECPM_CGCTRL3R_OFF) ? 0x40 : 0x00;

	*reg |= reg_mask | tmp_mask;
}

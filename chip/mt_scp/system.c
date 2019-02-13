/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System : hardware specific implementation */

#include "console.h"
#include "cpu.h"
#include "flash.h"
#include "hooks.h"
#include "host_command.h"
#include "memmap.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "version.h"
#include "watchdog.h"

/*
 * SCP_GPR[0] b15-b0  - scratchpad
 * SCP_GPR[0] b31-b16 - saved_flags
 */

int system_set_scratchpad(uint32_t value)
{
	/* Check if value fits in 16 bits */
	if (value & 0xffff0000)
		return EC_ERROR_INVAL;

	SCP_GPR[0] = (SCP_GPR[0] & 0xffff0000) | value;

	return EC_SUCCESS;
}

uint32_t system_get_scratchpad(void)
{
	return SCP_GPR[0] & 0xffff;
}

const char *system_get_chip_vendor(void)
{
	return "mtk";
}

const char *system_get_chip_name(void)
{
	/* Support only SCP_A for now */
	return "scp_a";
}

const char *system_get_chip_revision(void)
{
	return "";
}

void chip_pre_init(void)
{
}

static void scp_enable_tcm(void)
{
	/* Enable L1 cache and tightly coupled memory (TCM) */
	SCP_CLK_L1_SRAM_PD = 0;
	SCP_CLK_TCM_TAIL_SRAM_PD = 0;
	/* SCP CM4 mod */
	CM4_MODIFICATION = 3;
	CM4_DCM_FEATURE = 3;
}

static void scp_enable_pirq(void)
{
	/* Enable all peripheral to SCP IRQ, except IPC0. */
	SCP_INTC_IRQ_ENABLE = 0xFFFFFFFE;
	SCP_INTC_IRQ_ENABLE_MSB = 0xFFFFFFFF;
}

/* TODO(b/120176040): move to clock.c */
static void scp_ulposc_config(int osc)
{
	/* TODO(b/120176040): add ULPOSC calibration */
	const struct {
		uint8_t div;
		uint8_t cali;
	} ulposc_config[] = {
		{ .div = 12, .cali = 32},
		{ .div = 16, .cali = 32},
	};
	const int osc_index = osc - 1;
	uint32_t val;

	if (osc != 1 || osc != 2)
		return;

	/* Clear all bits */
	val = 0;
	/* Enable CP */
	val |= OSC_CP_EN;
	/* Set div */
	val |= ulposc_config[osc_index].div << 17;
	/* F-band = 0, I-band = 4 */
	val |= 4 << 6;
	/* Set calibration */
	val |= ulposc_config[osc_index].cali;
	/* Set control register 1 */
	AP_ULPOSC_CON02(osc) = val;
	/* Set control register 2, enable div2 */
	AP_ULPOSC_CON13(osc) |= OSC_DIV2_EN;
}

/*
 * TODO(b/120176040): move to clock.c and separate into
 * scp_set_clock_high_enable and _disable functions.
 */
void scp_set_clock_high(int osc, int on)
{
	if (on) {
		switch (osc) {
		case 1:
			/* Enable ULPOSC */
			SCP_CLK_EN |= EN_CLK_HIGH;
			/* TODO: Turn on clock gate after 25ms */
			SCP_CLK_EN |= CG_CLK_HIGH;
			break;
		case 2:
			/* Enable ULPOSC1 & ULPOSC2 */
			SCP_CLK_EN |= EN_CLK_HIGH;
			SCP_CLK_ON_CTRL &= ~HIGH_CORE_DIS_SUB;
			/* TODO: Turn on clock gate after 25ms */
			SCP_CLK_HIGH_CORE |= 1;
			break;
		default:
			break;
		}
	} else {
		switch (osc) {
		case 1:
			/* Disable clock gate */
			SCP_CLK_EN &= CG_CLK_HIGH;
			/* TODO: Turn off ULPOSC1 after 50us */
			SCP_CLK_EN &= EN_CLK_HIGH;
			break;
		case 2:
			SCP_CLK_HIGH_CORE &= ~1;
			/* TODO: Turn off ULPOSC1 after 50us */
			SCP_CLK_ON_CTRL |= HIGH_CORE_DIS_SUB;
			break;
		default:
			break;
		}
	}
	/* TODO: Wait 25us */
}

/* TODO(b/120176040): move to clock.c */
static void scp_enable_clock(void)
{
	/* VREQ */
	SCP_CPU_VREQ = 0x10001;
	SCP_SECURE_CTRL &= ~ENABLE_SPM_MASK_VREQ;

	/* DDREN auto mode */
	SCP_SYS_CTRL |= AUTO_DDREN;

	/* Set settle time */
	SCP_CLK_SYS_VAL = 1;  /* System clock */
	SCP_CLK_HIGH_VAL = 1; /* ULPOSC */
	SCP_CLK_SLEEP_CTRL = (SCP_CLK_SLEEP_CTRL & ~VREQ_COUNTER_MASK) | 2;

	/* Disable slow wake */
	SCP_CLK_SLEEP = SLOW_WAKE_DISABLE;
	/* Disable SPM sleep control, disable sleep mode */
	SCP_CLK_SLEEP_CTRL &= ~(SPM_SLEEP_MODE | EN_SLEEP_CTRL);

	/* Turn off ULPOSC2 */
	SCP_CLK_ON_CTRL |= HIGH_CORE_DIS_SUB;
	scp_ulposc_config(1);
	scp_set_clock_high(1, 1); /* Turn on ULPOSC1 */
	scp_ulposc_config(2);
	scp_set_clock_high(2, 1); /* Turn on ULPOSC2 */

	/* Enable default clock gate */
	SCP_CLK_GATE |= CG_DMA_CH3 | CG_DMA_CH2 | CG_DMA_CH1 | CG_DMA_CH0 |
			CG_I2C_M | CG_MAD_M;
}

void system_pre_init(void)
{
	/* SRAM */
	scp_enable_tcm();
	/* Clock */
	scp_enable_clock();
	/* Peripheral IRQ */
	scp_enable_pirq();
	/* Init dram mapping (and cache) */
	scp_memmap_init();
}

void system_reset(int flags)
{
	uint32_t save_flags = 0;

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		save_flags = system_get_reset_flags() | RESET_FLAG_PRESERVED;

	if (flags & SYSTEM_RESET_LEAVE_AP_OFF)
		save_flags |= RESET_FLAG_AP_OFF;

	/* Remember that the software asked us to hard reboot */
	if (flags & SYSTEM_RESET_HARD)
		save_flags |= RESET_FLAG_HARD;

	/* Reset flags are 32-bits, but save only 16 bits. */
	ASSERT(!(save_flags >> 16));
	SCP_GPR[0] = (save_flags << 16) | (SCP_GPR[0] & 0xffff);

	/* SCP can not hard reset itself */
	ASSERT(!(flags & SYSTEM_RESET_HARD));

	if (flags & SYSTEM_RESET_WAIT_EXT) {
		int i;

		/* Wait 10 seconds for external reset */
		for (i = 0; i < 1000; i++) {
			watchdog_reload();
			udelay(10000);
		}
	}

	/* Set watchdog timer to small value, and spin wait for watchdog reset */
	SCP_WDT_CFG = 0;
	SCP_WDT_CFG = SCP_WDT_ENABLE | SCP_WDT_PERIOD(1);
	watchdog_reload();
	while (1)
		;
}

static void check_reset_cause(void)
{
	uint32_t flags = 0;
	uint32_t raw_reset_cause = SCP_GPR[1];

	/* Set state to power-on */
	SCP_PWRON_STATE = PWRON_DEFAULT;

	if ((raw_reset_cause & 0xffff0000) == PWRON_DEFAULT) {
		/* Reboot */
		if (raw_reset_cause & PWRON_WATCHDOG)
			flags |= RESET_FLAG_WATCHDOG;
		else if (raw_reset_cause & PWRON_RESET)
			flags |= RESET_FLAG_POWER_ON;
		else
			flags |= RESET_FLAG_OTHER;
	} else {
		/* Power lost restart */
		flags |= RESET_FLAG_POWER_ON;
	}
	system_set_reset_flags(SCP_GPR[0] >> 16);
	SCP_GPR[0] &= 0xffff;
}

int system_is_reboot_warm(void)
{
	const uint32_t cold_flags =
		RESET_FLAG_RESET_PIN |
		RESET_FLAG_POWER_ON  |
		RESET_FLAG_WATCHDOG  |
		RESET_FLAG_HARD      |
		RESET_FLAG_SOFT      |
		RESET_FLAG_HIBERNATE;

	check_reset_cause();

	return !(system_get_reset_flags() & cold_flags);
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	return EC_ERROR_INVAL;
}

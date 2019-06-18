/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "pmu.h"
#include "task.h"

/*
 * RC Trim constants
 */
#define RCTRIM_RESOLUTION       (12)
#define RCTRIM_LOAD_VAL	        BIT(11)
#define RCTRIM_RANGE_MAX	(7 * 7)
#define RCTRIM_RANGE_MIN	(-8 * 7)
#define RCTRIM_RANGE		(RCTRIM_RANGE_MAX - RCTRIM_RANGE_MIN + 1)

/*
 * Enable peripheral clock
 * @param perih Peripheral from @ref uint32_t
 */
void pmu_clock_en(uint32_t periph)
{
	if (periph <= 31)
		GR_PMU_PERICLKSET0 = BIT(periph);
	else
		GR_PMU_PERICLKSET1 = (1 << (periph - 32));
}

/*
 * Disable peripheral clock
 * @param perih Peripheral from @ref uint32_t
 */
void pmu_clock_dis(uint32_t periph)
{
	if (periph <= 31)
		GR_PMU_PERICLKCLR0 = BIT(periph);
	else
		GR_PMU_PERICLKCLR1 = (1 << (periph - 32));
}

/*
 * Peripheral reset
 * @param periph Peripheral from @ref uint32_t
 */
void pmu_peripheral_rst(uint32_t periph)
{
	/* Reset high */
	if (periph <= 31)
		GR_PMU_RST0 = 1 << periph;
	else
		GR_PMU_RST1 = 1 << (periph - 32);
}


/*
 * enable clock doubler for USB purposes
 */
void pmu_enable_clock_doubler(void)
{
}
/*
 * Switch system clock to XO
 * @returns The value of XO_OSC_XTL_FSM_STATUS.	0 = okay, 1 = error.
 */
uint32_t pmu_clock_switch_xo(void)
{
	return 0;
}

/*
 * Enter sleep mode and handle exiting from sleep mode
 * @warning The CPU must be in RC no trim mode before calling this function
 */
void pmu_sleep(void)
{
}

/*
 * Exit hibernate mode
 * This function should be called after a powerdown exit event.
 * It handles turning the power domains back on.
 * Clocks will be left in RC no trim.
 */
void pmu_hibernate_exit(void)
{
}

/*
 * Enter powerdown mode
 * This function does not return. The powerdown exit event will
 * cause the CPU to begin executing the system / app bootloader.
 * @warning The CPU must be in RC no trim mode
 */
void pmu_powerdown(void)
{
}

/*
 * Exit powerdown mode
 * This function should be called after a powerdown exit event.
 * It handles turning the power domains back on.
 * Clocks will be left in RC no trim.
 */
void pmu_powerdown_exit(void)
{
}

/**
 * Handle PMU interrupt
 */
void pmu_interrupt(void)
{
	/* TBD */
}
/* DECLARE_IRQ(GC_IRQNUM_PMU_PMUINT, pmu_interrupt, 1); */

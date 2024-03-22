/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_POWER_MGT_H
#define __CROS_EC_POWER_MGT_H

#include "common.h"
#include "registers.h"

extern void uart_port_restore(void);
extern void uart_to_idle(void);
extern void clear_fabric_error(void);
extern void i2c_port_restore(void);
extern void lapic_restore(void);

#define FABRIC_IDLE_COUNT 50
#define TRUNK_CLKGATE_COUNT 0xf

/* power states for ISH */
enum ish_pm_state {
	/* D0 state: active mode */
	ISH_PM_STATE_D0 = 0,
	/* sleep state: cpu halt */
	ISH_PM_STATE_D0I0,
	/* deep sleep state 1: Trunk Clock Gating(TCG), cpu halt*/
	ISH_PM_STATE_D0I1,
	/* deep sleep state 2: TCG, SRAM retention, cpu halt */
	ISH_PM_STATE_D0I2,
	/* deep sleep state 3: TCG, SRAM power off, cpu halt*/
	ISH_PM_STATE_D0I3,
	/**
	 * D3 state: power off state, on ISH5.0, can't do real power off,
	 * similar to D0I3, but will reset ISH
	 */
	ISH_PM_STATE_D3,
	/**
	 * reset ISH, main FW received 'reboot' command
	 */
	ISH_PM_STATE_RESET,
	/**
	 * reset ISH, main FW received reset_prep interrupt during
	 * S0->Sx transition.
	 */
	ISH_PM_STATE_RESET_PREP,
	ISH_PM_STATE_NUM
};

/* halt ISH minute-ia cpu core */
static inline void ish_mia_halt(void)
{
	/* make sure interrupts are enabled before halting */
	__asm__ volatile("sti;\n"
			 "hlt;");
}

/* reset ISH mintue-ia cpu core  */
__noreturn static inline void ish_mia_reset(void)
{
	/**
	 * ISH HW looks at the rising edge of this bit to
	 * trigger a MIA reset.
	 */
	ISH_RST_REG = 0;
	ISH_RST_REG = 1;

	__builtin_unreachable();
}

/* Initialize power management module. */
#ifdef CONFIG_LOW_POWER_IDLE
void ish_pm_init(void);
#else
__maybe_unused static void ish_pm_init(void)
{
}
#endif

/**
 * reset ISH (reset minute-ia cpu core, and power off main SRAM)
 */
__noreturn void ish_pm_reset(enum ish_pm_state pm_state);

/**
 * notify the power management module that the UART for the console is in use.
 */
void ish_pm_refresh_console_in_use(void);

#endif /* __CROS_EC_POWER_MGT_H */

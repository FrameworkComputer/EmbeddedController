/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_POWER_MGT_H
#define __CROS_EC_POWER_MGT_H

/* power states for ISH */
enum {
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
	/* D3 state: power off state, on ISH5.0, can't do real power off,
	 * similar to D0I3, but will reset ISH
	 */
	ISH_PM_STATE_D3,
	/* ISH received reset_prep interrupt during S0->Sx transition */
	ISH_PM_STATE_RESET_PREP,
	ISH_PM_STATE_NUM
};

/* halt ISH cpu */
static inline void ish_halt(void)
{
	/* make sure interrupts are enabled before halting */
	__asm__ volatile("sti;\n"
			 "hlt;");
}

/* ish low power management initialization,
 * should be called at system init stage before RTOS task scheduling start
 */
void ish_pm_init(void);

#endif /* __CROS_EC_POWER_MGT_H */

/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "hooks.h"
#include "registers.h"
#include "timer.h"

#define POWER_BUTTON 2

int rbox_powerbtn_is_pressed(void)
{
	return !GREAD_FIELD(RBOX, CHECK_OUTPUT, PWRB_OUT);
}

static void rbox_release_ec_reset(void)
{
	/* Unfreeze the PINMUX */
	GREG32(PINMUX, HOLD) = 0;

	/* Allow some time for outputs to stabilize. */
	usleep(500);

	/* Let the EC go (the RO bootloader asserts it ASAP after POR) */
	GREG32(RBOX, ASSERT_EC_RST) = 0;

}
DECLARE_HOOK(HOOK_INIT, rbox_release_ec_reset, HOOK_PRIO_LAST);

static void rbox_init(void)
{
	/* Enable RBOX */
	clock_enable_module(MODULE_RBOX, 1);

	/* Clear any interrupt bits (write 1's to clear) */
	GREG32(RBOX, INT_STATE) = 0xffffffff;

	/* Clear any wakeup bits (write 0x2, then 0x0) */
	GREG32(RBOX, WAKEUP) = GC_RBOX_WAKEUP_CLEAR_MASK;
	GREG32(RBOX, WAKEUP) = 0;

	/* Override rbox fuses and setup correct behavior */
	GWRITE(RBOX, DEBUG_CLK10HZ_COUNT, 0x63ff);
	GWRITE(RBOX, DEBUG_SHORT_DELAY_COUNT, 0x4ff);
	GWRITE(RBOX, DEBUG_LONG_DELAY_COUNT, 0x31);
	GWRITE(RBOX, DEBUG_DEBOUNCE, 0x4);
	GWRITE(RBOX, DEBUG_KEY_COMBO0, 0xC0);
	GWRITE(RBOX, DEBUG_KEY_COMBO1, 0x0);
	GWRITE(RBOX, DEBUG_KEY_COMBO2, 0x0);
	/* DEBUG_BLOCK_OUTPUT value should be 0x7 */
	GWRITE(RBOX, DEBUG_BLOCK_OUTPUT,
	       GC_RBOX_DEBUG_BLOCK_OUTPUT_KEY0_SEL_MASK |
	       GC_RBOX_DEBUG_BLOCK_OUTPUT_KEY1_SEL_MASK |
	       GC_RBOX_DEBUG_BLOCK_OUTPUT_KEY0_VAL_MASK);
	/* DEBUG_POL value should be 0x21 */
	GWRITE(RBOX, DEBUG_POL,
	       0x1 << GC_RBOX_DEBUG_POL_AC_PRESENT_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_PWRB_IN_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_PWRB_OUT_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_KEY0_IN_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_KEY0_OUT_LSB |
	       0x1 << GC_RBOX_DEBUG_POL_KEY1_IN_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_KEY1_OUT_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_EC_RST_LSB |
	       0x0 << GC_RBOX_DEBUG_POL_BATT_DISABLE_LSB);
	/* DEBUG_TERM value should be 0x1204 */
	GWRITE(RBOX, DEBUG_TERM,
	       0x0 << GC_RBOX_DEBUG_TERM_AC_PRESENT_LSB |
	       0x1 << GC_RBOX_DEBUG_TERM_ENTERING_RW_LSB |
	       0x0 << GC_RBOX_DEBUG_TERM_PWRB_IN_LSB |
	       0x0 << GC_RBOX_DEBUG_TERM_PWRB_OUT_LSB |
	       0x2 << GC_RBOX_DEBUG_TERM_KEY0_IN_LSB |
	       0x0 << GC_RBOX_DEBUG_TERM_KEY0_OUT_LSB |
	       0x1 << GC_RBOX_DEBUG_TERM_KEY1_IN_LSB |
	       0x0 << GC_RBOX_DEBUG_TERM_KEY1_OUT_LSB);
	/* DEBUG_DRIVE value should be 0x157 */
	GWRITE(RBOX, DEBUG_DRIVE,
	       0x3 << GC_RBOX_DEBUG_DRIVE_PWRB_OUT_LSB |
	       0x1 << GC_RBOX_DEBUG_DRIVE_KEY0_OUT_LSB |
	       0x1 << GC_RBOX_DEBUG_DRIVE_KEY1_OUT_LSB |
	       0x1 << GC_RBOX_DEBUG_DRIVE_EC_RST_LSB |
	       0x1 << GC_RBOX_DEBUG_DRIVE_BATT_DISABLE_LSB);
	/* FUSE_CTRL value should be 0x3 */
	GWRITE(RBOX, FUSE_CTRL,
	       GC_RBOX_FUSE_CTRL_OVERRIDE_FUSE_MASK |
	       GC_RBOX_FUSE_CTRL_OVERRIDE_FUSE_READY_MASK);
}
DECLARE_HOOK(HOOK_INIT, rbox_init, HOOK_PRIO_DEFAULT - 1);

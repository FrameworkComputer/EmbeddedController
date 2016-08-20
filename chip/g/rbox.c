/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "hooks.h"
#include "registers.h"

static void rbox_release_ec_reset(void)
{
	/* Let the EC go (the RO bootloader asserts it ASAP after POR) */
	GREG32(RBOX, ASSERT_EC_RST) = 0;

	/* And unfreeze the PINMUX */
	GREG32(PINMUX, HOLD) = 0;
}
DECLARE_HOOK(HOOK_INIT, rbox_release_ec_reset, HOOK_PRIO_LAST);

static void rbox_init(void)
{
	/* Enable RBOX */
	clock_enable_module(MODULE_RBOX, 1);

	/* Clear existing interrupts */
	GWRITE(RBOX, WAKEUP_CLEAR, 1);
	GWRITE(RBOX, WAKEUP_CLEAR, 0);
	GWRITE(RBOX, INT_STATE, 1);
}
DECLARE_HOOK(HOOK_INIT, rbox_init, HOOK_PRIO_DEFAULT - 1);

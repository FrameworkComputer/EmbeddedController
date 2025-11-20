/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lid_angle.h"
#include "tablet_mode.h"

__overridable void lid_angle_peripheral_enable(int enable)
{
	int chipset_in_s0 = chipset_in_state(CHIPSET_STATE_ON);

	/*
	 * If the lid is in tabletmode and is suspended, ignore the lid
	 * angle, which might be faulty, then disable keyboard. This
	 * could be a scenario where convertibles with lid open are in
	 * tabletmode and system is suspended.
	 */
	if (IS_ENABLED(CONFIG_TABLET_MODE) && tablet_get_mode()) {
		enable = 0;
	}

	if (enable) {
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
	} else {
		/*
		 * Ensure that the chipset is off before disabling the keyboard.
		 * When the chipset is on, the EC keeps the keyboard enabled and
		 * the AP decides whether to ignore input devices or not.
		 */
		if (!chipset_in_s0) {
			keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
		}
	}
}

#ifdef CONFIG_PLATFORM_EC_DSP_REMOTE_LID_ANGLE
static void reset_tablet_mode(void)
{
	/*
	 * When the lid angle is calculated remotely we may have stale data for
	 * the lid angle if the user went from tablet -> clamshell or clamshell
	 * -> tablet while the DSP was off. Since the DSP will boot and assume
	 * clamshell mode until the lid angle is calculated, we should reset the
	 * state too.
	 */
	tablet_set_mode(0, TABLET_TRIGGER_LID | TABLET_TRIGGER_OVERRIDE_GMR);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, reset_tablet_mode, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESET, reset_tablet_mode, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_PLATFORM_EC_DSP_REMOTE_LID_ANGLE */

static void enable_peripherals(void)
{
	/*
	 * Make sure lid angle is not disabling peripherals when AP is running.
	 */
	lid_angle_peripheral_enable(1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, enable_peripherals, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_TABLET_MODE
static void suspend_peripherals(void)
{
	/*
	 * Make sure peripherals are disabled in S3 in tablet mode.
	 */
	if (tablet_get_mode())
		lid_angle_peripheral_enable(0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, suspend_peripherals, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_TABLET_MODE */

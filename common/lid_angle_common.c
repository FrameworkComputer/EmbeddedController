/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
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

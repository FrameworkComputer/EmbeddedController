/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "power.h"
#include "util.h"

static bool count_debounce = false;
static int boot_fail_count = 0;

/* Called by hook task every 200 ms */
static void control_workaround_tick(void)
{
	if (chipset_in_state(CHIPSET_STATE_SOFT_OFF) && !count_debounce) {
		/* System want to boot up */
		count_debounce = true;
		ccprintf("Boot debounce");
	} else if (chipset_in_state(CHIPSET_STATE_HARD_OFF) && count_debounce) {
		/* System boot up fail, back to G3 */
		count_debounce = false;
		boot_fail_count++;
		ccprintf("Boot fail %d", boot_fail_count);
	} else if (chipset_in_state(CHIPSET_STATE_ON)) {
		/* System can boot to OS */
		count_debounce = false;
		boot_fail_count = 0;
	}

	if (boot_fail_count > 3) {
		/* System cannot boot up to OS over 3 times */
		ccprintf("Boot fail trigger hibernate");
		system_enter_hibernate(0, 0);
	}
}
DECLARE_HOOK(HOOK_TICK, control_workaround_tick, HOOK_PRIO_DEFAULT);

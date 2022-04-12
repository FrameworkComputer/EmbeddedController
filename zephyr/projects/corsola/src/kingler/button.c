/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* kingler button */

#include "button.h"
#include "cros_board_info.h"
#include "gpio.h"
#include "hooks.h"

static void buttons_hook(void)
{
	int version;

	if (cbi_get_board_version(&version)) {
		return;
	}

	/* b:219891339: drop this workaround when we deprecate rev0 */
	if (version == 0) {
		/* swap VOLUP/VOLDN */
		button_reassign_gpio(BUTTON_VOLUME_DOWN, GPIO_VOLUME_UP_L);
		button_reassign_gpio(BUTTON_VOLUME_UP, GPIO_VOLUME_DOWN_L);
		/*
		 * button_reassign_gpio will disable the old button interrupt
		 * and then enable the new button interrupt which cause the
		 * GPIO_VOLUME_UP_L interrupt disabled after we reassign
		 * BUTTON_VOLUME_UP, so we need to re-enable it here.
		 */
		gpio_enable_interrupt(GPIO_VOLUME_UP_L);
	}
}
DECLARE_HOOK(HOOK_INIT, buttons_hook, HOOK_PRIO_DEFAULT);

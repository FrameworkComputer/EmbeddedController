/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "charge_state.h"

#include <zephyr/drivers/led.h>

__override int board_led_alt_policy(void)
{
	if (charge_get_status()->batt.flags & BATT_FLAG_BAD_ANY)
		return 1;
	else
		return 0;
}

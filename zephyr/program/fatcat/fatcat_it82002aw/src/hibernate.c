/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"

__override void board_hibernate_late(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_slp_z_r), 1);

	/*
	 * Ensure the GPIO is asserted long enough to discharge the
	 * the PP3300_Z1 regulator.
	 */
	k_busy_wait(10 * 1000); /* 10,000 us */
}

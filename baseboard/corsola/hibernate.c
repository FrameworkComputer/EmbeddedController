/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "driver/charger/isl923x_public.h"
#include "gpio.h"
#include "system.h"

/* Hayato board specific hibernate implementation */
__override void board_hibernate_late(void)
{
	/*
	 * Turn off PP5000_A. Required for devices without Z-state.
	 * Don't care for devices with Z-state.
	 */
	gpio_set_level(GPIO_EN_PP5000_A, 0);

	/*
	 * GPIO_EN_SLP_Z not implemented in rev0/1,
	 * fallback to usual hibernate process.
	 */
	if (board_get_version() <= 1) {
		if (IS_ENABLED(BOARD_ASURADA) ||
			(IS_ENABLED(CONFIG_ZEPHYR) &&
			IS_ENABLED(CONFIG_BOARD_ASURADA)))
			return;
	}

	isl9238c_hibernate(CHARGER_SOLO);

	gpio_set_level(GPIO_EN_SLP_Z, 1);

	/* should not reach here */
	__builtin_unreachable();
}

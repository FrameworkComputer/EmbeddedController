/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "driver/charger/isl923x_public.h"
#include "gpio.h"
#include "system.h"

/* Corsola board specific hibernate implementation */
__override void board_hibernate_late(void)
{
	if (IS_ENABLED(CONFIG_CHARGER_ISL9238C))
		isl9238c_hibernate(CHARGER_SOLO);

	gpio_set_level(GPIO_EN_ULP, 1);

	/* should not reach here */
	__builtin_unreachable();
}

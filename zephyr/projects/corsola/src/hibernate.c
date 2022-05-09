/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio.h>

#include "charger.h"
#include "driver/charger/isl923x_public.h"
#include "system.h"

/* Corsola board specific hibernate implementation */
__override void board_hibernate(void)
{
#ifdef CONFIG_CHARGER_ISL9238C
	isl9238c_hibernate(CHARGER_SOLO);
#endif
}

__override void board_hibernate_late(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ulp), 1);
}

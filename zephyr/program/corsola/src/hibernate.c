/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "charger.h"
#include "driver/charger/isl923x_public.h"
#include "system.h"

#include <zephyr/drivers/gpio.h>

/* Corsola board specific hibernate implementation */
__override void board_hibernate(void)
{
#ifdef CONFIG_CHARGER_ISL9238C
	isl9238c_hibernate(CHARGER_SOLO);
#endif
}

__override void board_hibernate_late(void)
{
#ifdef CONFIG_CORSOLA_HIBERNATE_PRE_OFF_5V
	/* b/283037861: Pre-off the 5V power line for hibernate. */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(en_pp5000_z2), 1);
	/* It takes around 30ms to release the PP5000 capacitance. */
	k_busy_wait(30 * USEC_PER_MSEC);
#endif
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ulp), 1);
}

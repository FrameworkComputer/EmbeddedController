/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "system.h"

#include <zephyr/drivers/gpio.h>

/* Geralt board specific hibernate implementation */
__override void board_hibernate_late(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(en_pp5000_z1_l), 1);
	/* It takes around 30ms to release the PP5000 capacitance. */
	k_busy_wait(30 * USEC_PER_MSEC);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(en_ulp), 1);
}

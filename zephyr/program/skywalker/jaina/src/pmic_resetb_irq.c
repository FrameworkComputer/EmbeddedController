/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "power.h"

#include <zephyr/drivers/gpio.h>

void pmic_ec_resetb_interrupt_jaina(enum gpio_signal signal)
{
	power_signal_interrupt(signal);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s3),
			gpio_get_level(signal));
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tch_pwr_en),
			gpio_get_level(signal));
}

/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Calypso passthru-helper functions */

#include "chipset.h"
#include "common.h"
#include "gpio.h"
#include "power/qcom.h"

void passthru_lid_open_to_pmic(void)
{
	gpio_pin_set_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_lid_open_od),
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_lid_open_r)));
}

void passthru_ac_on_to_pmic(void)
{
	gpio_pin_set_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ec_sysok_gate),
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_acok_r_od)));
}

void reset_all_passthru_pmic_signal(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_sysok_gate), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_lid_open_od), 0);
}

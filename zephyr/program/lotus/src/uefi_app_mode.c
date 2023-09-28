/*
 * Copyright 2023 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

#include "ec_commands.h"
#include "uefi_app_mode.h"
#include "gpio.h"
#include "gpio/gpio_int.h"

static uint8_t uefi_app_enable;

void uefi_app_mode_setting(uint8_t enable)
{
	uefi_app_enable = enable;

	/* Ignored the power button signal if we are in UEFI app mode */
	/* TODO: Maybe add a hook when OS boots to enable the interrupt again, */
	/* if the app forgets to do it. */
	if (uefi_app_enable) {
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_powerbtn));
	} else {
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_powerbtn));
	}
}

uint8_t uefi_app_btn_status(void)
{
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_on_off_btn_l)) == 1;
}

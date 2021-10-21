/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "power/qcom.h"
#include "system.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

void board_set_switchcap_power(int enable)
{
	gpio_set_level(GPIO_SWITCHCAP_ON, enable);
}

int board_is_switchcap_enabled(void)
{
	return gpio_get_level(GPIO_SWITCHCAP_ON);
}

int board_is_switchcap_power_good(void)
{
	return gpio_get_level(GPIO_DA9313_GPIO0);
}

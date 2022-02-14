/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "power/qcom.h"

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
	return gpio_get_level(GPIO_SWITCHCAP_PG);
}

/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trogdor baseboard-specific configuration */

#include "i2c.h"

/* Wake-up pins for hibernate */
enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);
BUILD_ASSERT(ARRAY_SIZE(hibernate_wake_pins) >= 3);

int board_allow_i2c_passthru(int port)
{
	return (port == I2C_PORT_VIRTUAL_BATTERY ||
		port == I2C_PORT_TCPC0 ||
		port == I2C_PORT_TCPC1);
}

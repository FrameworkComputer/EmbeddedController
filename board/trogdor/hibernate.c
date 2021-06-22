/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"

void board_hibernate(void)
{
	/*
	 * Sensors are unpowered in hibernate. Apply PD to the
	 * interrupt lines such that they don't float.
	 */
	gpio_set_flags(GPIO_ACCEL_GYRO_INT_L,
		       GPIO_INPUT | GPIO_PULL_DOWN);
	gpio_set_flags(GPIO_LID_ACCEL_INT_L,
		       GPIO_INPUT | GPIO_PULL_DOWN);
}

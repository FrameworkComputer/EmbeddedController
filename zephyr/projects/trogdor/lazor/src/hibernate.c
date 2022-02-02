/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "sku.h"
#include "system.h"
#include "usbc_ppc.h"

void board_hibernate(void)
{
	int i;

	if (!board_is_clamshell()) {
		/*
		 * Sensors are unpowered in hibernate. Apply PD to the
		 * interrupt lines such that they don't float.
		 */
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(
				      gpio_accel_gyro_int_l),
				      GPIO_DISCONNECTED);
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(
				      gpio_lid_accel_int_l),
				      GPIO_DISCONNECTED);
	}

	/*
	 * Board rev 5+ has the hardware fix. Don't need the following
	 * workaround.
	 */
	if (system_get_board_version() >= 5)
		return;

	/*
	 * Enable the PPC power sink path before EC enters hibernate;
	 * otherwise, ACOK won't go High and can't wake EC up. Check the
	 * bug b/170324206 for details.
	 */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		ppc_vbus_sink_enable(i, 1);
}

void board_hibernate_late(void)
{
	/* Set the hibernate GPIO to turn off the rails */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hibernate_l), 0);
}

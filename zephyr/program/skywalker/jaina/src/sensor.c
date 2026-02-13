/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "cros_cbi.h"
#include "driver/accel_bma422.h"
#include "driver/accelgyro_bmi260.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

static void disable_base_lid_irq(void)
{
	uint32_t val;

	cros_cbi_get_fw_config(FORM_FACTOR, &val);
	if (val == CLAMSHELL) {
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_imu_int_ec_l),
				      GPIO_INPUT | GPIO_PULL_UP);
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_lid_accel));
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(gpio_lid_accel_int_ec_l),
			GPIO_INPUT | GPIO_PULL_UP);
	}
}
DECLARE_HOOK(HOOK_INIT, disable_base_lid_irq, HOOK_PRIO_POST_DEFAULT);

static void board_sensor_init(void)
{
	uint32_t val;

	cros_cbi_get_fw_config(FORM_FACTOR, &val);
	if (val == CLAMSHELL) {
		ccprints("Board is Clamshell");
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
	} else if (val == CONVERTIBLE) {
		ccprints("Board is Convertible");
	}
}
DECLARE_HOOK(HOOK_INIT, board_sensor_init, HOOK_PRIO_DEFAULT);

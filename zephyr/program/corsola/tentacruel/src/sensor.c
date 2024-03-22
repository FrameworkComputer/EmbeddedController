/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "cros_cbi.h"
#include "driver/accel_bma422.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_bmi323.h"
#include "driver/accelgyro_icm42607.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

test_export_static bool base_is_none;
test_export_static bool lid_is_none;

void base_sensor_interrupt(enum gpio_signal signal)
{
	uint32_t val;

	cros_cbi_get_fw_config(FORM_FACTOR, &val);
	if (val == CONVERTIBLE) {
		cros_cbi_get_fw_config(BASE_SENSOR, &val);
		if (val == BASE_BMI323) {
			bmi3xx_interrupt(signal);
		}
		/* The convertible device gyro sensor default is icm42607 */
		else {
			icm42607_interrupt(signal);
		}
	} else {
		base_is_none = true;
	}
}

void lid_sensor_interrupt(enum gpio_signal signal)
{
	uint32_t val;

	cros_cbi_get_fw_config(FORM_FACTOR, &val);
	if (val == CONVERTIBLE) {
		cros_cbi_get_fw_config(LID_SENSOR, &val);
		if (val == LID_BMA422) {
			bma4xx_interrupt(signal);
		}
		/* The convertible device lid sensor default is lis2dw12 */
		else {
			lis2dw12_interrupt(signal);
		}
	} else {
		lid_is_none = true;
	}
}

static void disable_base_lid_irq(void)
{
	if (base_is_none && lid_is_none) {
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_base_imu));
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(base_imu_int_l),
				      GPIO_INPUT | GPIO_PULL_UP);
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(lid_accel_int_l),
				      GPIO_INPUT | GPIO_PULL_UP);
	}
}
DECLARE_HOOK(HOOK_INIT, disable_base_lid_irq, HOOK_PRIO_POST_DEFAULT);

static void board_sensor_init(void)
{
	uint32_t val;

	cros_cbi_get_fw_config(FORM_FACTOR, &val);
	if (val == CLAMSHELL) {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		ccprints("Board is Clamshell");
	} else if (val == CONVERTIBLE) {
		ccprints("Board is Convertible");

		cros_cbi_get_fw_config(BASE_SENSOR, &val);
		if (val == BASE_ICM42607) {
			ccprints("Base sensor is ICM42607");
		} else if (val == BASE_BMI323) {
			MOTIONSENSE_ENABLE_ALTERNATE(alt_base_accel);
			MOTIONSENSE_ENABLE_ALTERNATE(alt_base_gyro);
			ccprints("Base sensor is BMI323");
		}

		cros_cbi_get_fw_config(LID_SENSOR, &val);
		if (val == LID_LIS2DWLTR) {
			ccprints("Lid sensnor is LIS2DWLTR");
		} else if (val == LID_BMA422) {
			MOTIONSENSE_ENABLE_ALTERNATE(alt_lid_accel);
			ccprints("Lid sensnor is BMA422");
		}
	}
}
DECLARE_HOOK(HOOK_INIT, board_sensor_init, HOOK_PRIO_DEFAULT);

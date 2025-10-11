/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "ap_power/ap_power.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "cros_cbi.h"
#include "driver/accel_bma4xx.h"
#include "driver/accel_lis2dw12_public.h"
#include "driver/accelgyro_bmi3xx.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_sensor, LOG_LEVEL_INF);

static bool lid_uses_bma422;
static bool base_uses_bmi323;

void lid_accel_interrupt(enum gpio_signal signal)
{
	if (lid_uses_bma422) {
		bma4xx_interrupt(signal);
	} else {
		lis2dw12_interrupt(signal);
	}
}

void base_accel_gyro_interrupt(enum gpio_signal signal)
{
	if (base_uses_bmi323) {
		bmi3xx_interrupt(signal);
	} else {
		lsm6dsm_interrupt(signal);
	}
}

static void alt_sensor_init(void)
{
	lid_uses_bma422 = cros_cbi_ssfc_check_match(
		CBI_SSFC_VALUE_ID(DT_NODELABEL(lid_sensor_0)));

	base_uses_bmi323 = cros_cbi_ssfc_check_match(
		CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_0)));

	motion_sensors_check_ssfc();
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C);

static bool board_is_clamshell;

static void sense_startup_hook(struct ap_power_ev_callback *cb,
			       struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_STARTUP:
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_lid_accel));
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
		break;
	case AP_POWER_SHUTDOWN:
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_lid_accel));
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
		break;
	default:
		return;
	}
}

static void board_setup_init(void)
{
	int ret;
	uint32_t val;
	static struct ap_power_ev_callback cb;

	ret = cros_cbi_get_fw_config(FORM_FACTOR, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FORM_FACTOR);
		return;
	}
	if (val == CLAMSHELL) {
		board_is_clamshell = true;
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
	}

	ap_power_ev_init_callback(&cb, sense_startup_hook,
				  AP_POWER_STARTUP | AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&cb);

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_lid_accel));
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
	}
}
DECLARE_HOOK(HOOK_INIT, board_setup_init, HOOK_PRIO_PRE_DEFAULT);

static void disable_base_imu_irq(void)
{
	if (board_is_clamshell) {
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_lid_accel));
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(gpio_lid_accel_int_ec_l),
			GPIO_INPUT | GPIO_PULL_DOWN);

		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_imu_int_ec_l),
				      GPIO_INPUT | GPIO_PULL_DOWN);
	}
}
DECLARE_HOOK(HOOK_INIT, disable_base_imu_irq, HOOK_PRIO_POST_DEFAULT);

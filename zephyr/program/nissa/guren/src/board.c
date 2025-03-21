/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Board re-init for Glassway board.
 * Guren has convertible and clamshell config,
 * and shares the same firmware.
 * So some functions should be disabled for clamshell.
 */

#include "accelgyro.h"
#include "battery.h"
#include "common.h"
#include "cros_cbi.h"
#include "driver/accel_bma4xx.h"
#include "driver/accelgyro_bmi323.h"
#include "driver/accelgyro_icm42607.h"
#include "gpio/gpio_int.h"
#include "guren_sub_board.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_ERR);

test_export_static void board_setup_init(void)
{
	int ret;
	uint32_t val;
	ret = cros_cbi_get_fw_config(FORM_FACTOR, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FORM_FACTOR);
		return;
	}
	if (val == CLAMSHELL) {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_imu_int_l),
				      GPIO_INPUT | GPIO_PULL_UP);
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_acc_int_l),
				      GPIO_INPUT | GPIO_PULL_UP);
	}
}
DECLARE_HOOK(HOOK_INIT, board_setup_init, HOOK_PRIO_PRE_DEFAULT);

static bool base_use_alt_sensor;

void motion_interrupt(enum gpio_signal signal)
{
	if (base_use_alt_sensor) {
		icm42607_interrupt(signal);

	} else {
		bmi3xx_interrupt(signal);
	}
}

test_export_static void alt_sensor_init(void)
{
	base_use_alt_sensor = cros_cbi_ssfc_check_match(
		CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_icm42607)));

	motion_sensors_check_ssfc();
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C);

void pen_detect_interrupt(enum gpio_signal s)
{
	int pen_detect =
		!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pen_detect_odl));
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen_x),
			pen_detect);
}

__override void board_power_change(struct ap_power_ev_callback *cb,
				   struct ap_power_ev_data data)
{
	int state;
	uint32_t val;

	const struct gpio_dt_spec *const pen_power_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen_x);
	const struct gpio_dt_spec *const pen_detect_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_pen_detect_odl);
	const struct gpio_int_config *const pen_detect_int =
		GPIO_INT_FROM_NODELABEL(int_pen_det_l);
	const struct gpio_dt_spec *const hdmi_hpd_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_ec_soc_hdmi_hpd);

	cros_cbi_get_fw_config(FW_SUB_BOARD, &val);

	switch (data.event) {
	case AP_POWER_STARTUP:
		/* Enable Pen Detect interrupt */
		gpio_enable_dt_interrupt(pen_detect_int);
		/*
		 * Make sure pen detection is triggered or not when AP power on
		 */
		if (!gpio_pin_get_dt(pen_detect_gpio))
			gpio_pin_set_dt(pen_power_gpio, 1);

		if ((val == GUREN_SB_HDMI_LTE) || (val == GUREN_SB_HDMI) ||
		    (val == GUREN_SB_HDMI_1A)) {
			state = gpio_pin_get_dt(
				GPIO_DT_FROM_ALIAS(gpio_hpd_odl));
			gpio_pin_set_dt(hdmi_hpd_gpio, state);
		}
		break;
	case AP_POWER_SHUTDOWN:
		/*
		 * Disable pen detect INT and turn off pen power when AP
		 * shutdown
		 */
		gpio_disable_dt_interrupt(pen_detect_int);
		gpio_pin_set_dt(pen_power_gpio, 0);

		if ((val == GUREN_SB_HDMI_LTE) || (val == GUREN_SB_HDMI) ||
		    (val == GUREN_SB_HDMI_1A)) {
			gpio_pin_set_dt(hdmi_hpd_gpio, 0);
		}
		break;
	default:
		break;
	}
}

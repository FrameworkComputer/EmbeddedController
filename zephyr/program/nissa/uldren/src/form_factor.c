/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "button.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accel_bma4xx.h"
#include "driver/accelgyro_bmi323.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

#include <dt-bindings/buttons.h>

static void form_factor_init(void)
{
	int ret;
	uint32_t val;

	/*check tabletmode*/
	ret = cros_cbi_get_fw_config(FW_TABLET, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_TABLET);
		return;
	}
	if (val == FW_TABLET_NOT_PRESENT) {
		LOG_INF("Clamshell: disable motionsense function.");
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_imu_int_l),
				      GPIO_DISCONNECTED);

		LOG_INF("Clamshell: disable volume button function.");
		button_disable_gpio(BUTTON_VOLUME_UP);
		button_disable_gpio(BUTTON_VOLUME_DOWN);
	} else {
		LOG_INF("Tablet: Enable motionsense function.");
	}
}
DECLARE_HOOK(HOOK_INIT, form_factor_init, HOOK_PRIO_POST_I2C);

static void touchpad_enable_switch(void)
{
	if (lid_is_open() && (chipset_in_state(CHIPSET_STATE_ON) ||
			      chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)))
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchpad_lid_close),
				1);
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchpad_lid_close),
				0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, touchpad_enable_switch, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, touchpad_enable_switch, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_LID_CHANGE, touchpad_enable_switch, HOOK_PRIO_DEFAULT);

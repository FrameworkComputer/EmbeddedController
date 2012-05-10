/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM host commands for Chrome EC */

#include "host_command.h"
#include "pwm.h"
#include "thermal.h"


int pwm_command_get_fan_rpm(uint8_t *data, int *resp_size)
{
	struct ec_response_pwm_get_fan_rpm *r =
			(struct ec_response_pwm_get_fan_rpm *)data;

	r->rpm = pwm_get_fan_target_rpm();
	*resp_size = sizeof(struct ec_response_pwm_get_fan_rpm);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_FAN_RPM, pwm_command_get_fan_rpm);


int pwm_command_set_fan_target_rpm(uint8_t *data, int *resp_size)
{
	struct ec_params_pwm_set_fan_target_rpm *p =
			(struct ec_params_pwm_set_fan_target_rpm *)data;

#ifdef CONFIG_TASK_THERMAL
	thermal_toggle_auto_fan_ctrl(0);
#endif
	pwm_set_fan_target_rpm(p->rpm);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_FAN_TARGET_RPM,
		     pwm_command_set_fan_target_rpm);


int pwm_command_get_keyboard_backlight(uint8_t *data, int *resp_size)
{
	struct ec_response_pwm_get_keyboard_backlight *r =
			(struct ec_response_pwm_get_keyboard_backlight *)data;

	r->percent = pwm_get_keyboard_backlight();
	*resp_size = sizeof(struct ec_response_pwm_get_keyboard_backlight);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_KEYBOARD_BACKLIGHT,
		     pwm_command_get_keyboard_backlight);


int pwm_command_set_keyboard_backlight(uint8_t *data, int *resp_size)
{
	struct ec_params_pwm_set_keyboard_backlight *p =
			(struct ec_params_pwm_set_keyboard_backlight *)data;

	pwm_set_keyboard_backlight(p->percent);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT,
		     pwm_command_set_keyboard_backlight);

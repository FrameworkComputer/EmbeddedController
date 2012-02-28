/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM host commands for Chrome EC */

#include "host_command.h"
#include "pwm.h"


enum lpc_status pwm_command_get_fan_rpm(uint8_t *data)
{
	struct lpc_response_pwm_get_fan_rpm *r =
			(struct lpc_response_pwm_get_fan_rpm *)data;

	r->rpm = pwm_get_fan_target_rpm();
	return EC_LPC_STATUS_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_PWM_GET_FAN_RPM, pwm_command_get_fan_rpm);


enum lpc_status pwm_command_set_fan_target_rpm(uint8_t *data)
{
	struct lpc_params_pwm_set_fan_target_rpm *p =
			(struct lpc_params_pwm_set_fan_target_rpm *)data;

	pwm_set_fan_target_rpm(p->rpm);
	return EC_LPC_STATUS_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_PWM_SET_FAN_TARGET_RPM,
		     pwm_command_set_fan_target_rpm);


enum lpc_status pwm_command_get_keyboard_backlight(uint8_t *data)
{
	struct lpc_response_pwm_get_keyboard_backlight *r =
			(struct lpc_response_pwm_get_keyboard_backlight *)data;

	r->percent = pwm_get_keyboard_backlight();
	return EC_LPC_STATUS_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_PWM_GET_KEYBOARD_BACKLIGHT,
		     pwm_command_get_keyboard_backlight);


enum lpc_status pwm_command_set_keyboard_backlight(uint8_t *data)
{
	struct lpc_params_pwm_set_keyboard_backlight *p =
			(struct lpc_params_pwm_set_keyboard_backlight *)data;

	pwm_set_keyboard_backlight(p->percent);
	return EC_LPC_STATUS_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_PWM_SET_KEYBOARD_BACKLIGHT,
		     pwm_command_set_keyboard_backlight);

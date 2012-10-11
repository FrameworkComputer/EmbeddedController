/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM host commands for Chrome EC */

#include "host_command.h"
#include "pwm.h"
#include "thermal.h"


int pwm_command_get_fan_target_rpm(struct host_cmd_handler_args *args)
{
	struct ec_response_pwm_get_fan_rpm *r = args->response;

	r->rpm = pwm_get_fan_target_rpm();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_FAN_TARGET_RPM,
		     pwm_command_get_fan_target_rpm,
		     EC_VER_MASK(0));

int pwm_command_set_fan_target_rpm(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_fan_target_rpm *p = args->params;

#ifdef CONFIG_TASK_THERMAL
	thermal_control_fan(0);
#endif
	pwm_set_rpm_mode(1);
	pwm_set_fan_target_rpm(p->rpm);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_FAN_TARGET_RPM,
		     pwm_command_set_fan_target_rpm,
		     EC_VER_MASK(0));

int pwm_command_fan_duty(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_fan_duty *p = args->params;
	pwm_set_fan_duty(p->percent);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_FAN_DUTY,
		     pwm_command_fan_duty,
		     EC_VER_MASK(0));

int pwm_command_get_keyboard_backlight(struct host_cmd_handler_args *args)
{
	struct ec_response_pwm_get_keyboard_backlight *r = args->response;

	r->percent = pwm_get_keyboard_backlight();
	r->enabled = pwm_get_keyboard_backlight_enabled();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_KEYBOARD_BACKLIGHT,
		     pwm_command_get_keyboard_backlight,
		     EC_VER_MASK(0));

int pwm_command_set_keyboard_backlight(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_keyboard_backlight *p = args->params;

	pwm_set_keyboard_backlight(p->percent);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT,
		     pwm_command_set_keyboard_backlight,
		     EC_VER_MASK(0));

/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM module for Chrome EC */

#include "pwm.h"
#include "pwm_commands.h"
#include "lpc_commands.h"


/*****************************************************************************/
/* Host commands */

enum lpc_status pwm_command_get_fan_rpm(uint8_t *data)
{
	struct lpc_response_pwm_get_fan_rpm *r =
			(struct lpc_response_pwm_get_fan_rpm *)data;

	r->rpm = pwm_get_fan_rpm();
	return EC_LPC_STATUS_SUCCESS;
}

enum lpc_status pwm_command_set_fan_target_rpm(uint8_t *data)
{
	struct lpc_params_pwm_set_fan_target_rpm *p =
			(struct lpc_params_pwm_set_fan_target_rpm *)data;

	pwm_set_fan_target_rpm(p->rpm);
	return EC_LPC_STATUS_SUCCESS;
}

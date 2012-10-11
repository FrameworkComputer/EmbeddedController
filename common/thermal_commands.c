/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Thermal engine host commands for Chrome EC */

#include "common.h"
#include "host_command.h"
#include "thermal.h"

int thermal_command_set_threshold(struct host_cmd_handler_args *args)
{
	const struct ec_params_thermal_set_threshold *p = args->params;

	if (thermal_set_threshold(p->sensor_type, p->threshold_id, p->value))
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_SET_THRESHOLD,
		     thermal_command_set_threshold,
		     EC_VER_MASK(0));

int thermal_command_get_threshold(struct host_cmd_handler_args *args)
{
	const struct ec_params_thermal_get_threshold *p = args->params;
	struct ec_response_thermal_get_threshold *r = args->response;
	int value = thermal_get_threshold(p->sensor_type, p->threshold_id);

	if (value == -1)
		return EC_RES_ERROR;
	r->value = value;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_GET_THRESHOLD,
		     thermal_command_get_threshold,
		     EC_VER_MASK(0));

int thermal_command_auto_fan_ctrl(struct host_cmd_handler_args *args)
{
	if (thermal_control_fan(1))
		return EC_RES_ERROR;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_AUTO_FAN_CTRL,
		     thermal_command_auto_fan_ctrl,
		     EC_VER_MASK(0));


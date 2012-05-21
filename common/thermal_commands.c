/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Thermal engine host commands for Chrome EC */

#include "host_command.h"
#include "thermal.h"


int thermal_command_set_threshold(uint8_t *data, int *resp_size)
{
	struct ec_params_thermal_set_threshold *p =
			(struct ec_params_thermal_set_threshold *)data;

	if (thermal_set_threshold(p->sensor_type, p->threshold_id, p->value))
		return EC_RES_ERROR;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_SET_THRESHOLD,
		thermal_command_set_threshold);


int thermal_command_get_threshold(uint8_t *data, int *resp_size)
{
	struct ec_params_thermal_get_threshold *p =
			(struct ec_params_thermal_get_threshold *)data;
	struct ec_response_thermal_get_threshold *r =
			(struct ec_response_thermal_get_threshold *)data;
	int value = thermal_get_threshold(p->sensor_type, p->threshold_id);

	if (value == -1)
		return EC_RES_ERROR;
	r->value = value;

	*resp_size = sizeof(struct ec_response_thermal_get_threshold);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_GET_THRESHOLD,
		thermal_command_get_threshold);


int thermal_command_auto_fan_ctrl(uint8_t *data, int *resp_size)
{
	if (thermal_toggle_auto_fan_ctrl(1))
		return EC_RES_ERROR;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_AUTO_FAN_CTRL,
		thermal_command_auto_fan_ctrl);

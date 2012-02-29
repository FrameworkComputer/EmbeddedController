/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Thermal engine host commands for Chrome EC */

#include "thermal.h"
#include "host_command.h"


/*****************************************************************************/
/* Host commands */

enum lpc_status thermal_command_set_threshold(uint8_t *data)
{
	struct lpc_params_thermal_set_threshold *p =
			(struct lpc_params_thermal_set_threshold *)data;

	if (thermal_set_threshold(p->sensor_id, p->threshold_id, p->value))
		return EC_LPC_STATUS_ERROR;
	return EC_LPC_STATUS_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_THERMAL_SET_THRESHOLD,
		thermal_command_set_threshold);


enum lpc_status thermal_command_get_threshold(uint8_t *data)
{
	struct lpc_params_thermal_get_threshold *p =
			(struct lpc_params_thermal_get_threshold *)data;
	struct lpc_response_thermal_get_threshold *r =
			(struct lpc_response_thermal_get_threshold *)data;

	r->value = thermal_get_threshold(p->sensor_id, p->threshold_id);
	if (r->value == -1)
		return EC_LPC_STATUS_ERROR;

	return EC_LPC_STATUS_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_THERMAL_GET_THRESHOLD,
		thermal_command_get_threshold);


enum lpc_status thermal_command_auto_fan_ctrl(uint8_t *data)
{
	if (thermal_toggle_auto_fan_ctrl(1))
		return EC_LPC_STATUS_ERROR;
	return EC_LPC_STATUS_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_THERMAL_AUTO_FAN_CTRL,
		thermal_command_auto_fan_ctrl);

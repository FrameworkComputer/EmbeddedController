/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#include "console.h"
#include "temp_sensor.h"
#include "temp_sensor_commands.h"
#include "lpc_commands.h"
#include "uart.h"
#include "util.h"


/*****************************************************************************/
/* Host commands */

enum lpc_status temp_sensor_command_get_readings(uint8_t *data)
{
	struct lpc_params_temp_sensor_get_readings *p =
			(struct lpc_params_temp_sensor_get_readings *)data;
	struct lpc_response_temp_sensor_get_readings *r =
			(struct lpc_response_temp_sensor_get_readings *)data;

	int rv;
	rv = temp_sensor_read(p->temp_sensor_id);
	if (rv == -1)
		return EC_LPC_STATUS_ERROR;
	r->value = rv;

	return EC_LPC_STATUS_SUCCESS;
}

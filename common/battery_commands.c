/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery host commands for Chrome EC
 */


#include "host_command.h"
#include "smart_battery.h"
#include "battery.h"

static inline uint8_t hex2asc(uint8_t hex)
{
	return hex + ((hex > 9) ? 'A' : '0');
}

enum lpc_status battery_command_get_info(uint8_t *data)
{
	struct lpc_response_battery_info *r =
			(struct lpc_response_battery_info *)data;
	int val;

	if (battery_design_capacity(&val))
		return EC_LPC_RESULT_ERROR;
	r->design_capacity         = val;
	r->design_capacity_warning = val * BATTERY_LEVEL_WARNING / 100;
	r->design_capacity_low     = val * BATTERY_LEVEL_LOW / 100;

	if (battery_full_charge_capacity(&val))
		return EC_LPC_RESULT_ERROR;
	r->last_full_charge_capacity = val;

	if (battery_design_voltage(&val))
		return EC_LPC_RESULT_ERROR;
	r->design_output_voltage = val;

	if (battery_cycle_count(&val))
		return EC_LPC_RESULT_ERROR;
	r->cycle_count = val;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_BATTERY_INFO,
		battery_command_get_info);

enum lpc_status battery_command_get_type(uint8_t *data)
{
	struct lpc_response_battery_text *r =
			(struct lpc_response_battery_text *)data;

	if (battery_device_chemistry(r->text, sizeof(r->text)))
		return EC_LPC_RESULT_ERROR;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_BATTERY_TYPE,
		battery_command_get_type);

enum lpc_status battery_command_get_model_number(uint8_t *data)
{
	struct lpc_response_battery_text *r =
			(struct lpc_response_battery_text *)data;

	if (battery_device_name(r->text, sizeof(r->text)))
		return EC_LPC_RESULT_ERROR;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_BATTERY_MODEL_NUMBER,
		battery_command_get_model_number);

enum lpc_status battery_command_get_serial_number(uint8_t *data)
{
	struct lpc_response_battery_text *r =
			(struct lpc_response_battery_text *)data;
	int serial;

	if (battery_serial_number(&serial))
		return EC_LPC_RESULT_ERROR;

	/* Smart battery serial number is 16 bits */
	r->text[0] = hex2asc(0xf & (serial >> 12));
	r->text[1] = hex2asc(0xf & (serial >> 8));
	r->text[2] = hex2asc(0xf & (serial >> 4));
	r->text[3] = hex2asc(0xf & serial);
	r->text[4] = 0;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_BATTERY_SERIAL_NUMBER,
		battery_command_get_serial_number);

enum lpc_status battery_command_get_oem(uint8_t *data)
{
	struct lpc_response_battery_text *r =
			(struct lpc_response_battery_text *)data;

	if (battery_manufacturer_name(r->text, sizeof(r->text)))
		return EC_LPC_RESULT_ERROR;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_BATTERY_OEM,
		battery_command_get_oem);

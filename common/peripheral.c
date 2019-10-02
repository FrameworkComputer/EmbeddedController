/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"
#include "ec_commands.h"
#include "host_command.h"
#include "usb_pd_tcpm.h"

#ifdef CONFIG_HOSTCMD_LOCATE_CHIP
static enum ec_status hc_locate_chip(struct host_cmd_handler_args *args)
{
	const struct ec_params_locate_chip *params = args->params;
	struct ec_response_locate_chip *resp = args->response;

	switch (params->type) {
	case EC_CHIP_TYPE_CBI_EEPROM:
#ifdef CONFIG_CROS_BOARD_INFO
		if (params->index >= 1)
			return EC_RES_OVERFLOW;
		resp->bus_type = EC_BUS_TYPE_I2C;
		resp->i2c_info.port = I2C_PORT_EEPROM;
		resp->i2c_info.addr_flags = I2C_ADDR_EEPROM_FLAGS;
#else
		/* Lookup type is supported, but not present on system. */
		return EC_RES_UNAVAILABLE;
#endif /* CONFIG_CROS_BOARD_INFO */
		break;
	case EC_CHIP_TYPE_TCPC:
#if defined(CONFIG_USB_PD_PORT_MAX_COUNT) && !defined(CONFIG_USB_PD_TCPC)
		if (params->index >= CONFIG_USB_PD_PORT_MAX_COUNT)
			return EC_RES_OVERFLOW;
		resp->bus_type = tcpc_config[params->index].bus_type;
		if (resp->bus_type == EC_BUS_TYPE_I2C) {
			resp->i2c_info.port =
				tcpc_config[params->index].i2c_info.port;
			resp->i2c_info.addr_flags =
				tcpc_config[params->index].i2c_info.addr_flags;
		}
#ifdef CONFIG_INTEL_VIRTUAL_MUX
		resp->reserved = tcpc_config[params->index].usb23;
#endif
#else
		return EC_RES_UNAVAILABLE;
#endif /* CONFIG_USB_PD_PORT_MAX_COUNT */
		break;
	default:
		/* The type was unrecognized */
		return EC_RES_INVALID_PARAM;
	}

	args->response_size = sizeof(*resp);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_LOCATE_CHIP, hc_locate_chip, EC_VER_MASK(0));
/* If the params union expands in the future, need to bump EC_VER_MASK */
BUILD_ASSERT(sizeof(struct ec_params_locate_chip) == 4);
#endif /* CONFIG_HOSTCMD_LOCATE_CHIP */

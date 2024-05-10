/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "common.h"
#include "compile_time_macros.h"
#include "ec_commands.h"
#include "host_command.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#if defined(CONFIG_USB_PD_CONTROLLER)
#include "usbc/pdc_power_mgmt.h"
#endif

#ifdef CONFIG_HOSTCMD_LOCATE_CHIP
static enum ec_status hc_locate_chip(struct host_cmd_handler_args *args)
{
	const struct ec_params_locate_chip *params = args->params;
	struct ec_response_locate_chip *resp = args->response;

	switch (params->type) {
	case EC_CHIP_TYPE_CBI_EEPROM:
#ifdef CONFIG_CBI_EEPROM
		if (params->index >= 1)
			return EC_RES_OVERFLOW;
		resp->bus_type = EC_BUS_TYPE_I2C;
		resp->i2c_info.port = I2C_PORT_EEPROM;
		resp->i2c_info.addr_flags = I2C_ADDR_EEPROM_FLAGS;
#else
		/* Lookup type is supported, but not present on system. */
		return EC_RES_UNAVAILABLE;
#endif /* CONFIG_CBI_EEPROM */
		break;
	case EC_CHIP_TYPE_TCPC:
#if !defined(CONFIG_USB_PD_CONTROLLER) &&     \
	defined(CONFIG_USB_POWER_DELIVERY) && \
	defined(CONFIG_USB_PD_PORT_MAX_COUNT) && !defined(CONFIG_USB_PD_TCPC)
		if (params->index >= board_get_usb_pd_port_count())
			return EC_RES_OVERFLOW;
		resp->bus_type = tcpc_config[params->index].bus_type;
		if (resp->bus_type == EC_BUS_TYPE_I2C) {
			resp->i2c_info.port =
				tcpc_config[params->index].i2c_info.port;
			resp->i2c_info.addr_flags =
				tcpc_config[params->index].i2c_info.addr_flags;
		}
#else
		/* Not reachable in new boards. */
		return EC_RES_UNAVAILABLE; /* LCOV_EXCL_LINE */
#endif /* CONFIG_USB_PD_PORT_MAX_COUNT */
		break;
	case EC_CHIP_TYPE_PDC:
#if defined(CONFIG_USB_PD_CONTROLLER)
		if (params->index >= pdc_power_mgmt_get_usb_pd_port_count())
			return EC_RES_OVERFLOW;

		struct pdc_bus_info_t bus_info;
		int i2c_port;

		if (pdc_power_mgmt_get_bus_info(params->index, &bus_info)) {
			/* Cannot obtain I2C info for PDC */
			return EC_RES_ERROR;
		}

		/* Only I2C PDCs are supported at this time. */
		if (bus_info.bus_type != PDC_BUS_TYPE_I2C) {
			return EC_RES_UNAVAILABLE;
		}
		resp->bus_type = EC_BUS_TYPE_I2C;

		resp->i2c_info.addr_flags = bus_info.i2c.addr;

		i2c_port = i2c_get_port_from_device(bus_info.i2c.bus);
		if (i2c_port < 0) {
			return EC_RES_DUP_UNAVAILABLE;
		}

		resp->i2c_info.port = (uint16_t)i2c_port;

		break;
#else
		/* Only available on boards that use PD_CONTROLLER */
		return EC_RES_UNAVAILABLE; /* LCOV_EXCL_LINE */
#endif /* CONFIG_USB_PD_CONTROLLER */
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

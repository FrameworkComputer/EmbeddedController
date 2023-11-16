/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "i2c.h"
#include "stdbool.h"

#include <dt-bindings/gpio_defines.h>

int board_allow_i2c_passthru(const struct i2c_cmd_desc_t *cmd_desc)
{
	const uint8_t req_port = cmd_desc->port;

	switch (req_port) {
	case I2C_PORT_BY_DEV(DT_NODELABEL(tcpc_ps8815_port1)): {
		/*
		 * i2c4_1:
		 * The PS8815 TCPC at C1 is FW upgradable from the AP.
		 * Other TCPCs are not upgradable.
		 */

		uint32_t usb_db_type;

		if (cros_cbi_get_fw_config(FW_USB_DB, &usb_db_type) !=
		    EC_SUCCESS) {
			return false;
		}

		if (usb_db_type == FW_USB_DB_USB3)
			return true;

		break;
	}

	case I2C_PORT_BY_DEV(DT_NODELABEL(usb_c1_anx7452_retimer)): {
		/*
		 * i2c6_1:
		 * The ANX7452 retimer at C1 is FW upgradable from the AP.
		 * Other targets are not upgradable.
		 */

		uint32_t usb_db_type;

		if (cros_cbi_get_fw_config(FW_USB_DB, &usb_db_type) !=
		    EC_SUCCESS) {
			return false;
		}

		switch (usb_db_type) {
		case FW_USB_DB_USB4_ANX7452_V2:
			return true;
		}

		break;
	}
	}

	/* All remaining targets are not allowed */
	return false;
}

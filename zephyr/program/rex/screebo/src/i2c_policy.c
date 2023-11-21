/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "i2c.h"
#include "stdbool.h"

int board_allow_i2c_passthru(const struct i2c_cmd_desc_t *cmd_desc)
{
	const uint8_t req_port = cmd_desc->port;
	uint32_t usb_db_type;

	/*
	 * See i2c map in b:311283246.
	 * Only USB3_DB with PS8815 TCPC is FW upgradable from the AP.
	 */
	if (req_port == I2C_PORT_BY_DEV(DT_NODELABEL(tcpc_ps8815_port1))) {
		if (cros_cbi_get_fw_config(FW_USB_DB, &usb_db_type)) {
			return false;
		} else {
			return usb_db_type == FW_USB_DB_USB3;
		}
	}

	return false;
}

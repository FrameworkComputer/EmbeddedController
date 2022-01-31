/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charger.h"
#include "charger/isl923x_public.h"
#include "extpower.h"
#include "usb_pd.h"
#include "sub_board.h"

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_C0_TCPC,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
	/* Sub-board */
	{
		.i2c_port = I2C_PORT_USB_C1_TCPC,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

int extpower_is_present(void)
{
	int port;
	int rv;
	bool acok;

	for (port = 0; port < board_get_usb_pd_port_count(); port++) {
		rv = raa489000_is_acok(port, &acok);
		if ((rv == EC_SUCCESS) && acok)
			return 1;
	}

	return 0;
}

/*
 * Nivviks does not have a GPIO indicating whether extpower is present,
 * so detect using the charger(s).
 */
__override void board_check_extpower(void)
{
	static int last_extpower_present;
	int extpower_present = extpower_is_present();

	if (last_extpower_present ^ extpower_present)
		extpower_handle_update(extpower_present);

	last_extpower_present = extpower_present;
}

/*
 * Count of chargers depends on sub board presence.
 */
__override uint8_t board_get_charger_chip_count(void)
{
	switch (nissa_get_sb_type()) {
	default:
		return 1;

	case NISSA_SB_C_A:
	case NISSA_SB_C_LTE:
		return 2;
	}
}

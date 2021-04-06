/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

#include "button.h"
#include "cbi_ec_fw_config.h"
#include "charge_ramp.h"
#include "charger.h"
#include "console.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_lsm6dso.h"
#include "driver/als_tcs3400.h"
#include "power_button.h"
#include "power.h"
#include "switch.h"
#include "tablet_mode.h"
#include "throttle_ap.h"

#include "gpio_list.h" /* Must come after other header files. */

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

/******************************************************************************/
/* USB-A charging control */

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USBA_R,
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

/******************************************************************************/

/*
 * FW_CONFIG defaults for brya if the CBI.FW_CONFIG data is not
 * initialized.
 */
const union brya_cbi_fw_config fw_config_defaults = {
	.usb_db = DB_USB3_PS8815,
};

__override void board_cbi_init(void)
{
	config_usb_db_type();
}

#ifdef CONFIG_CHARGE_RAMP_SW

/*
 * TODO(b/181508008): tune this threshold
 */

#define BC12_MIN_VOLTAGE 4400

/**
 * Return true if VBUS is too low
 */
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	int voltage;

	if (charger_get_vbus_voltage(port, &voltage))
		voltage = 0;

	CPRINTS("%s: charger reports VBUS %d on port %d", __func__,
		voltage, port);

	if (voltage == 0) {
		CPRINTS("%s: must be disconnected", __func__);
		return 1;
	}

	if (voltage < BC12_MIN_VOLTAGE) {
		CPRINTS("%s: lower than %d", __func__,
			BC12_MIN_VOLTAGE);
		return 1;
	}

	return 0;
}

#endif /* CONFIG_CHARGE_RAMP_SW */

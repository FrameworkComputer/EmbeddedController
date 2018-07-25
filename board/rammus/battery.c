/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Placeholder values for temporary battery pack.
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "util.h"

static enum battery_present batt_pres_prev = BP_NOT_SURE;

/* Shutdown mode parameters to write to manufacturer access register */
#define SB_SHIP_MODE_REG	SB_MANUFACTURER_ACCESS
#define SB_SHUTDOWN_DATA	0x0010

/*
 * Unlike other smart batteries, Nautilus battery uses different bit fields
 * in manufacturer access register for the conditions of the CHG/DSG FETs.
 */
#define BATFETS_SHIFT		(14)
#define BATFETS_MASK		(0x3)
#define BATFETS_DISABLED	(0x2)

#define CHARGING_VOLTAGE_MV_SAFE	8400
#define CHARGING_CURRENT_MA_SAFE	1500

/* TODO(b:111815315): Need to config/implement the battery related code */

static const struct battery_info info = {
	.voltage_max = 8700,
	.voltage_normal = 7700,
	.voltage_min = 6000,
	/* Pre-charge values. */
	.precharge_current = 200, /* mA */

	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c = 0,
	.charging_max_c = 50,
	.discharging_min_c = -20,
	.discharging_max_c = 70,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int board_cut_off_battery(void)
{
	int rv;

	/* Ship mode command must be sent twice to take effect */
	rv = sb_write(SB_SHIP_MODE_REG, SB_SHUTDOWN_DATA);

	if (rv != EC_SUCCESS)
		return rv;

	return sb_write(SB_SHIP_MODE_REG, SB_SHUTDOWN_DATA);
}

enum battery_present battery_hw_present(void)
{
	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(GPIO_BATT_PRES_L) ? BP_NO : BP_YES;
}

static int battery_init(void)
{
	int batt_status;

	return battery_status(&batt_status) ? 0 :
		!!(batt_status & STATUS_INITIALIZED);
}

/*
 * Check for case where both XCHG and XDSG bits are set indicating that even
 * though the FG can be read from the battery, the battery is not able to be
 * charged or discharged. This situation might happen when power is reconnected
 * to a battery pack in sleep mode. In this transient siuation, the FG can be
 * read, but the battery is still not able to provide power to the system. The
 * calling function returns batt_pres = BP_NO, which instructs the charging
 * state machine to prevent powering up the AP on battery alone which could lead
 * to a brownout event when the battery isn't able yet to provide power to the
 * system.
 */
static int battery_check_disconnect(void)
{
	int rv;
	int batt_mfgacc;

	/* Check if battery charging + discharging is disabled. */
	rv = sb_read(SB_MANUFACTURER_ACCESS, &batt_mfgacc);
	if (rv)
		return BATTERY_DISCONNECT_ERROR;

	if (((batt_mfgacc >> BATFETS_SHIFT) & BATFETS_MASK) ==
	    BATFETS_DISABLED)
		return BATTERY_DISCONNECTED;

	return BATTERY_NOT_DISCONNECTED;
}

enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres;

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	/*
	 * Make sure battery status is implemented, I2C transactions are
	 * successful & the battery status is initialized to find out if it
	 * is a working battery and it is not in the cut-off mode.
	 *
	 * If battery I2C fails but VBATT is high, battery is booting from
	 * cut-off mode.
	 *
	 * FETs are turned off after Power Shutdown time.
	 * The device will wake up when a voltage is applied to PACK.
	 * Battery status will be inactive until it is initialized.
	 */
	if (batt_pres == BP_YES && batt_pres_prev != batt_pres &&
	    (battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL ||
	     battery_check_disconnect() != BATTERY_NOT_DISCONNECTED ||
	     battery_init() == 0)) {
		batt_pres = BP_NO;
	}

	batt_pres_prev = batt_pres;
	return batt_pres;
}


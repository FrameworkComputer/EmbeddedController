/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "baseboard_battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)


static enum battery_present batt_pres_prev = BP_NOT_SURE;

/* Get type of the battery connected on the board */
static int board_get_battery_type(void)
{
	char manu_name[32], device_name[32];
	int i;
	static enum battery_type board_battery_type = BATTERY_TYPE_COUNT;

	/*
	 * If board_battery_type is not the default value, then can return here
	 * as there is no need to query the fuel gauge.
	 */
	if (board_battery_type != BATTERY_TYPE_COUNT)
		return board_battery_type;

	/* Get the manufacture name. If can't read then just exit */
	if (battery_manufacturer_name(manu_name, sizeof(manu_name)))
		return board_battery_type;

	/*
	 * Compare the manufacturer name read from the fuel gauge to the
	 * manufacture names defined in the board_battery_info table above. If
	 * a device name has been specified in the board_battery_info table,
	 * then both the manufacture and device name must match.
	 */
	for (i = 0; i < BATTERY_TYPE_COUNT; i++) {
		const struct fuel_gauge_info * const fuel_gauge =
			&board_battery_info[i].fuel_gauge;

		if (strcasecmp(manu_name, fuel_gauge->manuf_name))
			continue;

		if (fuel_gauge->device_name == NULL) {
			board_battery_type = i;
			break;
		}

		if (battery_device_name(device_name, sizeof(device_name)))
			continue;

		if (strcasecmp(device_name, fuel_gauge->device_name))
			continue;

		CPRINTS("found batt:%s", fuel_gauge->manuf_name);
		board_battery_type = i;

		break;
	}

	return board_battery_type;
}

/*
 * Initialize the battery type for the board.
 *
 * Very first battery board_battery_info is called by the charger driver to
 * initialize the charger parameters hence initialize the battery type for the
 * board as soon as the I2C is initialized.
 */
static void board_init_battery_type(void)
{
	if (board_get_battery_type() == BATTERY_TYPE_COUNT)
		CPRINTS("battery not found");
}
DECLARE_HOOK(HOOK_INIT, board_init_battery_type, HOOK_PRIO_INIT_I2C + 1);

static inline const struct board_batt_params *board_get_batt_params(void)
{
	int type = board_get_battery_type();

	return &board_battery_info[type == BATTERY_TYPE_COUNT ?
		DEFAULT_BATTERY_TYPE : type];
}

const struct battery_info *battery_get_info(void)
{
	return &board_get_batt_params()->batt_info;
}

int board_cut_off_battery(void)
{
	int rv;
	int cmd;
	int data;
	int type = board_get_battery_type();

	/* If battery type is unknown can't send ship mode command */
	if (type == BATTERY_TYPE_COUNT)
		return EC_RES_ERROR;

	/* Ship mode command must be sent twice to take effect */
	cmd = board_battery_info[type].fuel_gauge.ship_mode.reg_addr;
	data = board_battery_info[type].fuel_gauge.ship_mode.reg_data[0];
	rv = sb_write(cmd, data);
	if (rv != EC_SUCCESS)
		return EC_RES_ERROR;

	data = board_battery_info[type].fuel_gauge.ship_mode.reg_data[1];
	rv = sb_write(cmd, data);

	return rv ? EC_RES_ERROR : EC_RES_SUCCESS;
}

int charger_profile_override(struct charge_state_data *curr)
{
	int type = board_get_battery_type();

	/*
	 * Some batteries, when fully discharged, may request 0 voltage/current
	 * which can then inadvertently disable the charger leading to the
	 * battery not waking up. For this type of battery, marked by
	 * override_nil being set, if SOC is 0 and requested voltage/current is
	 * 0, then use precharge current and max voltage instead.
	 */
	if (type != BATTERY_TYPE_COUNT &&
	    board_battery_info[type].fuel_gauge.override_nil) {
		int v = board_battery_info[type].batt_info.voltage_max;
		int i = board_battery_info[type].batt_info.precharge_current;

		if (curr->requested_voltage == 0 &&
		    curr->requested_current == 0 &&
		    curr->batt.state_of_charge == 0) {
			/*
			 * Battery is dead, override with precharge current and
			 * max voltage setting for the battery.
			 */
			curr->requested_voltage = v;
			curr->requested_current = i;
		}
	}

	return 0;
}

enum battery_present battery_hw_present(void)
{
	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(GPIO_EC_BATT_PRES_L) ? BP_NO : BP_YES;
}

static int battery_init(void)
{
	int batt_status;

	return battery_status(&batt_status) ? 0 :
		!!(batt_status & STATUS_INITIALIZED);
}

/*
 * This function checks the charge/discharge FET status bits. Each battery type
 * supported provides the register address, mask, and disconnect value for these
 * 2 FET status bits. If the FET status matches the disconnected value, then
 * BATTERY_DISCONNECTED is returned. This function is required to handle the
 * cases when the fuel gauge is awake and will return a non-zero state of
 * charge, but is not able yet to provide power (i.e. discharge FET is not
 * active). By returning BATTERY_DISCONNECTED the AP will not be powered up
 * until either the external charger is able to provided enough power, or
 * the battery is able to provide power and thus prevent a brownout when the
 * AP is powered on by the EC.
 */
enum battery_disconnect_state battery_get_disconnect_state(void)
{
	int rv;
	int reg;
	uint8_t data[6];
	int type = board_get_battery_type();

	/* If battery type is not known, can't check CHG/DCHG FETs */
	if (type == BATTERY_TYPE_COUNT) {
		/* Still don't know, so return here */
		return BATTERY_DISCONNECT_ERROR;
	}

	/* Read the status of charge/discharge FETs */
	if (board_battery_info[type].fuel_gauge.fet.mfgacc_support == 1) {
		rv = sb_read_mfgacc(PARAM_OPERATION_STATUS,
				SB_ALT_MANUFACTURER_ACCESS, data, sizeof(data));
		/* Get the lowest 16bits of the OperationStatus() data */
		reg = data[2] | data[3] << 8;
	} else
		rv = sb_read(board_battery_info[type].fuel_gauge.fet.reg_addr,
					&reg);

	if (rv)
		return BATTERY_DISCONNECT_ERROR;

	if ((reg & board_battery_info[type].fuel_gauge.fet.reg_mask) ==
	    board_battery_info[type].fuel_gauge.fet.disconnect_val) {
		CPRINTS("Batt disconnected: reg 0x%04x mask 0x%04x disc 0x%04x",
			reg,
			board_battery_info[type].fuel_gauge.fet.reg_mask,
			board_battery_info[type].fuel_gauge.fet.disconnect_val);
		return BATTERY_DISCONNECTED;
	}

	return BATTERY_NOT_DISCONNECTED;
}

/*
 * Physical detection of battery.
 */
static enum battery_present battery_check_present_status(void)
{
	enum battery_present batt_pres;

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	/*
	 * If the battery is not physically connected, then no need to perform
	 * any more checks.
	 */
	if (batt_pres != BP_YES)
		return batt_pres;

	/*
	 * If the battery is present now and was present last time we checked,
	 * return early.
	 */
	if (batt_pres == batt_pres_prev)
		return batt_pres;

	/*
	 * Ensure that battery is:
	 * 1. Not in cutoff
	 * 2. Initialized
	 */
	if (battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL ||
	    battery_init() == 0) {
		batt_pres = BP_NO;
	}

	return batt_pres;
}

enum battery_present battery_is_present(void)
{
	batt_pres_prev = battery_check_present_status();
	return batt_pres_prev;
}

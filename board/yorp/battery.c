/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

/* Number of writes needed to invoke battery cutoff command */
#define SHIP_MODE_WRITES 2

enum battery_type {
	BATTERY_LGC15,
	BATTERY_PANASONIC,
	BATTERY_TYPE_COUNT,
};

struct ship_mode_info {
	const uint8_t reg_addr;
	const uint16_t reg_data[SHIP_MODE_WRITES];
};

struct fet_info {
	const int mfgacc_support;
	const uint8_t reg_addr;
	const uint16_t reg_mask;
	const uint16_t disconnect_val;
};

struct fuel_gauge_info {
	const char *manuf_name;
	const char *device_name;
	const uint8_t override_nil;
	const struct ship_mode_info ship_mode;
	const struct fet_info fet;
};

struct board_batt_params {
	const struct fuel_gauge_info fuel_gauge;
	const struct battery_info batt_info;
};

#define DEFAULT_3S_BATTERY_TYPE BATTERY_LGC15
static enum battery_present batt_pres_prev = BP_NOT_SURE;

/*
 * Battery info for all Octopus battery types. Note that the fields
 * start_charging_min/max and charging_min/max are not used for the charger.
 * The effective temperature limits are given by discharging_min/max_c.
 *
 * Fuel Gauge (FG) parameters which are used for determing if the battery
 * is connected, the appropriate ship mode (battery cutoff) command, and the
 * charge/discharge FETs status.
 *
 * Ship mode (battery cutoff) requires 2 writes to the appropirate smart battery
 * register. For some batteries, the charge/discharge FET bits are set when
 * charging/discharging is active, in other types, these bits set mean that
 * charging/discharging is disabled. Therefore, in addition to the mask for
 * these bits, a disconnect value must be specified. Note that for TI fuel
 * gauge, the charge/discharge FET status is found in Operation Status (0x54),
 * but a read of Manufacturer Access (0x00) will return the lower 16 bits of
 * Operation status which contains the FET status bits.
 *
 * The assumption for battery types supported is that the charge/discharge FET
 * status can be read with a sb_read() command and therefore, only the regsister
 * address, mask, and disconnect value need to be provided.
 */
static const struct board_batt_params info[] = {
	/* LGC AC15A8J Battery Information */
	[BATTERY_LGC15] = {
		.fuel_gauge = {
			.manuf_name = "LGC",
			.device_name = "AC15A8J",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.mfgacc_support = 1,
				.reg_addr = 0x0,
				.reg_mask = 0x0002,
				.disconnect_val = 0x0,
			}
		},
		.batt_info = {
			.voltage_max		= 13200,
			.voltage_normal		= 11520, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 256,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 60,
		},
	},

	/* Panasonic AP1505L Battery Information */
	[BATTERY_PANASONIC] = {
		.fuel_gauge = {
			.manuf_name = "PANASONIC",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x4000,
				.disconnect_val = 0x0,
			}
		},
		.batt_info = {
			.voltage_max		= 13200,
			.voltage_normal		= 11550, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 256,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 60,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(info) == BATTERY_TYPE_COUNT);

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
	 * Compare the manufacturer name read from the fuel gague to the
	 * manaufactuer names defined in the info table above. If a device name
	 * has been specified in the info table, then both the manufactuer and
	 * device name must match.
	 */
	for (i = 0; i < BATTERY_TYPE_COUNT; i++) {
		if (strcasecmp(manu_name,
			info[i].fuel_gauge.manuf_name))
			continue;

		if (info[i].fuel_gauge.device_name == NULL) {
			board_battery_type = i;
			break;
		}

		if (battery_device_name(device_name, sizeof(device_name)))
			continue;

		if (strcasecmp(device_name, info[i].fuel_gauge.device_name))
			continue;

		board_battery_type = i;
		CPRINTS("found batt:%s",
			info[board_battery_type].fuel_gauge.manuf_name);
		break;

	}

	return board_battery_type;
}

/*
 * Initialize the battery type for the board.
 *
 * Very first battery info is called by the charger driver to initialize
 * the charger parameters hence initialize the battery type for the board
 * as soon as the I2C is initialized.
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

	return &info[type == BATTERY_TYPE_COUNT ?
		     DEFAULT_3S_BATTERY_TYPE : type];
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
	cmd = info[type].fuel_gauge.ship_mode.reg_addr;
	data = info[type].fuel_gauge.ship_mode.reg_data[0];
	rv = sb_write(cmd, data);
	if (rv != EC_SUCCESS)
		return EC_RES_ERROR;

	data = info[type].fuel_gauge.ship_mode.reg_data[1];
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
	if (type != BATTERY_TYPE_COUNT && info[type].fuel_gauge.override_nil) {
		int v = info[type].batt_info.voltage_max;
		int i = info[type].batt_info.precharge_current;

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
 * This function checks the charge/dishcarge FET status bits. Each battery type
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
static int battery_check_disconnect(void)
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
	if (info[type].fuel_gauge.fet.mfgacc_support == 1) {
		rv = sb_read_mfgacc(PARAM_OPERATION_STATUS,
				SB_ALT_MANUFACTURER_ACCESS, data, sizeof(data));
		/* Get the lowest 16bits of the OperationStatus() data */
		reg = data[2] | data[3] << 8;
	} else
		rv = sb_read(info[type].fuel_gauge.fet.reg_addr,
					&reg);

	if (rv)
		return BATTERY_DISCONNECT_ERROR;

	CPRINTS("Battery FET: reg 0x%04x mask 0x%04x disc 0x%04x", reg,
		info[type].fuel_gauge.fet.reg_mask,
		info[type].fuel_gauge.fet.disconnect_val);
	reg &= info[type].fuel_gauge.fet.reg_mask;
	if (reg == info[type].fuel_gauge.fet.disconnect_val)
		return BATTERY_DISCONNECTED;

	return BATTERY_NOT_DISCONNECTED;
}

/*
 * Physical detection of battery.
 */
static enum battery_present battery_check_present_status(void)
{
	enum battery_present batt_pres;
	int batt_disconnect_status;

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
	 * Check battery disconnect status. If we are unable to read battery
	 * disconnect status, then return BP_NOT_SURE. Battery could be in ship
	 * mode and might require pre-charge current to wake it up. BP_NO is not
	 * returned here because charger state machine will not provide
	 * pre-charge current assuming that battery is not present.
	 */
	batt_disconnect_status = battery_check_disconnect();
	if (batt_disconnect_status == BATTERY_DISCONNECT_ERROR)
		return BP_NOT_SURE;

	/*
	 * Ensure that battery is:
	 * 1. Not in cutoff
	 * 2. Not disconnected
	 * 3. Initialized
	 */
	if (battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL ||
	    batt_disconnect_status != BATTERY_NOT_DISCONNECTED ||
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

int board_battery_initialized(void)
{
	return battery_hw_present() == batt_pres_prev;
}

/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "battery_smart.h"
#include "bd9995x.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

/* Number of writes needed to invoke battery cutoff command */
#define SHIP_MODE_WRITES 2

enum battery_type {
	BATTERY_LGC15,
	BATTERY_LGC203,
	BATTERY_SANYO,
	BATTERY_SONY,
	BATTERY_PANASONIC,
	BATTERY_CELXPERT,
	BATTERY_LGC011,
	BATTERY_SMP011,
	BATTERY_LGC,
	BATTERY_BYD,
	BATTERY_SIMPLO,
	BATTERY_TYPE_COUNT,
};

#define DEFAULT_BATTERY_TYPE BATTERY_SANYO
static enum battery_present batt_pres_prev = BP_NOT_SURE;
static enum battery_type board_battery_type = BATTERY_TYPE_COUNT;

/* Battery may delay reporting battery present */
static int battery_report_present = 1;

static int disch_on_ac;

/*
 * Battery info for all Coral battery types. Note that the fields
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
				.reg_addr = 0x0,
				.reg_mask = 0x0002,
				.disconnect_val = 0x0,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13200, 5),
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

	/* LGC C203-36J Battery Information */
	[BATTERY_LGC203] = {
		.fuel_gauge = {
			.manuf_name = "AS1GXXc3KB",
			.ship_mode = {
				.reg_addr = 0x00,
				.reg_data = { 0x0010, 0x0010 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0002,
				.disconnect_val = 0x0,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13200, 5),
			.voltage_normal		= 11520, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 256,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 45,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 60,
		},
	},

	/* SANYO AC15A3J Battery Information */
	[BATTERY_SANYO] = {
		.fuel_gauge = {
			.manuf_name = "SANYO",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x4000,
				.disconnect_val = 0x0,
			},
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13200, 5),
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

	/* Sony Ap13J4K Battery Information */
	[BATTERY_SONY] = {
		.fuel_gauge = {
			.manuf_name = "SONYCorp",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x8000,
				.disconnect_val = 0x8000,
			},
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13200, 5),
			.voltage_normal		= 11400, /* mV */
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
			},
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13200, 5),
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

	/* Celxpert Li7C3PG0 Battery Information */
	[BATTERY_CELXPERT] = {
		.fuel_gauge = {
			.manuf_name = "Celxpert",
			.ship_mode = {
				.reg_addr = 0x34,
				.reg_data = { 0x0, 0x1000 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0018,
				.disconnect_val = 0x0,
			},
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13050, 5),
			.voltage_normal		= 11400, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 200,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 60,
		},
	},

	/* LGC\011 L17L3PB0 Battery Information */
	[BATTERY_LGC011] = {
		.fuel_gauge = {
			.manuf_name = "LGC",
			.ship_mode = {
				.reg_addr = 0x34,
				.reg_data = { 0x0, 0x1000 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0018,
				.disconnect_val = 0x0,
			},
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13050, 5),
			.voltage_normal		= 11400, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 500,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 60,
		},
	},

	/* SMP\011 L17M3PB0 Battery Information */
	[BATTERY_SMP011] = {
		.fuel_gauge = {
			.manuf_name = "SMP",
			.ship_mode = {
				.reg_addr = 0x34,
				.reg_data = { 0x0, 0x1000 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x0018,
				.disconnect_val = 0x0,
			},
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13050, 5),
			.voltage_normal		= 11400, /* mV */
			.voltage_min		= 9000, /* mV */
			.precharge_current	= 186,	/* mA */
			.start_charging_min_c	= 0,
			.start_charging_max_c	= 50,
			.charging_min_c		= 0,
			.charging_max_c		= 60,
			.discharging_min_c	= 0,
			.discharging_max_c	= 60,
		},
	},

	/* LGC DELL Y07HK Battery Information */
	[BATTERY_LGC] = {
		.fuel_gauge = {
			.manuf_name = "LGC-LGC3.553",
			.ship_mode = {
				.reg_addr = 0x0,
				.reg_data = { 0x10, 0x10 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x6000,
				.disconnect_val = 0x6000,
			},
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13200, 5),
			.voltage_normal		= 114000, /* mV */
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

	/* BYD DELL FY8XM6C Battery Information */
	[BATTERY_BYD] = {
		.fuel_gauge = {
			.manuf_name = "BYD",
			.ship_mode = {
				.reg_addr = 0x0,
				.reg_data = { 0x10, 0x10 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x6000,
				.disconnect_val = 0x6000,
			},
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13200, 5),
			.voltage_normal		= 114000, /* mV */
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

	/* Simplo () Battery Information */
	[BATTERY_SIMPLO] = {
		.fuel_gauge = {
			.manuf_name = "SMP-SDI3.72",
			.ship_mode = {
				.reg_addr = 0x0,
				.reg_data = { 0x10, 0x10 },
			},
			.fet = {
				.reg_addr = 0x43,
				.reg_mask = 0x0003,
				.disconnect_val = 0x0000,
			},
		},
		.batt_info = {
			.voltage_max		= TARGET_WITH_MARGIN(13200, 5),
			.voltage_normal		= 114900, /* mV */
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

static inline const struct board_batt_params *board_get_batt_params(void)
{
	return &info[board_battery_type == BATTERY_TYPE_COUNT ?
			     DEFAULT_BATTERY_TYPE :
			     board_battery_type];
}

/* Get type of the battery connected on the board */
static int board_get_battery_type(void)
{
	char manu_name[32], device_name[32];
	int i;

	if (!battery_manufacturer_name(manu_name, sizeof(manu_name))) {
		for (i = 0; i < BATTERY_TYPE_COUNT; i++) {
			if (!strcasecmp(manu_name,
					info[i].fuel_gauge.manuf_name)) {
				if (info[i].fuel_gauge.device_name == NULL) {
					board_battery_type = i;
					break;
				} else if (!battery_device_name(
						   device_name,
						   sizeof(device_name))) {
					if (!strcasecmp(device_name,
							info[i].fuel_gauge
								.device_name)) {
						board_battery_type = i;
						break;
					}
				}
			}
		}
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
	if (board_get_battery_type() != BATTERY_TYPE_COUNT)
		CPRINTS("found batt:%s",
			info[board_battery_type].fuel_gauge.manuf_name);
	else
		CPRINTS("battery not found");
}
DECLARE_HOOK(HOOK_INIT, board_init_battery_type, HOOK_PRIO_INIT_I2C + 1);

const struct battery_info *battery_get_info(void)
{
	return &board_get_batt_params()->batt_info;
}

int board_cut_off_battery(void)
{
	int rv;
	int cmd;
	int data;

	/* If battery type is unknown can't send ship mode command */
	if (board_get_battery_type() == BATTERY_TYPE_COUNT)
		return EC_RES_ERROR;

	/* Ship mode command must be sent twice to take effect */
	cmd = info[board_battery_type].fuel_gauge.ship_mode.reg_addr;
	data = info[board_battery_type].fuel_gauge.ship_mode.reg_data[0];
	rv = sb_write(cmd, data);
	if (rv != EC_SUCCESS)
		return EC_RES_ERROR;

	data = info[board_battery_type].fuel_gauge.ship_mode.reg_data[1];
	rv = sb_write(cmd, data);

	return rv ? EC_RES_ERROR : EC_RES_SUCCESS;
}

static int charger_should_discharge_on_ac(struct charge_state_data *curr)
{
	/* can not discharge on AC without battery */
	if (curr->batt.is_present != BP_YES)
		return 0;

	/* Do not discharge on AC if the battery is still waking up */
	if (!(curr->batt.flags & BATT_FLAG_WANT_CHARGE) &&
	    !(curr->batt.status & STATUS_FULLY_CHARGED))
		return 0;

	/*
	 * In light load (<450mA being withdrawn from VSYS) the DCDC of the
	 * charger operates intermittently i.e. DCDC switches continuously
	 * and then stops to regulate the output voltage and current, and
	 * sometimes to prevent reverse current from flowing to the input.
	 * This causes a slight voltage ripple on VSYS that falls in the
	 * audible noise frequency (single digit kHz range). This small
	 * ripple generates audible noise in the output ceramic capacitors
	 * (caps on VSYS and any input of DCDC under VSYS).
	 *
	 * To overcome this issue enable the battery learning operation
	 * and suspend USB charging and DC/DC converter.
	 */
	if (!battery_is_cut_off() &&
	    !(curr->batt.flags & BATT_FLAG_WANT_CHARGE) &&
	    (curr->batt.status & STATUS_FULLY_CHARGED))
		return 1;

	/*
	 * To avoid inrush current from the external charger, enable
	 * discharge on AC till the new charger is detected and charge
	 * detect delay has passed.
	 */
	if (!chg_ramp_is_detected() && curr->batt.state_of_charge > 2)
		return 1;

	return 0;
}

int charger_profile_override(struct charge_state_data *curr)
{
	disch_on_ac = charger_should_discharge_on_ac(curr);

	charger_discharge_on_ac(disch_on_ac);

	if (disch_on_ac) {
		curr->state = ST_DISCHARGE;
		return 0;
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

	return battery_status(&batt_status) ?
		       0 :
		       !!(batt_status & STATUS_INITIALIZED);
}

/* Allow booting now that the battery has woke up */
static void battery_now_present(void)
{
	CPRINTS("battery will now report present");
	battery_report_present = 1;
}
DECLARE_DEFERRED(battery_now_present);

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

	/* If battery type is not known, can't check CHG/DCHG FETs */
	if (board_battery_type == BATTERY_TYPE_COUNT) {
		/* Keep trying to determine the battery type */
		board_init_battery_type();
		if (board_battery_type == BATTERY_TYPE_COUNT)
			/* Still don't know, so return here */
			return BATTERY_DISCONNECT_ERROR;
	}

	/* Read the status of charge/discharge FETs */
	if (info[board_battery_type].fuel_gauge.flags &
	    FUEL_GAUGE_FLAG_MFGACC) {
		rv = sb_read_mfgacc(PARAM_OPERATION_STATUS,
				    SB_ALT_MANUFACTURER_ACCESS, data,
				    sizeof(data));
		/* Get the lowest 16bits of the OperationStatus() data */
		reg = data[2] | data[3] << 8;
	} else
		rv = sb_read(info[board_battery_type].fuel_gauge.fet.reg_addr,
			     &reg);

	if (rv)
		return BATTERY_DISCONNECT_ERROR;

	CPRINTS("Battery FET: reg 0x%04x mask 0x%04x disc 0x%04x", reg,
		info[board_battery_type].fuel_gauge.fet.reg_mask,
		info[board_battery_type].fuel_gauge.fet.disconnect_val);
	reg &= info[board_battery_type].fuel_gauge.fet.reg_mask;
	if (reg == info[board_battery_type].fuel_gauge.fet.disconnect_val)
		return BATTERY_DISCONNECTED;

	return BATTERY_NOT_DISCONNECTED;
}

/*
 * Physical detection of battery.
 */

enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres;
	static int battery_report_present_timer_started;

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	/*
	 * Make sure battery status is implemented, I2C transactions are
	 * success & the battery status is Initialized to find out if it
	 * is a working battery and it is not in the cut-off mode.
	 *
	 * FETs are turned off after Power Shutdown time.
	 * The device will wake up when a voltage is applied to PACK.
	 * Battery status will be inactive until it is initialized.
	 */
	if (batt_pres == BP_YES && batt_pres_prev != batt_pres &&
	    (battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL ||
	     battery_check_disconnect() != BATTERY_NOT_DISCONNECTED ||
	     battery_init() == 0)) {
		battery_report_present = 0;
		/*
		 * When this path is taken, the _timer_started flag must be
		 * reset so the 'else if' path will be entered and the
		 * battery_report_present flag can be set by the deferred
		 * call. This handles the case of the battery being disconected
		 * and reconnected while running or if battery_init() returns an
		 * error due to a failed sb_read.
		 */
		battery_report_present_timer_started = 0;
	} else if (batt_pres == BP_YES && batt_pres_prev == BP_NO &&
		   !battery_report_present_timer_started) {
		/*
		 * Wait 1/2 second before reporting present if it was
		 * previously reported as not present
		 */
		battery_report_present_timer_started = 1;
		battery_report_present = 0;
		hook_call_deferred(&battery_now_present_data, 500 * MSEC);
	}

	if (!battery_report_present)
		batt_pres = BP_NO;

	batt_pres_prev = batt_pres;

	return batt_pres;
}

int board_battery_initialized(void)
{
	return battery_hw_present() == batt_pres_prev;
}

/* Customs options controllable by host command. */
#define PARAM_FASTCHARGE (CS_PARAM_CUSTOM_PROFILE_MIN + 0)
#define PARAM_LEARN_MODE 0x10001
#define PARAM_DISCONNECT_STATE 0x10002

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	switch (param) {
	case PARAM_LEARN_MODE:
		*value = disch_on_ac;
		return EC_RES_SUCCESS;
	case PARAM_DISCONNECT_STATE:
		*value = battery_check_disconnect();
		return EC_RES_SUCCESS;
	default:
		return EC_RES_INVALID_PARAM;
	}
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}

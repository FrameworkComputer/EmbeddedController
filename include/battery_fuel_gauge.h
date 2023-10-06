/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery fuel gauge parameters
 */

#ifndef __CROS_EC_BATTERY_FUEL_GAUGE_H
#define __CROS_EC_BATTERY_FUEL_GAUGE_H

#include "battery.h"
#include "common.h"

#include <stdbool.h>

/* Number of writes needed to invoke battery cutoff command */
#define SHIP_MODE_WRITES 2

/* When battery type is not initialized */
#define BATTERY_TYPE_UNINITIALIZED -1

struct battery_voltage_current {
	uint16_t mv;
	uint16_t ma;
} __packed;

struct battery_temperature_range {
	int8_t min_c;
	int8_t max_c;
} __packed;

struct fuel_gauge_reg_addr_data {
	uint8_t addr;
	uint16_t data;
} __packed;

struct ship_mode_info {
	uint8_t reg_addr;
	uint16_t reg_data[SHIP_MODE_WRITES];
};

struct sleep_mode_info {
	uint8_t reg_addr;
	uint16_t reg_data;
};

struct fet_info {
	uint8_t reg_addr;
	uint16_t reg_mask;
	uint16_t disconnect_val;
	uint16_t cfet_mask; /* CHG FET status mask */
	uint16_t cfet_off_val;
};

enum fuel_gauge_flags {
	/*
	 * Write Block Support. If enabled, we use a i2c write block command
	 * instead of a 16-bit write. The effective difference is the i2c
	 * transaction will prefix the length (2).
	 */
	FUEL_GAUGE_FLAG_WRITE_BLOCK = BIT(0),
	/* Sleep command support. fuel_gauge_info.sleep_mode must be defined. */
	FUEL_GAUGE_FLAG_SLEEP_MODE = BIT(1),
	/*
	 * Manufacturer access command support. If enabled, FET status is read
	 * from the OperationStatus (0x54) register using the
	 * ManufacturerBlockAccess (0x44).
	 */
	FUEL_GAUGE_FLAG_MFGACC = BIT(2),
	/*
	 * SMB block protocol support in manufacturer access command. If
	 * enabled, FET status is read from the OperationStatus (0x54) register
	 * using the ManufacturerBlockAccess (0x44).
	 */
	FUEL_GAUGE_FLAG_MFGACC_SMB_BLOCK = BIT(3),
};

struct fuel_gauge_info {
	char *manuf_name;
	char *device_name;
	uint32_t flags;
	uint32_t board_flags;
	struct ship_mode_info ship_mode;
	struct sleep_mode_info sleep_mode;
	struct fet_info fet;
};

struct board_batt_params {
	struct fuel_gauge_info fuel_gauge;
	struct battery_info batt_info;
};

/* Forward declare board specific data used by common code */
extern struct board_batt_params default_battery_conf;
extern const struct board_batt_params board_battery_info[];
extern const enum battery_type DEFAULT_BATTERY_TYPE;

#ifdef CONFIG_BATTERY_TYPE_NO_AUTO_DETECT
/*
 * Set the battery type, when auto-detection cannot be used.
 *
 * @param type	Battery type
 */
void battery_set_fixed_battery_type(int type);
#endif

/**
 * Return the board-specific default battery type.
 *
 * @return a value of `enum battery_type`.
 */
__override_proto int board_get_default_battery_type(void);

/**
 * Detect a battery model.
 */
void init_battery_type(void);

/**
 * Return struct board_batt_params of the battery.
 */
const struct board_batt_params *get_batt_params(void);

/**
 * Return 1 if CFET is disabled, 0 if enabled. -1 if an error was encountered.
 * If the CFET mask is not defined, it will return 0.
 */
int battery_is_charge_fet_disabled(void);

/**
 * Send the fuel gauge sleep command through SMBus.
 *
 * @return	0 if successful, non-zero if error occurred
 */
enum ec_error_list battery_sleep_fuel_gauge(void);

/**
 * Return whether BCIC is enabled or not.
 *
 * This is a callback used by boards which share the same FW but need to enable
 * BCIC for one board and disable it for another. This is needed because without
 * this callback, BCIC can't tell battery config is missing because it's an old
 * unit or because the default config is applicable.
 *
 * @return true if board supports BCIC or false otherwise.
 */
__override_proto bool board_batt_conf_enabled(void);

/**
 * Report the absolute difference between the highest and lowest cell voltage in
 * the battery pack, in millivolts.  On error or unimplemented, returns '0'.
 */
__override_proto int
board_battery_imbalance_mv(const struct board_batt_params *info);

#endif /* __CROS_EC_BATTERY_FUEL_GAUGE_H */

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

struct ship_mode_info {
	uint8_t reg_addr;
	uint8_t reserved;
	uint16_t reg_data[SHIP_MODE_WRITES];
} __packed __aligned(2);

struct sleep_mode_info {
	uint8_t reg_addr;
	uint8_t reserved;
	uint16_t reg_data;
} __packed __aligned(2);

struct fet_info {
	uint8_t reg_addr;
	uint8_t reserved;
	uint16_t reg_mask;
	uint16_t disconnect_val;
	uint16_t cfet_mask; /* CHG FET status mask */
	uint16_t cfet_off_val;
} __packed __aligned(2);

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
#if defined(__x86_64__) && !defined(TEST_BUILD)
	/* These shouldn't be used on the (__x86_64__) host. */
	uint32_t reserved[2];
#else
	char *manuf_name;
	char *device_name;
#endif
	uint32_t flags;
	uint32_t board_flags;
	struct ship_mode_info ship_mode;
	struct sleep_mode_info sleep_mode;
	struct fet_info fet;
} __packed __aligned(4);

struct board_batt_params {
	struct fuel_gauge_info fuel_gauge;
	struct battery_info batt_info;
} __packed __aligned(4);

struct batt_conf_header {
	/* Version of struct batt_conf_header and its internals. */
	uint8_t struct_version;
	uint8_t reserved[3];
	char manuf_name[16];
	char device_name[16];
	struct board_batt_params config;
} __packed __aligned(4);

/* Forward declare board specific data used by common code */
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

/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery fuel gauge parameters
 */

#ifndef __CROS_EC_BATTERY_FUEL_GAUGE_H
#define __CROS_EC_BATTERY_FUEL_GAUGE_H

#include "battery.h"
#include <stdbool.h>

/* Number of writes needed to invoke battery cutoff command */
#define SHIP_MODE_WRITES 2

/* When battery type is not initialized */
#define BATTERY_TYPE_UNINITIALIZED -1

struct ship_mode_info {
	/*
	 * Write Block Support. If wb_support is true, then we use a i2c write
	 * block command instead of a 16-bit write. The effective difference is
	 * that the i2c transaction will prefix the length (2) when wb_support
	 * is enabled.
	 */
	const uint8_t wb_support;
	const uint8_t reg_addr;
	const uint16_t reg_data[SHIP_MODE_WRITES];
};

struct sleep_mode_info {
	const bool sleep_supported;
	const uint8_t reg_addr;
	const uint16_t reg_data;
};

struct fet_info {
	const int mfgacc_support;
	const uint8_t reg_addr;
	const uint16_t reg_mask;
	const uint16_t disconnect_val;
	const uint16_t cfet_mask; /* CHG FET status mask */
	const uint16_t cfet_off_val;
};

struct fuel_gauge_info {
	const char *manuf_name;
	const char *device_name;
	const uint8_t override_nil;
	const struct ship_mode_info ship_mode;
	const struct sleep_mode_info sleep_mode;
	const struct fet_info fet;

#ifdef CONFIG_BATTERY_MEASURE_IMBALANCE
	/* See battery_*_imbalance_mv() for functions which are suitable. */
	int (*imbalance_mv)(void);
#endif
};

struct board_batt_params {
	const struct fuel_gauge_info fuel_gauge;
	const struct battery_info batt_info;
};

/* Forward declare board specific data used by common code */
extern const struct board_batt_params board_battery_info[];
extern const enum battery_type DEFAULT_BATTERY_TYPE;

#ifdef CONFIG_BATTERY_MEASURE_IMBALANCE
/**
 * Report the absolute difference between the highest and lowest cell voltage in
 * the battery pack, in millivolts.  On error or unimplemented, returns '0'.
 */
int battery_default_imbalance_mv(void);

#ifdef CONFIG_BATTERY_BQ4050
int battery_bq4050_imbalance_mv(void);
#endif

#endif

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
 * Return 1 if CFET is disabled, 0 if enabled. -1 if an error was encountered.
 * If the CFET mask is not defined, it will return 0.
 */
int battery_is_charge_fet_disabled(void);

/**
 * Battery cut off command via SMBus write block.
 *
 * @param ship_mode		Battery ship mode information
 * @return non-zero if error
 */
int cut_off_battery_block_write(const struct ship_mode_info *ship_mode);

/**
 * Battery cut off command via SMBus write word.
 *
 * @param ship_mode		Battery ship mode information
 * @return non-zero if error
 */
int cut_off_battery_sb_write(const struct ship_mode_info *ship_mode);

/**
 * Send the fuel gauge sleep command through SMBus.
 *
 * @return	0 if successful, non-zero if error occurred
 */
enum ec_error_list battery_sleep_fuel_gauge(void);

#endif /* __CROS_EC_BATTERY_FUEL_GAUGE_H */

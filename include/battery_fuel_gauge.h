/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery fuel gauge parameters
 */

#ifndef __CROS_EC_BATTERY_FUEL_GAUGE_H
#define __CROS_EC_BATTERY_FUEL_GAUGE_H

#include "battery.h"

/* Number of writes needed to invoke battery cutoff command */
#define SHIP_MODE_WRITES 2

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

#endif /* __CROS_EC_BATTERY_FUEL_GAUGE_H */

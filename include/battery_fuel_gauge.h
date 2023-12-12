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
#include "ec_commands.h"

#include <stdbool.h>

/**
 * Represent a battery config embedded in FW.
 */
struct batt_conf_embed {
	char *manuf_name;
	char *device_name;
	struct board_batt_params config;
};

/* Forward declare board specific data used by common code */
extern const struct batt_conf_embed board_battery_info[];
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
 * Return pointer to active battery config.
 */
const struct batt_conf_embed *get_batt_conf(void);

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

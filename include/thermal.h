/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Thermal engine module for Chrome EC */

#ifndef __CROS_EC_THERMAL_H
#define __CROS_EC_THERMAL_H

/* The thermal configuration for a single temp sensor is defined here. */
#include "ec_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/* We need to to hold a config for each board's sensors. Not const, so we can
 * tweak it at run-time if we have to.
 */
extern struct ec_thermal_config thermal_params[];

/* Helper function to compute percent cooling */
int thermal_fan_percent(int low, int high, int cur);

/* Allow board custom fan control. Called after reading temperature sensors.
 *
 * @param fan Fan ID to control (0 to CONFIG_FANS)
 * @param tmp Array of temperatures (C) for each temperature sensor (size
 *            TEMP_SENSOR_COUNT)
 */
void board_override_fan_control(int fan, int *tmp);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_THERMAL_H */

/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Thermal engine module for Chrome EC */

#ifndef __CROS_EC_THERMAL_H
#define __CROS_EC_THERMAL_H

/* The thermal configuration for a single temp sensor is defined here. */
#include "ec_commands.h"

/* We need to to hold a config for each board's sensors. Not const, so we can
 * tweak it at run-time if we have to.
 */
extern struct ec_thermal_config thermal_params[];

/* Helper function to compute percent cooling */
int thermal_fan_percent(int low, int high, int cur);

#endif  /* __CROS_EC_THERMAL_H */

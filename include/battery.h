/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery charging parameters and constraints
 */

#ifndef __CROS_EC_BATTERY_H
#define __CROS_EC_BATTERY_H

/* Stop charge when charging and battery level >= this percentage */
#define BATTERY_LEVEL_FULL		100

/* Tell host we're charged when battery level >= this percentage */
#define BATTERY_LEVEL_NEAR_FULL		 97

/*
 * Send battery-low host event when discharging and battery level <= this level
 */
#define BATTERY_LEVEL_LOW		 10

/*
 * Send battery-critical host event when discharging and battery level <= this
 * level.
 */
#define BATTERY_LEVEL_CRITICAL		  5

/*
 * Shut down main processor and/or hibernate EC when discharging and battery
 * level < this level.
 */
#define BATTERY_LEVEL_SHUTDOWN		  3

#endif /* __CROS_EC_BATTERY_H */


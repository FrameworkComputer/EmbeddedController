/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery charging parameters and constraints
 */

#ifndef __CROS_EC_BATTERY_H
#define __CROS_EC_BATTERY_H

/* Design capacities, percentage */
#define BATTERY_LEVEL_WARNING  15
#define BATTERY_LEVEL_LOW      10
#define BATTERY_LEVEL_CRITICAL 5
#define BATTERY_LEVEL_SHUTDOWN 3

/* Stop charge when state of charge reaches this percentage */
#define STOP_CHARGE_THRESHOLD 100
/* Threshold for power led to turn green */
#define POWERLED_GREEN_THRESHOLD 90

/* Define the lightbar thresholds, as though we care. */
#define LIGHTBAR_POWER_THRESHOLD_BLUE   90
#define LIGHTBAR_POWER_THRESHOLD_GREEN  40
#define LIGHTBAR_POWER_THRESHOLD_YELLOW 10
/* Red below 10% */


#endif /* __CROS_EC_BATTERY_H */


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
/* Shut down main processor when battery level reaches this level */
#define BATTERY_LEVEL_SHUTDOWN 3
/* Hibernate EC immediately when battery level reaches this level */
#define BATTERY_LEVEL_HIBERNATE_EC 2

/* Stop charge when state of charge reaches this percentage */
#define STOP_CHARGE_THRESHOLD 100
/* Tell host we're charged at this percentage */
#define NEAR_FULL_THRESHOLD 97
/* Precharge only when state of charge is below this level */
#define PRE_CHARGE_THRESHOLD 25

#endif /* __CROS_EC_BATTERY_H */


/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#ifndef __CROS_EC_BATTERY_SMART_H
#define __CROS_EC_BATTERY_SMART_H

#include "battery.h"

/*
 * Physical detection of battery.
 */
__override_proto enum battery_present battery_check_present_status(void);

extern enum battery_present batt_pres_prev;

#endif  /* __CROS_EC_BATTERY_SMART_H */

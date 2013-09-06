/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __THERMAL_FALCO_EXTERNS_H
#define __THERMAL_FALCO_EXTERNS_H

/* Normally private symbols from the modules that we're testing. */
extern struct adapter_limits
	ad_limits[][NUM_AC_TURBO_STATES][NUM_AC_THRESHOLDS];
extern int ap_is_throttled;
extern struct adapter_limits batt_limits[NUM_BATT_THRESHOLDS];

#endif	/* __THERMAL_FALCO_EXTERNS_H */

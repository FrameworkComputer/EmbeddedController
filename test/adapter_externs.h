/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ADAPTER_EXTERNS_H
#define __ADAPTER_EXTERNS_H

/* Normally private symbols from the modules that we're testing. */

extern enum adapter_type ac_adapter;
extern struct adapter_id_vals ad_id_vals[];
extern struct adapter_limits
	ad_limits[][NUM_AC_TURBO_STATES][NUM_AC_THRESHOLDS];
extern int ac_turbo;
extern int ap_is_throttled;
extern void check_threshold(int current, struct adapter_limits *lim);
extern struct adapter_limits batt_limits[NUM_BATT_THRESHOLDS];
extern void watch_battery_closely(struct power_state_context *ctx);

#endif	/* __ADAPTER_EXTERNS_H */

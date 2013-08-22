/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* We can do even smarter charging if we can identify the AC adapter */

#ifndef __CROS_EC_EXTPOWER_FALCO_H
#define __CROS_EC_EXTPOWER_FALCO_H

#include "charge_state.h"

/* Supported adapters */
enum adapter_type {
	ADAPTER_UNKNOWN = 0,
	ADAPTER_45W,
	ADAPTER_65W,
	ADAPTER_90W,
	NUM_ADAPTER_TYPES
};

/* Adapter identification values */
struct adapter_id_vals {
	int lo, hi;
};

/* Adapter-specific parameters. */
struct adapter_limits {
	int hi_val, lo_val;			/* current thresholds (mA) */
	int hi_cnt, lo_cnt;			/* count needed to trigger */
	int count;				/* samples past the limit */
	int triggered;				/* threshold reached */
};

/* Rate at which adapter samples are collected. */
#define EXTPOWER_FALCO_POLL_PERIOD  (MSEC * 100)

/* Number of special states */
#define NUM_AC_TURBO_STATES 2
#define NUM_AC_THRESHOLDS 2
#define NUM_BATT_THRESHOLDS 2

/* Change turbo mode or throttle the AP depending on the adapter state. */
void watch_adapter_closely(struct power_state_context *ctx);

#endif  /* __CROS_EC_EXTPOWER_FALCO_H */

/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PECI based cpu power governer
 */

#ifndef __CROS_EC_CPU_POWER_H
#define __CROS_EC_CPU_POWER_H

#define SB_RMI_WRITE_SUSTAINED_POWER_LIMIT_CMD  0x30
#define SB_RMI_WRITE_FAST_PPT_LIMIT_CMD 0x31
#define SB_RMI_WRITE_SLOW_PPT_LIMIT_CMD 0x32
#define SB_RMI_WRITE_P3T_LIMIT_CMD 0x3C

enum power_limit_type {
    TYPE_SPL = 0,
	TYPE_SPPT,
    TYPE_FPPT,
	TYPE_P3T,
	TYPE_COUNT,
};

enum power_limit_function {
    FUNCTION_DEFAULT = 0,
	FUNCTION_SLIDER = 0,
    FUNCTION_SAFETY,
	FUNCTION_POWER,
    FUNCTION_THERMAL,
	FUNCTION_COUNT,
};

struct power_limit_details {
    int mwatt[TYPE_COUNT];
} __ec_align1;

#define BATTERY_55mW 55000
#define BATTERY_61mW 61000
#define POWER_DELTA 20000

void update_soc_power_limit(bool force_update, bool force_no_adapter);

#endif	/* __CROS_EC_CPU_POWER_H */

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

#define BATTERY_TYPE_55W 0
#define BATTERY_TYPE_61W 1

struct power_limit_details {
    uint32_t spl_mwatt;
    uint32_t sppt_mwatt;
    uint32_t fppt_mwatt;
    uint32_t p3t_mwatt;
} __ec_align1;

enum power_limit_function {
    FUNCTION_DEFAULT = 0,
	FUNCTION_SAFETY,
	FUNCTION_SLIDER,
	FUNCTION_POWER,
    FUNCTION_THERMAL,
	FUNCTION_COUNT,
};

void update_soc_power_limit(bool force_update, bool force_no_adapter);
void update_os_power_slider(void);
void update_safety_power_limit(void);

#endif	/* __CROS_EC_CPU_POWER_H */

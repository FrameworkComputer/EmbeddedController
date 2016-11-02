/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel X86 chipset power control module for Chrome EC */


#ifndef __CROS_EC_INTEL_X86_H
#define __CROS_EC_INTEL_X86_H

#include "power.h"

/**
 * Handle RSMRST signal.
 *
 * @param state Current chipset state.
 */
void handle_rsmrst(enum power_state state);

/**
 * Force chipset to G3 state.
 */
void chipset_force_g3(void);

/**
 * Wait for S5 exit and then attempt RTC reset.
 *
 * @return power_state New chipset state.
 */
enum power_state power_wait_s5_rtc_reset(void);

#endif /* __CROS_EC_INTEL_X86_H */

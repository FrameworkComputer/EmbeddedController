/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CHIPSET_STATE_CHECK_H__
#define __CHIPSET_STATE_CHECK_H__

#include "chipset.h"
#include "ap_power/ap_power_interface.h"

BUILD_ASSERT((int)AP_POWER_STATE_HARD_OFF == (int)CHIPSET_STATE_HARD_OFF);
BUILD_ASSERT((int)AP_POWER_STATE_SOFT_OFF == (int)CHIPSET_STATE_SOFT_OFF);
BUILD_ASSERT((int)AP_POWER_STATE_SUSPEND == (int)CHIPSET_STATE_SUSPEND);
BUILD_ASSERT((int)AP_POWER_STATE_ON == (int)CHIPSET_STATE_ON);
BUILD_ASSERT((int)AP_POWER_STATE_STANDBY == (int)CHIPSET_STATE_STANDBY);
BUILD_ASSERT((int)AP_POWER_STATE_ANY_OFF == (int)CHIPSET_STATE_ANY_OFF);
BUILD_ASSERT((int)AP_POWER_STATE_ANY_SUSPEND == (int)CHIPSET_STATE_ANY_SUSPEND);

#endif /* __CHIPSET_STATE_CHECK_H__ */

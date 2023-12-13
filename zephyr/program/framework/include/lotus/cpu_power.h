/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CPU_POWER_H__
#define __CROS_EC_CPU_POWER_H__

/**
 * Check if safety level(LEVEL_TYPEC_1_5A) is triggered.
 *
 * @param none
 * @return true:safety level(LEVEL_TYPEC_1_5A) is triggered
 *              all sink port need to be forced to 1.5A
 *         false:safety level(LEVEL_TYPEC_1_5A) is not trigger
 */
bool safety_force_typec_1_5A(void);

#endif /* __CROS_EC_CPU_POWER_H__ */

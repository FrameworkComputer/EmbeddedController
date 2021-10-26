/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_POWER_MT8192_H_
#define __CROS_EC_POWER_MT8192_H_

#ifndef CONFIG_ZEPHYR

enum power_signal {
	PMIC_PWR_GOOD,
	AP_IN_S3_L,
	AP_WDT_ASSERTED,
	POWER_SIGNAL_COUNT,
};

#endif /* !CONFIG_ZEPHYR */

#endif /* __CROS_EC_POWER_MT8192_H_ */

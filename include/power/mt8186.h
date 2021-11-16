/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_POWER_MT8186_H_
#define __CROS_EC_POWER_MT8186_H_

enum power_signal {
	AP_IN_RST,
	AP_IN_S3,
	AP_WDT_ASSERTED,
	AP_WARM_RST_REQ,
	POWER_SIGNAL_COUNT,
};

#endif /* __CROS_EC_POWER_MT8186_H_ */

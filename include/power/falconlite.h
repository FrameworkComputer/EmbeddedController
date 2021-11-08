/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_POWER_FALCONLITE_H_
#define __CROS_EC_POWER_FALCONLITE_H_

enum power_signal_falconlite {
	FCL_AP_WARM_RST_REQ,
	FCL_AP_SHUTDOWN_REQ,
	FCL_AP_WATCHDOG,
	FCL_PG_S5,
	FCL_PG_VDD1_VDD2,
	FCL_PG_VDD_MEDIA_ML,
	FCL_PG_VDD_SOC,
	FCL_PG_VDD_DDR_OD,
	POWER_SIGNAL_COUNT,
};

#endif /* __CROS_EC_POWER_FALCONLITE_H_ */

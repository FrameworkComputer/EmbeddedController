/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_POWER_SC7180_H_
#define __CROS_EC_POWER_SC7180_H_

#ifdef CONFIG_CHIPSET_SC7180
enum power_signal {
	SC7180_AP_RST_ASSERTED = 0,
	SC7180_PS_HOLD,
	SC7180_POWER_GOOD,
	SC7180_WARM_RESET,
	SC7180_AP_SUSPEND,
	SC7180_DEPRECATED_AP_RST_REQ,
	POWER_SIGNAL_COUNT,
};
#endif

/* Swithcap functions */
void board_set_switchcap_power(int enable);
int board_is_switchcap_enabled(void);
int board_is_switchcap_power_good(void);

#endif /* __CROS_EC_POWER_SC7180_H_ */

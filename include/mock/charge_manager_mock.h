/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Controls for the mock charge_manager
 */

#ifndef __MOCK_CHARGE_MANAGER_MOCK_H
#define __MOCK_CHARGE_MANAGER_MOCK_H

struct mock_ctrl_charge_manager {
	int vbus_voltage_mv;
};

#define MOCK_CTRL_DEFAULT_CHARGE_MANAGER    \
	((struct mock_ctrl_charge_manager) { \
		.vbus_voltage_mv = 0,       \
	})

extern struct mock_ctrl_charge_manager mock_ctrl_charge_manager;

void mock_charge_manager_set_vbus_voltage(int voltage_mv);

#endif /* __MOCK_CHARGE_MANAGER_MOCK_H */

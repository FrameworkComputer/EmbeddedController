/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Mock charge_manager
 */

#include <stdlib.h>

#include "charge_manager.h"
#include "common.h"
#include "mock/charge_manager_mock.h"

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

void charge_manager_update_dualrole(int port, enum dualrole_capabilities cap)
{
}

void charge_manager_set_ceil(int port, enum ceil_requestor requestor, int ceil)
{
}

int charge_manager_get_selected_charge_port(void)
{
	return 0;
}

int charge_manager_get_active_charge_port(void)
{
	return 0;
}

int charge_manager_get_vbus_voltage(int port)
{
	return mock_ctrl_charge_manager.vbus_voltage_mv;
}

void mock_charge_manager_set_vbus_voltage(int voltage_mv)
{
	mock_ctrl_charge_manager.vbus_voltage_mv = voltage_mv;
}

struct mock_ctrl_charge_manager mock_ctrl_charge_manager =
MOCK_CTRL_DEFAULT_CHARGE_MANAGER;

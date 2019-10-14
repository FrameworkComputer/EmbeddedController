/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Fake BATTERY module.
 */
#include "battery.h"
#include "common.h"

int battery_design_voltage(int *voltage)
{
	*voltage = 0;
	return 0;
}

enum battery_present battery_is_present(void)
{
	return BP_NO;
}

int battery_design_capacity(int *capacity)
{
	*capacity = 0;
	return 0;
}

int battery_full_charge_capacity(int *capacity)
{
	*capacity = 0;
	return 0;
}

int battery_remaining_capacity(int *capacity)
{
	*capacity = 0;
	return 0;
}

int battery_status(int *status)
{
	return EC_ERROR_UNIMPLEMENTED;
}

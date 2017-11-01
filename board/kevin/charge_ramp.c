/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Board-specific charge ramp callbacks. */

#include "common.h"

#include "bd9995x.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "system.h"

/**
 * Return true if VBUS is sagging too low
 */
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	return charger_get_vbus_voltage(port) < BD9995X_BC12_MIN_VOLTAGE;
}

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
 * Return true if ramping is allowed for given supplier
 */
int board_is_ramp_allowed(int supplier)
{
	/* Don't allow ramping in RO when write protected */
	if (!system_is_in_rw() && system_is_locked())
		return 0;
	else
		return supplier == CHARGE_SUPPLIER_BC12_DCP ||
		       supplier == CHARGE_SUPPLIER_BC12_SDP ||
		       supplier == CHARGE_SUPPLIER_BC12_CDP ||
		       supplier == CHARGE_SUPPLIER_PROPRIETARY;
}

/**
 * Return true if VBUS is sagging too low
 */
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	return charger_get_vbus_voltage(port) < BD9995X_BC12_MIN_VOLTAGE;
}

/**
 * Return the maximum allowed input current
 */
int board_get_ramp_current_limit(int supplier, int sup_curr)
{
	return bd9995x_get_bc12_ilim(supplier);
}

/**
 * Return if board is consuming full amount of input current
 */
int board_is_consuming_full_charge(void)
{
	int chg_pct = charge_get_percent();

	return chg_pct > 2 && chg_pct < 95;
}

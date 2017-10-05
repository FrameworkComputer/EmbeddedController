/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Charge input current limit ramp module for Chrome EC */

#include "common.h"
#include "system.h"
#include "usb_charge.h"

test_mockable int chg_ramp_allowed(int supplier)
{
	/* Don't allow ramping in RO when write protected. */
	if (!system_is_in_rw() && system_is_locked())
		return 0;

	/* Othewise ask the BC1.2 detect module */
	return usb_charger_ramp_allowed(supplier);
}

test_mockable int chg_ramp_max(int supplier, int sup_curr)
{
	/* Ask the BC1.2 detect module */
	return usb_charger_ramp_max(supplier, sup_curr);
}

/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Charge input current limit ramp module for Chrome EC */

#include "charge_manager.h"
#include "common.h"
#include "system.h"
#include "usb_charge.h"
#include "util.h"

#define TYPEC_DTS_RAMP_MAX 2400

test_mockable int chg_ramp_allowed(int supplier)
{
	/* Don't allow ramping in RO when write protected. */
	if (!system_is_in_rw() && system_is_locked())
		return 0;

	switch (supplier) {
	case CHARGE_SUPPLIER_TYPEC_DTS:
#ifdef CONFIG_CHARGE_RAMP_HW
	/* Need ramping for USB-C chargers as well to avoid voltage droops. */
	case CHARGE_SUPPLIER_PD:
	case CHARGE_SUPPLIER_TYPEC:
#endif
		return 1;
	/* default: fall through */
	}

	/* Othewise ask the BC1.2 detect module */
	return usb_charger_ramp_allowed(supplier);
}

test_mockable int chg_ramp_max(int supplier, int sup_curr)
{
	switch (supplier) {
	case CHARGE_SUPPLIER_TYPEC_DTS:
		/*
		 * Ramp DTS suppliers to advertised current or predetermined
		 * limit, whichever is greater.
		 */
		return MAX(TYPEC_DTS_RAMP_MAX, sup_curr);
#ifdef CONFIG_CHARGE_RAMP_HW
	case CHARGE_SUPPLIER_PD:
	case CHARGE_SUPPLIER_TYPEC:
		return sup_curr;
#endif
	/* default: fall through */
	}

	/* Otherwise ask the BC1.2 detect module */
	return usb_charger_ramp_max(supplier, sup_curr);
}

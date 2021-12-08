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

test_mockable int chg_ramp_allowed(int port, int supplier)
{
	/* Don't allow ramping in RO when write protected. */
	if (!system_is_in_rw() && system_is_locked())
		return 0;

	switch (supplier) {
	/*
	 * Use ramping for USB-C DTS suppliers (debug accessory eg suzy-q).
	 * The suzy-q simply passes through the VBUS. The power supplier behind
	 * may be a SDP/CDP which requires ramping.
	 */
	case CHARGE_SUPPLIER_TYPEC_DTS:
		return 1;
	/*
	 * Don't regulate the input voltage for USB-C chargers. It is
	 * unnecessary as the USB-C compliant adapters should never trigger it
	 * active.
	 *
	 * The USB-C spec defines their load curves should not be below
	 * 4.75V @0A and 4V @3A. We can't define the voltage regulation value
	 * higher than 4V since it limits the current reaching its max 3A. If
	 * we define the voltage regulation value lower than 4V, their load
	 * curves will never be below the voltage regulation line.
	 *
	 * Check go/charge_ramp_typec for detail.
	 */
	case CHARGE_SUPPLIER_PD:
	case CHARGE_SUPPLIER_TYPEC:
		return 0;
	/* default: fall through */
	}

	/* Otherwise ask the BC1.2 detect module */
	return usb_charger_ramp_allowed(port, supplier);
}

test_mockable int chg_ramp_max(int port, int supplier, int sup_curr)
{
	switch (supplier) {
	case CHARGE_SUPPLIER_PD:
	case CHARGE_SUPPLIER_TYPEC:
	case CHARGE_SUPPLIER_TYPEC_DTS:
		/*
		 * We should not ramp DTS beyond what they advertise, otherwise
		 * we may brownout the systems they are connected to.
		 */
		return sup_curr;
	/* default: fall through */
	}

	/* Otherwise ask the BC1.2 detect module */
	return usb_charger_ramp_max(port, supplier, sup_curr);
}

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
	case CHARGE_SUPPLIER_TYPEC_DTS:
#ifdef CONFIG_CHARGE_RAMP_HW
	/* Need ramping for USB-C chargers as well to avoid voltage droops. */
	case CHARGE_SUPPLIER_PD:
	case CHARGE_SUPPLIER_TYPEC:
#endif
		return 1;
	/* default: fall through */
	}

	/* Otherwise ask the BC1.2 detect module */
	return usb_charger_ramp_allowed(port, supplier);
}

test_mockable int chg_ramp_max(int port, int supplier, int sup_curr)
{
	switch (supplier) {
#ifdef CONFIG_CHARGE_RAMP_HW
	case CHARGE_SUPPLIER_PD:
	case CHARGE_SUPPLIER_TYPEC:
#endif
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

/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "usb_pd.h"
#include "usb_pd_pdo.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

test_mockable int pd_set_power_supply_ready(int port)
{
	/* Not implemented */
	return EC_SUCCESS;
}

test_mockable void pd_power_supply_reset(int port)
{
	/* Not implemented */
}

__overridable void pd_set_input_current_limit(int port, uint32_t max_ma,
					      uint32_t supply_voltage)
{
	/* Not implemented */
}

test_mockable int pd_snk_is_vbus_provided(int port)
{
	/* Not implemented */
	return 1;
}

__override int pd_check_power_swap(int port)
{
	/* Always allow power swap */
	return 1;
}

__override int pd_check_data_swap(int port,
				  enum pd_data_role data_role)
{
	/* Always allow data swap */
	return 1;
}

__override void pd_check_pr_role(int port,
				 enum pd_power_role pr_role,
				 int flags)
{
}

__override void pd_check_dr_role(int port,
				 enum pd_data_role dr_role,
				 int flags)
{
}

__override int pd_custom_vdm(int port, int cnt, uint32_t *payload,
			     uint32_t **rpayload)
{
	return 0;
}

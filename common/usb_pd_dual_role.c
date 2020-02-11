/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Dual Role (Source & Sink) USB-PD module.
 */

#include "usb_pd.h"

#if defined(PD_MAX_VOLTAGE_MV) && defined(PD_OPERATING_POWER_MW)
/*
 * As a sink, this is the max voltage (in millivolts) we can request
 * before getting source caps
 */
static unsigned int max_request_mv = PD_MAX_VOLTAGE_MV;

void pd_set_max_voltage(unsigned int mv)
{
	max_request_mv = mv;
}

unsigned int pd_get_max_voltage(void)
{
	return max_request_mv;
}
#endif /* defined(PD_MAX_VOLTAGE_MV) && defined(PD_OPERATING_POWER_MW) */

int pd_charge_from_device(uint16_t vid, uint16_t pid)
{
	/* TODO: rewrite into table if we get more of these */
	/*
	 * White-list Apple charge-through accessory since it doesn't set
	 * unconstrained bit, but we still need to charge from it when
	 * we are a sink.
	 */
	return (vid == USB_VID_APPLE &&
		(pid == USB_PID1_APPLE || pid == USB_PID2_APPLE));
}

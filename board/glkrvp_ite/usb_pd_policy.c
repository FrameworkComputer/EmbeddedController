/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "stddef.h"
#include "system.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

int pd_set_power_supply_ready(int port)
{
	/* Disable charging */
	board_charging_enable(port, 0);

	/* Provide VBUS */
	board_vbus_enable(port, 1);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Disable VBUS */
	board_vbus_enable(port, 0);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_check_vconn_swap(int port)
{
	/* in G3, do not allow vconn swap since pp5000_A rail is off */
	/* TODO: return gpio_get_level(GPIO_PMIC_EN); */
	return 1;
}

void pd_execute_data_swap(int port,
			  enum pd_data_role data_role)
{
	/* Do nothing */
}

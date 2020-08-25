/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "charger.h"
#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "driver/charger/rt946x.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

static uint8_t vbus_en;

int board_vbus_source_enabled(int port)
{
	return vbus_en;
}

int pd_set_power_supply_ready(int port)
{

	pd_set_vbus_discharge(port, 0);
	/* Provide VBUS */
	vbus_en = 1;
	charger_enable_otg_power(CHARGER_SOLO, 1);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	prev_en = vbus_en;
	/* Disable VBUS */
	vbus_en = 0;
	charger_enable_otg_power(CHARGER_SOLO, 0);
	/* Enable discharge if we were previously sourcing 5V */
	if (prev_en)
		pd_set_vbus_discharge(port, 1);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_check_vconn_swap(int port)
{
	/*
	 * VCONN is provided directly by the battery (PPVAR_SYS)
	 * but use the same rules as power swap.
	 */
	return pd_get_dual_role(port) == PD_DRP_TOGGLE_ON ? 1 : 0;
}

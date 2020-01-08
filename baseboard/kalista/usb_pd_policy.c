/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "extpower.h"
#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "driver/tcpm/anx74xx.h"
#include "driver/tcpm/ps8xxx.h"
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

#define PDO_FIXED_FLAGS (PDO_FIXED_UNCONSTRAINED | \
			 PDO_FIXED_DATA_SWAP | \
			 PDO_FIXED_COMM_CAP)

const uint32_t pd_src_pdo[] = {
	PDO_FIXED(5000, 3000, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

int board_vbus_source_enabled(int port)
{
	if (port != 0)
		return 0;
	return gpio_get_level(GPIO_USB_C0_5V_EN);
}

int pd_set_power_supply_ready(int port)
{
	/* Enable VBUS source */
	gpio_set_level(GPIO_USB_C0_5V_EN, 1);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Disable VBUS source */
	gpio_set_level(GPIO_USB_C0_5V_EN, 0);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_snk_is_vbus_provided(int port)
{
	return !gpio_get_level(GPIO_USB_C0_VBUS_WAKE_L);
}

__override void pd_check_pr_role(int port,
				 enum pd_power_role pr_role,
				 int flags)
{
}

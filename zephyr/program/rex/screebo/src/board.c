/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* screebo board-specific configuration */

#include "hooks.h"
#include "usb_charge.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_ERR);

#define USBA_PORT_A0 0

void shutdown_usb_a_deferred(void)
{
	usb_charge_set_mode(USBA_PORT_A0, USB_CHARGE_MODE_DISABLED,
			    USB_ALLOW_SUSPEND_CHARGE);
}
DECLARE_DEFERRED(shutdown_usb_a_deferred);

void board_usb_port_startup(void)
{
	/* Turn USB ports on as we go into S0 from S5. */
	hook_call_deferred(&shutdown_usb_a_deferred_data, -1);
	usb_charge_set_mode(USBA_PORT_A0, USB_CHARGE_MODE_ENABLED,
			    USB_ALLOW_SUSPEND_CHARGE);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_usb_port_startup, HOOK_PRIO_DEFAULT);

void board_usb_port_shutdown(void)
{
	/* Turn USB ports off as we go back to S5. */
	hook_call_deferred(&shutdown_usb_a_deferred_data, 2 * SECOND);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_usb_port_shutdown, HOOK_PRIO_DEFAULT);

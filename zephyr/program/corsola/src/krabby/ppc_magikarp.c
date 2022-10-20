/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Tentacruel PPC/BC12 (mixed RT1739 or PI3USB9201+SYV682X) configuration */

#include "baseboard_usbc_config.h"
#include "console.h"
#include "cros_board_info.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usbc/ppc.h"
#include "variant_db_detection.h"

#include <zephyr/logging/log.h>

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

void bc12_interrupt(enum gpio_signal signal)
{
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

static void board_usbc_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc));

	/* Enable BC1.2 interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_bc12));
}
DECLARE_HOOK(HOOK_INIT, board_usbc_init, HOOK_PRIO_POST_DEFAULT);

void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_SIGNAL(DT_NODELABEL(usb_c0_ppc_int_odl))) {
		syv682x_interrupt(0);
	} else if (signal == GPIO_SIGNAL(DT_ALIAS(gpio_usb_c1_ppc_int_odl))) {
		syv682x_interrupt(1);
	}
}

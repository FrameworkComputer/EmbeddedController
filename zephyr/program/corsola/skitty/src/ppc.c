/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Skitty configuration */

#include "baseboard_usbc_config.h"
#include "cros_board_info.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usb_mux.h"
#include "usbc/ppc.h"

#include <zephyr/logging/log.h>
#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

static void board_usbc_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc));
}
DECLARE_HOOK(HOOK_INIT, board_usbc_init, HOOK_PRIO_POST_DEFAULT);

void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_SIGNAL(DT_NODELABEL(usb_c0_ppc_int_odl))) {
		ppc_chips[0].drv->interrupt(0);
	}
}

/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "baseboard_usbc_config.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "system.h"
#include "usb_mux.h"
#include "usbc/ppc.h"

static void board_usbc_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc_bc12));
}
DECLARE_HOOK(HOOK_INIT, board_usbc_init, HOOK_PRIO_POST_DEFAULT);

void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_SIGNAL(DT_NODELABEL(usb_c0_ppc_bc12_int_odl))) {
		ppc_chips[0].drv->interrupt(0);
	}
	if (signal == GPIO_SIGNAL(DT_ALIAS(gpio_usb_c1_ppc_int_odl))) {
		ppc_chips[1].drv->interrupt(1);
	}
}

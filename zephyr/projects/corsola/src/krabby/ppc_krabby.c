/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Krabby PPC/BC12 (RT1739) configuration */

#include "baseboard_usbc_config.h"
#include "gpio/gpio_int.h"
#include "driver/ppc/rt1739.h"
#include "driver/ppc/syv682x.h"
#include "hooks.h"
#include "variant_db_detection.h"

void c0_bc12_interrupt(enum gpio_signal signal)
{
	rt1739_interrupt(0);
}

static void board_usbc_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc_bc12));
}
DECLARE_HOOK(HOOK_INIT, board_usbc_init, HOOK_PRIO_POST_DEFAULT);

void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_SIGNAL(DT_ALIAS(gpio_usb_c1_ppc_int_odl))) {
		syv682x_interrupt(1);
	}
}

/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio/gpio_int.h"
#include "usbc_ppc.h"

#include <zephyr/init.h>

static int board_usbc_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc_bc12));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_ppc));

	return 0;
}
SYS_INIT(board_usbc_init, APPLICATION, 1);

void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_SIGNAL(DT_NODELABEL(usb_c0_ppc_bc12_int_odl))) {
		ppc_chips[0].drv->interrupt(0);
	}
	if (signal == GPIO_SIGNAL(DT_NODELABEL(usb_c1_ppc_int_odl))) {
		ppc_chips[1].drv->interrupt(1);
	}
}

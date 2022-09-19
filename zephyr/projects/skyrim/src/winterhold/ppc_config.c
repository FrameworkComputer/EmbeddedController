/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Winterhold board-specific PPC code */

#include <zephyr/drivers/gpio.h>

#include "driver/ppc/nx20p348x.h"
#include "usbc_ppc.h"

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		nx20p348x_interrupt(0);
		break;

	case GPIO_USB_C1_PPC_INT_ODL:
		nx20p348x_interrupt(1);
		break;

	default:
		break;
	}
}

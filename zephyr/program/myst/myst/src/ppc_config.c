/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Myst board-specific PPC code */

#include "driver/ppc/aoz1380_public.h"
#include "driver/ppc/nx20p348x.h"
#include "usbc_ppc.h"

#include <zephyr/drivers/gpio.h>

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		break;

	case GPIO_USB_C1_PPC_INT_ODL:
		break;

	default:
		break;
	}
}

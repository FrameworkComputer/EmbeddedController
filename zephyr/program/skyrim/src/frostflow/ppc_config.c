/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Frostflow board-specific PPC code */

#include <zephyr/drivers/gpio.h>

#include "driver/ppc/nx20p348x.h"
#include "driver/ppc/aoz1380_public.h"
#include "usbc_ppc.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/*
 * In the AOZ1380 PPC, there are no programmable features.  We use
 * the attached NCT3807 to control a GPIO to indicate 1A5 or 3A0
 * current limits.
 */
int board_aoz1380_set_vbus_source_current_limit(int port, enum tcpc_rp_value rp)
{
	int rv = EC_SUCCESS;

	rv = gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(ioex_usb_c0_ilim_3a_en),
			     (rp == TYPEC_RP_3A0) ? 1 : 0);

	return rv;
}

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		aoz1380_interrupt(0);
		break;

	case GPIO_USB_C1_PPC_INT_ODL:
		nx20p348x_interrupt(1);
		break;

	default:
		break;
	}
}

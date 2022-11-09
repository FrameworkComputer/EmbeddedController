/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Frostflow board-specific USB-C mux configuration */

#include <zephyr/drivers/gpio.h>

#include "console.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/retimer/anx7483_public.h"
#include "hooks.h"
#include "ioexpander.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/*
 * USB C0 (general) and C1 (just ANX DB) use IOEX pins to
 * indicate flipped polarity to a protection switch.
 */
static int ioex_set_flip(int port, mux_state_t mux_state)
{
	if (port == 0) {
		if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(ioex_usb_c0_sbu_flip),
				1);
		else
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(ioex_usb_c0_sbu_flip),
				0);
	} else {
		if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(ioex_usb_c1_sbu_flip),
				1);
		else
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(ioex_usb_c1_sbu_flip),
				0);
	}

	return EC_SUCCESS;
}

int board_anx7483_c0_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	/* Set the SBU polarity mux */
	RETURN_ERROR(ioex_set_flip(me->usb_port, mux_state));

	return anx7483_set_default_tuning(me, mux_state);
}

int board_c1_ps8818_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	CPRINTSUSB("C1: PS8818 mux using default tuning");

	/* Once a DP connection is established, we need to set IN_HPD */
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 1);
	else
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 0);

	return 0;
}

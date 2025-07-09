/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "console.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/retimer/anx7483_public.h"
#include "hooks.h"
#include "ioexpander.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"

#include <zephyr/drivers/gpio.h>

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/*
 * USB C0 (general) and C1 (just ANX DB) use IOEX pins to
 * indicate flipped polarity to a protection switch.
 */
int board_anx7483_c1_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	bool flipped = mux_state & USB_PD_MUX_POLARITY_INVERTED;

	/* Remove flipped from the state for easier compraisons */
	mux_state = mux_state & ~USB_PD_MUX_POLARITY_INVERTED;

	RETURN_ERROR(anx7483_set_default_tuning(me, mux_state));

	if (cros_cbi_ssfc_check_match(CBI_SSFC_VALUE_ID(DT_NODELABEL(ffc2)))) {
		if (mux_state == USB_PD_MUX_USB_ENABLED) {
			RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX1,
						    ANX7483_EQ_SETTING_6_8DB));
			RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX2,
						    ANX7483_EQ_SETTING_6_8DB));
			RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX1,
						    ANX7483_EQ_SETTING_7_8DB));
			RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX2,
						    ANX7483_EQ_SETTING_7_8DB));
		} else if (mux_state == USB_PD_MUX_DOCK && !flipped) {
			RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX1,
						    ANX7483_EQ_SETTING_6_8DB));
			RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX1,
						    ANX7483_EQ_SETTING_7_8DB));
		} else if (mux_state == USB_PD_MUX_DOCK && flipped) {
			RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX2,
						    ANX7483_EQ_SETTING_6_8DB));
			RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX2,
						    ANX7483_EQ_SETTING_7_8DB));
		}
	}

	return EC_SUCCESS;
}

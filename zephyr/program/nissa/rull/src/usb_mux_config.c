/* Copyright 2024 The ChromiumOS Authors
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

int board_anx7483_c1_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	RETURN_ERROR(anx7483_set_default_tuning(me, mux_state));

	RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_UTX1,
				    ANX7483_EQ_SETTING_12_5DB));
	RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_UTX2,
				    ANX7483_EQ_SETTING_12_5DB));
	RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX1,
				    ANX7483_EQ_SETTING_12_5DB));
	RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX2,
				    ANX7483_EQ_SETTING_12_5DB));
	RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX1,
				    ANX7483_EQ_SETTING_12_5DB));
	RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX2,
				    ANX7483_EQ_SETTING_12_5DB));

	return EC_SUCCESS;
}

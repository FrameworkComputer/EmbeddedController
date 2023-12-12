/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Winterhold board-specific USB-C mux configuration */

#include "charge_state.h"
#include "console.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/retimer/anx7483_public.h"
#include "hooks.h"
#include "ioexpander.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"
#include "ztest/usb_mux_config.h"

#include <zephyr/drivers/gpio.h>

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/*
 * USB C0 (general) and C1 (just ANX DB) use IOEX pins to
 * indicate flipped polarity to a protection switch.
 */
test_export_static int ioex_set_flip(int port, mux_state_t mux_state)
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

int board_anx7483_c1_fg_defalut_tuning(const struct usb_mux *me)
{
	RETURN_ERROR(
		anx7483_set_fg(me, ANX7483_PIN_URX1, ANX7483_FG_SETTING_1_2DB));
	RETURN_ERROR(
		anx7483_set_fg(me, ANX7483_PIN_URX2, ANX7483_FG_SETTING_1_2DB));
	RETURN_ERROR(
		anx7483_set_fg(me, ANX7483_PIN_UTX1, ANX7483_FG_SETTING_1_2DB));
	RETURN_ERROR(
		anx7483_set_fg(me, ANX7483_PIN_UTX2, ANX7483_FG_SETTING_1_2DB));

	return EC_SUCCESS;
}

int board_anx7483_c1_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	bool flipped = mux_state & USB_PD_MUX_POLARITY_INVERTED;

	/* Set the SBU polarity mux */
	RETURN_ERROR(ioex_set_flip(me->usb_port, mux_state));

	/* Remove flipped from the state for easier compraisons */
	mux_state = mux_state & ~USB_PD_MUX_POLARITY_INVERTED;

	RETURN_ERROR(anx7483_set_default_tuning(me, mux_state));

	/*
	 * Set the Flat Gain to default every time, to prevent DP only mode's
	 * Flat Gain change in the last plug.
	 */
	RETURN_ERROR(board_anx7483_c1_fg_defalut_tuning(me));

	if (mux_state == USB_PD_MUX_USB_ENABLED) {
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX1,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX2,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX1,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX2,
					    ANX7483_EQ_SETTING_12_5DB));
	} else if (mux_state == USB_PD_MUX_DP_ENABLED) {
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX1,
					    ANX7483_EQ_SETTING_8_4DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX2,
					    ANX7483_EQ_SETTING_8_4DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_UTX1,
					    ANX7483_EQ_SETTING_8_4DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_UTX2,
					    ANX7483_EQ_SETTING_8_4DB));
		RETURN_ERROR(anx7483_set_fg(me, ANX7483_PIN_URX1,
					    ANX7483_FG_SETTING_0_5DB));
		RETURN_ERROR(anx7483_set_fg(me, ANX7483_PIN_URX2,
					    ANX7483_FG_SETTING_0_5DB));
		RETURN_ERROR(anx7483_set_fg(me, ANX7483_PIN_UTX1,
					    ANX7483_FG_SETTING_0_5DB));
		RETURN_ERROR(anx7483_set_fg(me, ANX7483_PIN_UTX2,
					    ANX7483_FG_SETTING_0_5DB));
	} else if (mux_state == USB_PD_MUX_DOCK && !flipped) {
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX1,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX2,
					    ANX7483_EQ_SETTING_8_4DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX1,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_UTX2,
					    ANX7483_EQ_SETTING_8_4DB));
		RETURN_ERROR(anx7483_set_fg(me, ANX7483_PIN_URX2,
					    ANX7483_FG_SETTING_0_5DB));
		RETURN_ERROR(anx7483_set_fg(me, ANX7483_PIN_UTX2,
					    ANX7483_FG_SETTING_0_5DB));
	} else if (mux_state == USB_PD_MUX_DOCK && flipped) {
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX1,
					    ANX7483_EQ_SETTING_8_4DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX2,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_UTX1,
					    ANX7483_EQ_SETTING_8_4DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX2,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_fg(me, ANX7483_PIN_URX1,
					    ANX7483_FG_SETTING_0_5DB));
		RETURN_ERROR(anx7483_set_fg(me, ANX7483_PIN_UTX1,
					    ANX7483_FG_SETTING_0_5DB));
	}

	return EC_SUCCESS;
}

int charger_profile_override(struct charge_state_data *curr)
{
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		curr->requested_current = MIN(curr->requested_current,
					      WINTERHOLD_CHARGE_CURRENT_MAX);
	}

	return 0;
}

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}

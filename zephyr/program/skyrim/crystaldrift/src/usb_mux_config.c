/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Crystaldrift board-specific USB-C mux configuration */

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

int board_c0_amd_fp6_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	/* Set the SBU polarity mux */
	RETURN_ERROR(ioex_set_flip(me->usb_port, mux_state));

	return EC_SUCCESS;
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
					    ANX7483_EQ_SETTING_10_3DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX2,
					    ANX7483_EQ_SETTING_10_3DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_UTX1,
					    ANX7483_EQ_SETTING_10_3DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_UTX2,
					    ANX7483_EQ_SETTING_10_3DB));
		RETURN_ERROR(anx7483_set_fg(me, ANX7483_PIN_URX1,
					    ANX7483_FG_SETTING_1_2DB));
		RETURN_ERROR(anx7483_set_fg(me, ANX7483_PIN_URX2,
					    ANX7483_FG_SETTING_1_2DB));
		RETURN_ERROR(anx7483_set_fg(me, ANX7483_PIN_UTX1,
					    ANX7483_FG_SETTING_1_2DB));
		RETURN_ERROR(anx7483_set_fg(me, ANX7483_PIN_UTX2,
					    ANX7483_FG_SETTING_1_2DB));
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

test_export_static void setup_mux(void)
{
	uint32_t val;

	if (cros_cbi_get_fw_config(FW_IO_DB, &val) != 0)
		CPRINTSUSB("Error finding FW_DB_IO in CBI FW_CONFIG");
	/* Val will have our dts default on error, so continue setup */

	if (val == FW_IO_DB_PS8811_PS8818) {
		CPRINTSUSB("C1: Setting PS8818 mux");
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_ps8818_port1);
	} else if (val == FW_IO_DB_NONE_ANX7483) {
		CPRINTSUSB("C1: Setting ANX7483 mux");
	} else {
		CPRINTSUSB("Unexpected DB_IO board: %d", val);
	}
}
DECLARE_HOOK(HOOK_INIT, setup_mux, HOOK_PRIO_INIT_I2C);

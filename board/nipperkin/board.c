/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush board-specific configuration */

#include "base_fw_config.h"
#include "board_fw_config.h"
#include "button.h"
#include "chipset.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/retimer/ps8811.h"
#include "driver/retimer/ps8818.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "tablet_mode.h"
#include "temp_sensor/thermistor.h"
#include "temp_sensor/tmp112.h"
#include "usb_mux.h"

#include "gpio_list.h" /* Must come after other header files. */

/*
 * We have total 30 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{-1, -1}, {0, 5}, {1, 1}, {1, 0}, {0, 6},
	{0, 7}, {-1, -1}, {-1, -1}, {1, 4}, {1, 3},
	{-1, -1}, {1, 6}, {1, 7}, {3, 1}, {2, 0},
	{1, 5}, {2, 6}, {2, 7}, {2, 1}, {2, 4},
	{2, 5}, {1, 2}, {2, 3}, {2, 2}, {3, 0},
	{-1, -1}, {0, 4}, {-1, -1}, {8, 2}, {-1, -1},
	{-1, -1},
};
const int keyboard_factory_scan_pins_used =
		ARRAY_SIZE(keyboard_factory_scan_pins);

__override enum ec_error_list
board_a1_ps8811_retimer_init(const struct usb_mux *me)
{
	return EC_SUCCESS;
}

/*
 * PS8818 set mux board tuning.
 * Adds in board specific gain and DP lane count configuration
 * TODO(b/179036200): Adjust PS8818 tuning for guybrush reference
 */
__override int board_c1_ps8818_mux_set(const struct usb_mux *me,
				    mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX1EQ_10G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX2EQ_10G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX1EQ_5G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX2EQ_5G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Set the RX input termination */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_RX_PHY,
					PS8818_RX_INPUT_TERM_MASK,
					PS8818_RX_INPUT_TERM_112_OHM);
		if (rv)
			return rv;
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_DPEQ_LEVEL,
					PS8818_DPEQ_LEVEL_UP_MASK,
					PS8818_DPEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Enable HPD on the DB */
		gpio_set_level(GPIO_USB_C1_HPD, 1);
	} else {
		/* Disable HPD on the DB */
		gpio_set_level(GPIO_USB_C1_HPD, 0);
	}

	return rv;
}

static void board_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_chipset_startup(void)
{
	if (get_board_version() > 1)
		tmp112_init();
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup,
	     HOOK_PRIO_DEFAULT);

int board_get_soc_temp(int idx, int *temp_k)
{
	uint32_t board_version = get_board_version();

	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	if (board_version == 1)
		return get_temp_3v3_30k9_47k_4050b(ADC_TEMP_SENSOR_SOC, temp_k);

	return tmp112_get_val_k(idx, temp_k);
}

/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Krabby board-specific USB-C configuration */

#include "driver/tcpm/it83xx_pd.h"
#include "driver/usb_mux/ps8743.h"
#include "hooks.h"

#include "variant_db_detection.h"

void board_usb_mux_init(void)
{
	if (corsola_get_db_type() == CORSOLA_DB_TYPEC) {
		ps8743_tune_usb_eq(&usb_muxes[1],
				   PS8743_USB_EQ_TX_12_8_DB,
				   PS8743_USB_EQ_RX_12_8_DB);
		ps8743_write(&usb_muxes[1],
				   PS8743_REG_HS_DET_THRESHOLD,
				   PS8743_USB_HS_THRESH_NEG_10);
	}
}
DECLARE_HOOK(HOOK_INIT, board_usb_mux_init, HOOK_PRIO_INIT_I2C + 1);

const struct cc_para_t *board_get_cc_tuning_parameter(enum usbpd_port port)
{
	const static struct cc_para_t
		cc_parameter[CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT] = {
		{
			.rising_time = IT83XX_TX_PRE_DRIVING_TIME_1_UNIT,
			.falling_time = IT83XX_TX_PRE_DRIVING_TIME_2_UNIT,
		},
		{
			.rising_time = IT83XX_TX_PRE_DRIVING_TIME_1_UNIT,
			.falling_time = IT83XX_TX_PRE_DRIVING_TIME_2_UNIT,
		},
	};

	return &cc_parameter[port];
}

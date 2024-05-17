/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Pujjoga sub-board declarations */

#ifndef __CROS_EC_NISSA_PUJJOGA_SUB_BOARD_H__
#define __CROS_EC_NISSA_PUJJOGA_SUB_BOARD_H__

#include <ap_power/ap_power.h>

enum pujjoga_sub_board_type {
	PUJJOGA_SB_UNKNOWN = -1, /* Uninitialised */
	PUJJOGA_SB_NONE = 0, /* No board defined */
	PUJJOGA_SB_HDMI_A = 1, /* HDMI, USB type A */
};

enum pujjoga_sub_board_type pujjoga_get_sb_type(void);

/**
 * Configure the GPIO that controls the HDMI VCC pin on the HDMI sub-board.
 *
 * This is the gpio_hdmi_en_odl pin, which is configured as active-low
 * open-drain output to enable the VCC pin on the HDMI connector (typically when
 * the AP is on, in S0 or S0ix).
 *
 * This function must be called if the pin is connected to the HDMI board and
 * VCC is not enabled by default.
 */
void pujjoga_configure_hdmi_vcc(void);

void hdmi_power_handler(struct ap_power_ev_callback *cb,
			struct ap_power_ev_data data);

#endif /* __CROS_EC_NISSA_PUJJOGA_SUB_BOARD_H__ */

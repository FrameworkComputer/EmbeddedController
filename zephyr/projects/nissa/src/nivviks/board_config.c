/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nivviks sub-board hardware configuration */

#include "gpio.h"
#include "hooks.h"
#include "sub_board.h"

static void nivviks_subboard_init(void)
{
	enum nissa_sub_board_type sb = nissa_get_sb_type();

	if (sb != NISSA_SB_C_A && sb != NISSA_SB_HDMI_A) {
		/* Turn off unused USB A1 GPIOs */
	}
	if (sb == NISSA_SB_C_A || sb == NISSA_SB_C_LTE) {
		/* Enable type-C port 1 */
	}
	if (sb == NISSA_SB_HDMI_A) {
		/* Enable HDMI GPIOs */
	}
}
/*
 * Make sure setup is done after EEPROM is readable.
 */
DECLARE_HOOK(HOOK_INIT, nivviks_subboard_init, HOOK_PRIO_INIT_I2C + 1);

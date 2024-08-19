/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "ec_commands.h"
#include "gpio_it8xxx2.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_scan.h"

#include <zephyr/logging/log.h>

#include <drivers/vivaldi_kbd.h>

LOG_MODULE_REGISTER(brox, LOG_LEVEL_INF);

static bool key_bl = FW_KB_BL_NOT_PRESENT;

int8_t board_vivaldi_keybd_idx(void)
{
	if (key_bl == FW_KB_BL_NOT_PRESENT) {
		return DT_NODE_CHILD_IDX(DT_NODELABEL(kbd_config_1));
	} else {
		return DT_NODE_CHILD_IDX(DT_NODELABEL(kbd_config_0));
	}
}

/*
 * Keyboard function decided by FW config.
 */
test_export_static void kb_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_BL, &val);

	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_KB_TYPE);
	}

	if (val == FW_KB_BL_PRESENT) {
		key_bl = FW_KB_BL_PRESENT;
	} else {
		key_bl = FW_KB_BL_NOT_PRESENT;
	}
}
DECLARE_HOOK(HOOK_INIT, kb_init, HOOK_PRIO_POST_FIRST);

/*
 * We have total 32 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 },	  { -1, -1 },	    { -1, -1 },	      { GPIO_KSI, 0 },
	{ GPIO_KSI, 1 },  { GPIO_KSI, 2 },  { GPIO_KSI, 3 },  { GPIO_KSI, 4 },
	{ GPIO_KSI, 5 },  { GPIO_KSI, 6 },  { GPIO_KSI, 7 },  { GPIO_KSOH, 7 },
	{ GPIO_KSOH, 6 }, { GPIO_KSOH, 5 }, { GPIO_KSOH, 4 }, { GPIO_KSOH, 3 },
	{ GPIO_KSOH, 2 }, { GPIO_KSOH, 1 }, { GPIO_KSOH, 0 }, { GPIO_KSOL, 7 },
	{ GPIO_KSOL, 6 }, { GPIO_KSOL, 5 }, { GPIO_KSOL, 4 }, { GPIO_KSOL, 3 },
	{ GPIO_KSOL, 2 }, { GPIO_KSOL, 1 }, { GPIO_KSOL, 0 }, { GPIO_C, 5 },
	{ GPIO_C, 3 },	  { -1, -1 },	    { -1, -1 },	      { -1, -1 },
	{ -1, -1 },
};
const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);

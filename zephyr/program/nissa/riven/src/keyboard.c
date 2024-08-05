/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "ec_commands.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"

#include <drivers/vivaldi_kbd.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

/*
 * Keyboard function decided by FW config.
 */
test_export_static void kb_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_TYPE, &val);

	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_KB_TYPE);
	}

	if (val == FW_KB_TYPE_CA_FR) {
		/*
		 * Canadian French keyboard (US layout),
		 *   \| (key 45):     0x0061->0x61->0x56
		 *   r-ctrl (key 64): 0xe014->0x14->0x1d
		 * move key45 (row:7,col:17) to key64 (row:3,col:14)
		 */
		set_scancode_set2(3, 14, get_scancode_set2(7, 17));
	}
}
DECLARE_HOOK(HOOK_INIT, kb_init, HOOK_PRIO_POST_FIRST);

/*
 * We have total 32 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { -1, -1 }, { -1, -1 }, { 3, 1 },  { 3, 0 }, { 2, 7 },
	{ 2, 6 },   { 2, 5 },	{ 2, 4 },   { 2, 3 },  { 2, 2 }, { 8, 3 },
	{ 8, 2 },   { 0, 4 },	{ 0, 5 },   { 0, 6 },  { 0, 7 }, { 1, 0 },
	{ 1, 1 },   { 1, 2 },	{ 1, 3 },   { 1, 4 },  { 1, 5 }, { 1, 6 },
	{ 1, 7 },   { 2, 0 },	{ 2, 1 },   { 11, 1 }, { 0, 3 }, { -1, -1 },
	{ -1, -1 }, { -1, -1 }, { -1, -1 },
};
const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);

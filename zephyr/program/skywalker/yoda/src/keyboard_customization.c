/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_cbi.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_customization.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <drivers/scancode_set2.h>

LOG_MODULE_REGISTER(yoda_keyboard, LOG_LEVEL_INF);

static bool key_numpad = FW_KB_NUMERIC_PAD_ABSENT;
/*
 * Keyboard function decided by FW config.
 */
test_export_static void keyboard_matrix_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_NUMERIC_PAD, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d, "
			"assuming FW_KB_NUMERIC_PAD_PRESENT",
			FW_KB_NUMERIC_PAD);
		val = FW_KB_NUMERIC_PAD_PRESENT;
	}

	switch (val) {
	case FW_KB_NUMERIC_PAD_PRESENT:
		key_numpad = FW_KB_NUMERIC_PAD_PRESENT;
		LOG_INF("numpad keyboard matrix");
		break;
	case FW_KB_NUMERIC_PAD_ABSENT:
		key_numpad = FW_KB_NUMERIC_PAD_ABSENT;
		LOG_INF("no numpad keyboard matrix");
		break;
	default:
		LOG_WRN("invalid cbi value: %x", val);
		return;
	}
}
DECLARE_HOOK(HOOK_INIT, keyboard_matrix_init, HOOK_PRIO_POST_FIRST);

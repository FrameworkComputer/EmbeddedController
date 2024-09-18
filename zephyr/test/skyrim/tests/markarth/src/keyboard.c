/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "keyboard_8042_sharedlib.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <drivers/vivaldi_kbd.h>
#include <keyboard_scan.h>

void kb_layout_init(void);
FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VALUE_FUNC(uint16_t, get_scancode_set2, uint8_t, uint8_t);
FAKE_VOID_FUNC(set_scancode_set2, uint8_t, uint8_t, uint16_t);

static int kb_layout;

static int
cros_cbi_get_fw_config_kb_layout(enum cbi_fw_config_field_id field_id,
				 uint32_t *value)
{
	if (field_id != FW_KB_LAYOUT)
		return -EINVAL;

	switch (kb_layout) {
	case 0:
		*value = KEYBOARD_DEFAULT;
		break;
	case 1:
		*value = KEYBOARD_ANSI;
		break;
	case -1:
		return -EINVAL;
	default:
		return 0;
	}
	return 0;
}

ZTEST(markarth_keyboard, test_kb_layout_default)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		cros_cbi_get_fw_config_kb_layout;

	kb_layout = 0;
	kb_layout_init();

	zassert_equal(set_scancode_set2_fake.call_count, 0);
}

ZTEST(markarth_keyboard, test_kb_layout_ansi)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		cros_cbi_get_fw_config_kb_layout;

	kb_layout = 1;
	kb_layout_init();

	zassert_equal(set_scancode_set2_fake.call_count, 2);
}

ZTEST(markarth_keyboard, test_kb_layout_error)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		cros_cbi_get_fw_config_kb_layout;

	kb_layout = -1;
	kb_layout_init();

	zassert_equal(set_scancode_set2_fake.call_count, 0);
}

static void test_before(void *fixture)
{
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(get_scancode_set2);
	RESET_FAKE(set_scancode_set2);
}
ZTEST_SUITE(markarth_keyboard, NULL, NULL, test_before, NULL, NULL);

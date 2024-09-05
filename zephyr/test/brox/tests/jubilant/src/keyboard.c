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

void kb_init(void);
void keyboard_matrix_init(void);

static int kb_blight;
static int kb_numpad;

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);

FAKE_VOID_FUNC(lpc_keyboard_resume_irq);

static struct {
	uint16_t row;
	uint16_t col;
	int call_count;
} vol_up_key;

void set_vol_up_key(uint8_t row, uint8_t col)
{
	vol_up_key.row = row;
	vol_up_key.col = col;
	vol_up_key.call_count++;
}

struct keyboard_scan_config keyscan_config;

static void test_before(void *fixture)
{
	RESET_FAKE(cros_cbi_get_fw_config);
}
ZTEST_SUITE(jubilant_keyboard, NULL, NULL, test_before, NULL, NULL);

static int
cros_cbi_get_fw_config_kb_blight(enum cbi_fw_config_field_id field_id,
				 uint32_t *value)
{
	if (field_id != FW_KB_BL)
		return -EINVAL;

	switch (kb_blight) {
	case 0:
		*value = FW_KB_BL_PRESENT;
		break;
	case 1:
		*value = FW_KB_BL_NOT_PRESENT;
		break;
	case -1:
		return -EINVAL;
	default:
		return 0;
	}
	return 0;
}

static int
cros_cbi_get_fw_config_kb_numpad(enum cbi_fw_config_field_id field_id,
				 uint32_t *value)
{
	if (field_id != FW_KB_NUMERIC_PAD)
		return -EINVAL;

	switch (kb_numpad) {
	case 0:
		*value = FW_KB_NUMERIC_PAD_ABSENT;
		break;
	case 1:
		*value = FW_KB_NUMERIC_PAD_PRESENT;
		break;
	case -1:
		return -EINVAL;
	default:
		return 0;
	}
	return 0;
}

ZTEST(jubilant_keyboard, test_kb_init)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		cros_cbi_get_fw_config_kb_blight;

	kb_blight = 0;
	kb_init();
	zassert_equal(board_vivaldi_keybd_idx(), 0);

	kb_blight = 1;
	kb_init();
	zassert_equal(board_vivaldi_keybd_idx(), 1);
}

ZTEST(jubilant_keyboard, test_keyboard_matrix_init)
{
	uint16_t fn_key = 0x0037;
	uint16_t forwardslash_pipe_key = 0x0061;

	cros_cbi_get_fw_config_fake.custom_fake =
		cros_cbi_get_fw_config_kb_numpad;

	kb_numpad = 0;
	keyboard_matrix_init();

	zassert_equal(get_scancode_set2(0, 16), fn_key);
	zassert_equal(get_scancode_set2(7, 17), forwardslash_pipe_key);
	kb_numpad = 1;
	keyboard_matrix_init();

	zassert_equal(get_scancode_set2(4, 10), fn_key);
	zassert_equal(get_scancode_set2(2, 7), forwardslash_pipe_key);
}

ZTEST(jubilant_keyboard, test_kb_init_cbi_error)
{
	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	kb_init();
}

ZTEST(jubilant_keyboard, test_keyboard_matrix_cbi_error)
{
	uint16_t fn_key = 0x0037;
	uint16_t forwardslash_pipe_key = 0x0061;

	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	kb_numpad = -1;
	keyboard_matrix_init();

	zassert_equal(get_scancode_set2(4, 10), fn_key);
	zassert_equal(get_scancode_set2(2, 7), forwardslash_pipe_key);
}

ZTEST(jubilant_keyboard, test_get_scancode_set2)
{
	/* Test some special keys of the customization matrix */
	zassert_equal(get_scancode_set2(3, 0), SCANCODE_LEFT_WIN);
	zassert_equal(get_scancode_set2(0, 12), SCANCODE_F15);

	/* Test out of the matrix range */
	zassert_equal(get_scancode_set2(8, 12), 0);
	zassert_equal(get_scancode_set2(0, 18), 0);
}

ZTEST(jubilant_keyboard, test_set_scancode_set2)
{
	/* Set some special keys and read back */
	zassert_equal(get_scancode_set2(1, 0), 0);
	set_scancode_set2(1, 0, SCANCODE_LEFT_WIN);
	zassert_equal(get_scancode_set2(1, 0), SCANCODE_LEFT_WIN);

	zassert_equal(get_scancode_set2(4, 0), 0);
	set_scancode_set2(4, 0, SCANCODE_CAPSLOCK);
	zassert_equal(get_scancode_set2(4, 0), SCANCODE_CAPSLOCK);

	zassert_equal(get_scancode_set2(0, 13), 0);
	set_scancode_set2(0, 13, SCANCODE_F15);
	zassert_equal(get_scancode_set2(0, 13), SCANCODE_F15);
}

ZTEST(jubilant_keyboard, test_get_keycap_label)
{
	zassert_equal(get_keycap_label(3, 0), KLLI_SEARC);
	zassert_equal(get_keycap_label(0, 12), KLLI_F15);
	zassert_equal(get_keycap_label(8, 12), KLLI_UNKNO);
	zassert_equal(get_keycap_label(0, 18), KLLI_UNKNO);
}

ZTEST(jubilant_keyboard, test_set_keycap_label)
{
	zassert_equal(get_keycap_label(2, 0), KLLI_UNKNO);
	set_keycap_label(2, 0, KLLI_SEARC);
	zassert_equal(get_keycap_label(2, 0), KLLI_SEARC);

	zassert_equal(get_keycap_label(0, 14), KLLI_UNKNO);
	set_keycap_label(0, 14, KLLI_F15);
	zassert_equal(get_keycap_label(0, 14), KLLI_F15);
}

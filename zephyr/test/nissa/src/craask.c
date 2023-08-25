/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "button.h"
#include "craask.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "nissa_sub_board.h"

#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);
FAKE_VALUE_FUNC(enum nissa_sub_board_type, nissa_get_sb_type);
FAKE_VOID_FUNC(usb_interrupt_c1, enum gpio_signal);

FAKE_VOID_FUNC(lpc_keyboard_resume_irq);

static void test_before(void *fixture)
{
	RESET_FAKE(cbi_get_board_version);
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(nissa_get_sb_type);
}

ZTEST_SUITE(craask, NULL, NULL, test_before, NULL, NULL);

static int board_version;

static int cbi_get_board_version_mock(uint32_t *value)
{
	*value = board_version;
	return 0;
}

int clock_get_freq(void)
{
	return 16000000;
}

ZTEST(craask, test_volum_up_dn_buttons)
{
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_mock;

	nissa_get_sb_type_fake.return_val = NISSA_SB_C_A;

	board_version = 1;
	buttons_init();
	zassert_equal(buttons[BUTTON_VOLUME_UP].gpio, GPIO_VOLUME_UP_L);
	zassert_equal(buttons[BUTTON_VOLUME_DOWN].gpio, GPIO_VOLUME_DOWN_L);

	board_version = 2;
	buttons_init();
	zassert_equal(buttons[BUTTON_VOLUME_UP].gpio, GPIO_VOLUME_UP_L);
	zassert_equal(buttons[BUTTON_VOLUME_DOWN].gpio, GPIO_VOLUME_DOWN_L);

	board_version = 3;
	buttons_init();
	zassert_equal(buttons[BUTTON_VOLUME_UP].gpio, GPIO_VOLUME_DOWN_L);
	zassert_equal(buttons[BUTTON_VOLUME_DOWN].gpio, GPIO_VOLUME_UP_L);
}

static bool has_keypad;

static int cbi_get_keyboard_configuration(enum cbi_fw_config_field_id field,
					  uint32_t *value)
{
	if (field != FW_KB_NUMERIC_PAD)
		return -EINVAL;

	*value = has_keypad ? FW_KB_NUMERIC_PAD_PRESENT :
			      FW_KB_NUMERIC_PAD_ABSENT;
	return 0;
}

ZTEST(craask, test_keyboard_configuration)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		cbi_get_keyboard_configuration;

	has_keypad = false;
	kb_init();
	zassert_equal(keyboard_raw_get_cols(), KEYBOARD_COLS_NO_KEYPAD);
	zassert_equal(keyscan_config.actual_key_mask[11], 0xfa);
	zassert_equal(keyscan_config.actual_key_mask[12], 0xca);
	zassert_equal(keyscan_config.actual_key_mask[13], 0x00);
	zassert_equal(keyscan_config.actual_key_mask[14], 0x00);
	zassert_equal_ptr(board_vivaldi_keybd_config(), &craask_kb);

	/* Initialize keyboard_cols for next test */
	keyboard_raw_set_cols(KEYBOARD_COLS_MAX);

	has_keypad = true;
	kb_init();
	zassert_equal(keyboard_raw_get_cols(), KEYBOARD_COLS_WITH_KEYPAD);
	zassert_equal(keyscan_config.actual_key_mask[11], 0xfe);
	zassert_equal(keyscan_config.actual_key_mask[12], 0xff);
	zassert_equal(keyscan_config.actual_key_mask[13], 0xff);
	zassert_equal(keyscan_config.actual_key_mask[14], 0xff);
	zassert_equal_ptr(board_vivaldi_keybd_config(), &craask_kb_w_kb_numpad);
}

static bool keyboard_ca_fr;

static int cbi_get_keyboard_type_config(enum cbi_fw_config_field_id field,
					uint32_t *value)
{
	if (field != FW_KB_TYPE)
		return -EINVAL;

	*value = keyboard_ca_fr ? FW_KB_TYPE_CA_FR : FW_KB_TYPE_DEFAULT;
	return 0;
}

ZTEST(craask, test_keyboard_type)
{
	uint16_t forwardslash_pipe_key = get_scancode_set2(2, 7);
	uint16_t right_control_key = get_scancode_set2(4, 0);

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_keyboard_type_config;

	keyboard_ca_fr = false;
	kb_init();
	zassert_equal(get_scancode_set2(4, 0), right_control_key);
	zassert_equal(get_scancode_set2(2, 7), forwardslash_pipe_key);

	keyboard_ca_fr = true;
	kb_init();
	zassert_equal(get_scancode_set2(4, 0), forwardslash_pipe_key);
	zassert_equal(get_scancode_set2(2, 7), right_control_key);
}

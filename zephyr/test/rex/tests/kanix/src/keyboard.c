/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "hooks.h"
#include "keyboard_protocol.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

void kb_init(void);

static bool keyboard_type;

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VOID_FUNC(set_scancode_set2, uint8_t, uint8_t, uint16_t);
FAKE_VOID_FUNC(get_scancode_set2, uint8_t, uint8_t);

int cros_cbi_get_fw_config_mock(enum cbi_fw_config_field_id field_id,
				uint32_t *value)
{
	zassert_equal(field_id, FW_KB_TYPE);
	*value = keyboard_type ? FW_KB_CA_FR : FW_KB_DEFAULT;
	return 0;
}

static void keyboard_config_before(void *fixture)
{
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(set_scancode_set2);
	RESET_FAKE(get_scancode_set2);
}

ZTEST_SUITE(kanix_keyboard, NULL, NULL, keyboard_config_before, NULL, NULL);

ZTEST(kanix_keyboard, test_keyboard_type_init)
{
	cros_cbi_get_fw_config_fake.custom_fake = cros_cbi_get_fw_config_mock;

	keyboard_type = false;
	kb_init();
	zassert_equal(get_scancode_set2_fake.call_count, 0);
	zassert_equal(set_scancode_set2_fake.call_count, 0);

	keyboard_type = true;
	kb_init();
	zassert_equal(get_scancode_set2_fake.call_count, 2);
	zassert_equal(set_scancode_set2_fake.call_count, 2);
}

ZTEST(kanix_keyboard, test_keyboard_type_init_error)
{
	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	kb_init();
	zassert_equal(get_scancode_set2_fake.call_count, 0);
	zassert_equal(set_scancode_set2_fake.call_count, 0);
}

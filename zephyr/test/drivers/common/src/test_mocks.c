/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "test/drivers/test_mocks.h"

#include <zephyr/ztest.h>

/* Mocks for common/init_rom.c */
DEFINE_FAKE_VALUE_FUNC(const void *, init_rom_map, const void *, int);
DEFINE_FAKE_VOID_FUNC(init_rom_unmap, const void *, int);
DEFINE_FAKE_VALUE_FUNC(int, init_rom_copy, int, int, int);

/* Mocks for common/system.c */
DEFINE_FAKE_VALUE_FUNC(int, system_jumped_late);
DEFINE_FAKE_VALUE_FUNC(int, system_is_locked);
DEFINE_FAKE_VOID_FUNC(system_reset, int);
DEFINE_FAKE_VOID_FUNC(software_panic, uint32_t, uint32_t);
DEFINE_FAKE_VOID_FUNC(assert_post_action, const char *, unsigned int);

/* Mocks for common/lid_angle.c */
DEFINE_FAKE_VOID_FUNC(lid_angle_peripheral_enable, int);

/* Mocks for gpio.h */
DEFINE_FAKE_VALUE_FUNC(int, gpio_config_unused_pins);
DEFINE_FAKE_VALUE_FUNC(int, gpio_configure_port_pin, int, int, int);

/* Mocks for drivers */
DEFINE_FAKE_VALUE_FUNC(int, ppc_get_alert_status, int);

/**
 * @brief Reset all the fakes before each test.
 */
static void fff_reset_rule_before(const struct ztest_unit_test *test,
				  void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	RESET_FAKE(init_rom_map);
	RESET_FAKE(init_rom_unmap);
	RESET_FAKE(init_rom_copy);
	RESET_FAKE(system_jumped_late);
	RESET_FAKE(system_is_locked);
	RESET_FAKE(system_reset);
	RESET_FAKE(software_panic);
	RESET_FAKE(assert_post_action);
	RESET_FAKE(lid_angle_peripheral_enable);
	RESET_FAKE(gpio_config_unused_pins);
	RESET_FAKE(gpio_configure_port_pin);
	RESET_FAKE(ppc_get_alert_status);
}

ZTEST_RULE(fff_reset_rule, fff_reset_rule_before, NULL);

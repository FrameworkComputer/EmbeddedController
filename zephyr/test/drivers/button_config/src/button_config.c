/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Tests for button_config.c
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "button.h"
#include "button_config.h"
#include "common.h"
#include "ec_tasks.h"
#include "hooks.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"

LOG_MODULE_REGISTER(button_cfg_test, LOG_LEVEL_INF);

int stub_button_state;
int stub_get_button_state(const struct device *d, gpio_pin_t p)
{
	ARG_UNUSED(d);
	ARG_UNUSED(p);
	return stub_button_state;
}

FAKE_VALUE_FUNC(int, stub_gpio_pin_get, const struct device *, gpio_pin_t);
FAKE_VALUE_FUNC(int, stub_gpio_pin_get_raw, const struct device *, gpio_pin_t);

#define BUTTON_CFG_LIST(FAKE)                \
	{                                    \
		FAKE(stub_gpio_pin_get);     \
		FAKE(stub_gpio_pin_get_raw); \
	}

static void test_button_cfg_reset(void)
{
	BUTTON_CFG_LIST(RESET_FAKE);

	FFF_RESET_HISTORY();

	stub_button_state = 0;
	stub_gpio_pin_get_fake.custom_fake = gpio_pin_get;
	stub_gpio_pin_get_raw_fake.custom_fake = gpio_pin_get_raw;
}

static void button_config_rule(const struct ztest_unit_test *test, void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	test_button_cfg_reset();
}

ZTEST_RULE(button_config_rule, button_config_rule, button_config_rule);

/**
 * Make sure mocks are setup before HOOK(HOOK_PRIO_INIT_POWER_BUTTON) runs
 * otherwise unexpected calls to mocks above occur prevent default
 * gpio_pin_get behavior
 */
DECLARE_HOOK(HOOK_INIT, test_button_cfg_reset, HOOK_PRIO_FIRST);

/**
 * @brief Test Suite: Verifies button_config functionality.
 */
ZTEST_SUITE(button_config, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

/**
 * @brief TestPurpose: Verify button_config initialization.
 *
 */
ZTEST(button_config, test_button_config)
{
	const struct button_config_v2 *button;

	for (int i = 0; i < BUTTON_CFG_COUNT; i++) {
		button = button_cfg_get(i);

		LOG_INF("button[%d]= {%s, %d, %d, {%d, 0x%X}, %d, %d}\n", i,
			button->name, button->type, button->gpio,
			button->spec.pin, button->spec.dt_flags,
			button->debounce_us, button->button_flags);
	}

	button = button_cfg_get(BUTTON_CFG_POWER_BUTTON);

	zassert_equal(button->type, 0, NULL);
	zassert_equal(button->gpio, GPIO_POWER_BUTTON_L, NULL);
	zassert_equal(button->debounce_us, 30000, NULL);
	zassert_equal(button->button_flags, 0, NULL);
}

/**
 * @brief TestPurpose: Verify button_config pressed raw.
 *
 */
ZTEST(button_config, test_button_pressed)
{
	stub_gpio_pin_get_fake.custom_fake = stub_get_button_state;

	stub_button_state = 1;
	zassert_equal(1, button_is_pressed(BUTTON_CFG_POWER_BUTTON));

	stub_button_state = 0;
	zassert_equal(0, button_is_pressed(BUTTON_CFG_POWER_BUTTON));

	stub_button_state = -1;
	zassert_equal(0, button_is_pressed(BUTTON_CFG_POWER_BUTTON));
}

/**
 * @brief TestPurpose: Verify button_config pressed raw.
 *
 */
ZTEST(button_config, test_button_pressed_raw)
{
	stub_gpio_pin_get_raw_fake.custom_fake = stub_get_button_state;

	stub_button_state = 1;
	zassert_equal(1, button_is_pressed_raw(BUTTON_CFG_POWER_BUTTON));

	stub_button_state = 0;
	zassert_equal(0, button_is_pressed_raw(BUTTON_CFG_POWER_BUTTON));

	stub_button_state = -1;
	zassert_equal(0, button_is_pressed_raw(BUTTON_CFG_POWER_BUTTON));
}

/**
 * @brief TestPurpose: Verify button name.
 *
 */
ZTEST(button_config, test_button_name)
{
	const char *name;

	name = button_get_name(BUTTON_CFG_POWER_BUTTON);
	zassert_ok(strcmp(name, "POWER_BUTTON"), NULL);

	name = button_get_name(BUTTON_CFG_COUNT);
	zassert_ok(strcmp(name, "NULL"), NULL);
}

/**
 * @brief TestPurpose: Verify button debounce.
 *
 */
ZTEST(button_config, test_button_debounce)
{
	const uint32_t debounce_time_us = 30000;

	zassert_equal(debounce_time_us,
		      button_get_debounce_us(BUTTON_CFG_POWER_BUTTON), NULL);

	zassert_equal(0, button_get_debounce_us(BUTTON_CFG_COUNT), NULL);
}

/**
 * @brief TestPurpose: Verify button interrupt.
 *
 */
extern bool gpio_test_interrupt_triggered;
ZTEST(button_config, test_button_interrupt)
{
	const struct button_config_v2 *cfg;

	cfg = button_cfg_get(BUTTON_CFG_TEST_BUTTON);

	gpio_test_interrupt_triggered = false;

	zassert_ok(button_disable_interrupt(BUTTON_CFG_TEST_BUTTON), NULL);
	gpio_pin_set_raw(cfg->spec.port, cfg->spec.pin, 0);
	gpio_pin_set_raw(cfg->spec.port, cfg->spec.pin, 1);
	zassert_equal(gpio_test_interrupt_triggered, false);

	zassert_ok(button_enable_interrupt(BUTTON_CFG_TEST_BUTTON), NULL);
	gpio_pin_set_raw(cfg->spec.port, cfg->spec.pin, 0);
	gpio_pin_set_raw(cfg->spec.port, cfg->spec.pin, 1);
	zassert_equal(gpio_test_interrupt_triggered, true);
}

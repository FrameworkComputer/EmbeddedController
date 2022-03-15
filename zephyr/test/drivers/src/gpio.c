/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for GPIO.
 */

#include <device.h>

#include <drivers/gpio/gpio_emul.h>
#include <logging/log.h>
#include <zephyr.h>
#include <ztest.h>

#include "common.h"
#include "ec_tasks.h"
#include "gpio.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "stubs.h"
#include "util.h"
#include "test_state.h"

extern bool gpio_test_interrupt_triggered;
/**
 * @brief TestPurpose: Verify Zephyr to EC GPIO bitmask conversion.
 *
 * @details
 * Validate Zephyr to EC GPIO bitmask conversion.
 *
 * Expected Results
 *  - GPIO bitmask conversions are successful
 */
ZTEST(gpio, test_convert_from_zephyr_flags)
{
	int retval;
	struct {
		int zephyr_bmask;
		gpio_flags_t expected_ec_bmask;
	} validate[] = {
		{ GPIO_DISCONNECTED, GPIO_FLAG_NONE },
		{ GPIO_OUTPUT_INIT_LOW, GPIO_LOW },
		{ GPIO_OUTPUT_INIT_HIGH, GPIO_HIGH },
		{ GPIO_VOLTAGE_1P8, GPIO_SEL_1P8V },
		{ GPIO_INT_ENABLE, GPIO_FLAG_NONE },
		{ GPIO_INT_ENABLE | GPIO_INT_EDGE, GPIO_FLAG_NONE },
		{ GPIO_INT_ENABLE | GPIO_INT_EDGE | GPIO_INT_HIGH_1,
		  GPIO_INT_F_RISING },
		{ GPIO_INT_ENABLE | GPIO_INT_EDGE | GPIO_INT_LOW_0,
		  GPIO_INT_F_FALLING },
		{ GPIO_INT_ENABLE | GPIO_INT_HIGH_1, GPIO_INT_F_HIGH },
		{ GPIO_INT_ENABLE | GPIO_INT_LOW_0, GPIO_INT_F_LOW },
		{ GPIO_OUTPUT_INIT_LOGICAL, 0 },
		{ GPIO_OPEN_DRAIN | GPIO_PULL_UP,
		  GPIO_OPEN_DRAIN | GPIO_PULL_UP },
	};
	int num_tests = ARRAY_SIZE(validate);

	for (int i = 0; i < num_tests; i++) {
		retval = convert_from_zephyr_flags(validate[i].zephyr_bmask);
		zassert_equal(validate[i].expected_ec_bmask, retval,
			      "[%d] Expected 0x%08X, returned 0x%08X.", i,
			      validate[i].expected_ec_bmask, retval);
	}
}

/**
 * @brief TestPurpose: Verify EC to Zephyr GPIO bitmask conversion.
 *
 * @details
 * Validate EC to Zephyr GPIO bitmask conversion.
 *
 * Expected Results
 *  - GPIO bitmask conversions are successful
 */
ZTEST(gpio, test_convert_to_zephyr_flags)
{
	gpio_flags_t retval;

	struct {
		gpio_flags_t ec_bmask;
		int expected_zephyr_bmask;
	} validate[] = {
		{ GPIO_FLAG_NONE, GPIO_DISCONNECTED },
		{ GPIO_LOW, GPIO_OUTPUT_INIT_LOW },
		{ GPIO_HIGH, GPIO_OUTPUT_INIT_HIGH },
		{ GPIO_INT_F_RISING,
		  GPIO_INT_ENABLE | GPIO_INT_EDGE | GPIO_INT_HIGH_1 },
		{ GPIO_INT_F_FALLING,
		  GPIO_INT_ENABLE | GPIO_INT_EDGE | GPIO_INT_LOW_0 },
		{ GPIO_INT_F_LOW, GPIO_INT_ENABLE | GPIO_INT_LOW_0 },
		{ GPIO_INT_F_HIGH, GPIO_INT_ENABLE | GPIO_INT_HIGH_1 },
		{ GPIO_SEL_1P8V, GPIO_VOLTAGE_1P8 },
	};
	int num_tests = ARRAY_SIZE(validate);

	for (int i = 0; i < num_tests; i++) {
		retval = convert_to_zephyr_flags(validate[i].ec_bmask);
		zassert_equal(validate[i].expected_zephyr_bmask, retval,
			      "[%d] Expected 0x%08X, returned 0x%08X.", i,
			      validate[i].expected_zephyr_bmask, retval);
	}
}

/**
 * @brief TestPurpose: Verify GPIO signal_is_gpio.
 *
 * @details
 * Validate signal_is_gpio
 *
 * Expected Results
 *  - Success
 */
ZTEST(gpio, test_signal_is_gpio)
{
	zassert_true(signal_is_gpio(
		GPIO_SIGNAL(DT_NODELABEL(gpio_test))), "Expected true");
}

/**
 * @brief TestPurpose: Verify legacy API GPIO set/get level.
 *
 * @details
 * Validate set/get level for legacy API
 * This tests the legacy API, though no Zepyhr
 * based code should use it.
 *
 * Expected Results
 *  - Success
 */
ZTEST(gpio, test_legacy_gpio_get_set_level)
{
	enum gpio_signal signal = GPIO_SIGNAL(DT_NODELABEL(gpio_test));
	int level;
	/* Test invalid signal */
	gpio_set_level(GPIO_COUNT, 0);
	zassert_equal(0, gpio_get_level(GPIO_COUNT), "Expected level==0");
	/* Test valid signal */
	gpio_set_level(signal, 0);
	zassert_equal(0, gpio_get_level(signal), "Expected level==0");
	gpio_set_level(signal, 1);
	zassert_equal(1, gpio_get_level(signal), "Expected level==1");
	level = gpio_get_ternary(signal);
	gpio_set_level_verbose(CC_CHIPSET, signal, 0);
	zassert_equal(0, gpio_get_level(signal), "Expected level==0");
}

/**
 * @brief TestPurpose: Verify legacy GPIO enable/disable interrupt.
 *
 * @details
 * Validate gpio_enable_interrupt/gpio_disable_interrupt
 * Uses the legacy API. No Zephyr code should use this API.
 *
 * Expected Results
 *  - Success
 */

ZTEST(gpio, test_legacy_gpio_enable_interrupt)
{
	enum gpio_signal signal = GPIO_SIGNAL(DT_NODELABEL(gpio_test));

	gpio_test_interrupt_triggered = false;

	/* Test invalid signal */
	zassert_not_equal(EC_SUCCESS, gpio_disable_interrupt(GPIO_COUNT), NULL);
	zassert_not_equal(EC_SUCCESS, gpio_enable_interrupt(GPIO_COUNT), NULL);
	zassert_false(gpio_test_interrupt_triggered, NULL);

	/* Test valid signal */
	zassert_ok(gpio_disable_interrupt(signal), NULL);
	gpio_set_level(signal, 0);
	zassert_false(gpio_test_interrupt_triggered, NULL);
	gpio_set_level(signal, 1);
	zassert_false(gpio_test_interrupt_triggered, NULL);

	zassert_ok(gpio_enable_interrupt(signal), NULL);
	gpio_set_level(signal, 0);
	zassert_true(gpio_test_interrupt_triggered, NULL);
	gpio_test_interrupt_triggered = false;
	gpio_set_level(signal, 1);
	zassert_true(gpio_test_interrupt_triggered, NULL);
}

/**
 * @brief TestPurpose: Verify GPIO set/get level.
 *
 * @details
 * Validate set/get level
 *
 * Expected Results
 *  - Success
 */
ZTEST(gpio, test_gpio_pin_get_set_level)
{
	const struct gpio_dt_spec *gp = GPIO_DT_FROM_NODELABEL(gpio_test);

	/* Test invalid signal */
	zassert_equal(NULL, gpio_get_dt_spec(-1), "Expected NULL ptr");

	zassert_false(gp == NULL, "Unexpected NULL ptr");
	/* Test valid signal */
	gpio_pin_set_dt(gp, 0);
	zassert_equal(0, gpio_pin_get_dt(gp), "Expected level==0");

	gpio_pin_set_dt(gp, 1);
	zassert_equal(1, gpio_pin_get_dt(gp), "Expected level==1");
}

/**
 * @brief TestPurpose: Verify GPIO get name.
 *
 * @details
 * Validate gpio_get_name
 *
 * Expected Results
 *  - Success
 */
ZTEST(gpio, test_gpio_get_name)
{
	enum gpio_signal signal = GPIO_SIGNAL(DT_NODELABEL(gpio_test));
	const char *signal_name;

	/* Test invalid signal */
	signal_name = gpio_get_name(GPIO_COUNT);
	zassert_mem_equal("UNIMPLEMENTED", signal_name, strlen(signal_name),
			  "gpio_get_name returned a valid signal \'%s\'",
			  signal_name);

	/* Test valid signal */
	signal_name = gpio_get_name(signal);
	zassert_mem_equal("test", signal_name, strlen(signal_name),
			  "gpio_get_name returned signal \'%s\'", signal_name);
}

/**
 * @brief Helper function to get GPIO flags
 *
 * @param signal
 * @return gpio_flags_t
 */
gpio_flags_t gpio_helper_get_flags(enum gpio_signal signal)
{
	const struct gpio_dt_spec *spec;
	gpio_flags_t flags;

	spec = gpio_get_dt_spec(signal);
	gpio_emul_flags_get(spec->port, spec->pin, &flags);

	return flags;
}

/**
 * @brief TestPurpose: Verify GPIO get default flags.
 *
 * @details
 * Validate gpio_get_default_flags
 *
 * Expected Results
 *  - Success
 */
ZTEST(gpio, test_gpio_get_default_flags)
{
	enum gpio_signal signal = GPIO_SIGNAL(DT_NODELABEL(gpio_test));
	gpio_flags_t flags;
	gpio_flags_t flags_at_start[GPIO_COUNT];
	int def_flags;

	/* Snapshot of GPIO flags before testing */
	for (int i = 0; i < GPIO_COUNT; i++)
		flags_at_start[i] = gpio_helper_get_flags(i);

	/* Test invalid signal */
	def_flags = gpio_get_default_flags(GPIO_COUNT);
	zassert_equal(0, def_flags, "Expected 0x0, returned 0x%08X", def_flags);
	gpio_set_flags(GPIO_COUNT, GPIO_INPUT);

	/* Verify flags didn't change */
	for (int i = 0; i < GPIO_COUNT; i++) {
		flags = gpio_helper_get_flags(i);
		zassert_equal(flags_at_start[i], flags,
			      "%s[%d] flags_at_start=0x%x, flags=0x%x",
			      gpio_get_name(i), i, flags_at_start[i], flags);
	}

	/* Test valid signal */
	def_flags = gpio_get_default_flags(signal);
	zassert_equal(GPIO_INPUT | GPIO_OUTPUT, def_flags,
		      "Expected 0x%08x, returned 0x%08X",
		      GPIO_INPUT | GPIO_OUTPUT, def_flags);

	gpio_set_flags(signal, GPIO_INPUT);
	flags = gpio_helper_get_flags(signal);
	zassert_equal(flags, GPIO_INPUT, "Flags set 0x%x", flags);

	gpio_set_flags(signal, GPIO_OUTPUT);
	flags = gpio_helper_get_flags(signal);
	zassert_equal(flags, GPIO_OUTPUT, "Flags set 0x%x", flags);
}


/**
 * @brief TestPurpose: Verify GPIO no-auto-init.
 *
 * @details
 * Validate no-auto-init device tree property,
 * which will not initialise the GPIO at startup.
 *
 * Expected Results
 *  - Success
 */
ZTEST(gpio, test_gpio_no_auto_init)
{
	const struct gpio_dt_spec *gp = GPIO_DT_FROM_NODELABEL(gpio_no_init);
	enum gpio_signal signal = GPIO_SIGNAL(DT_NODELABEL(gpio_no_init));
	gpio_flags_t flags;

	flags = gpio_helper_get_flags(signal);
	zassert_equal(0, flags,
		      "Expected 0x%08x, returned 0x%08X",
		      0, flags);

	/* Configure pin. */
	gpio_pin_configure_dt(gp, GPIO_INPUT | GPIO_OUTPUT);
	flags = gpio_helper_get_flags(signal);
	zassert_equal(flags,
		      (GPIO_ACTIVE_LOW | GPIO_OUTPUT | GPIO_INPUT),
		       "Flags set 0x%x", flags);
}

/**
 * @brief TestPurpose: Verify GPIO reset.
 *
 * @details
 * Validate gpio_reset
 *
 * Expected Results
 *  - Success
 */
ZTEST(gpio, test_gpio_reset)
{
	enum gpio_signal signal = GPIO_SIGNAL(DT_NODELABEL(gpio_test));
	gpio_flags_t flags;
	gpio_flags_t flags_at_start[GPIO_COUNT];

	/* Snapshot of GPIO flags before testing */
	for (int i = 0; i < GPIO_COUNT; i++)
		flags_at_start[i] = gpio_helper_get_flags(i);

	/* Test reset on invalid signal */
	gpio_reset(GPIO_COUNT);

	/* Verify flags didn't change */
	for (int i = 0; i < GPIO_COUNT; i++) {
		flags = gpio_helper_get_flags(i);
		zassert_equal(flags_at_start[i], flags,
			      "%s[%d] flags_at_start=0x%x, flags=0x%x",
			      gpio_get_name(i), i, flags_at_start[i], flags);
	}

	/* Test reset on valid signal */
	gpio_set_flags(signal, GPIO_OUTPUT);
	flags = gpio_helper_get_flags(signal);
	zassert_equal(flags, GPIO_OUTPUT, "Flags set 0x%x", flags);

	gpio_reset(signal);

	flags = gpio_helper_get_flags(signal);
	zassert_equal(flags, gpio_get_default_flags(signal), "Flags set 0x%x",
		      flags);
}

/**
 * @brief TestPurpose: Verify GPIO enable/disable interrupt.
 *
 * @details
 * Validate gpio_enable_dt_interrupt
 *
 * Expected Results
 *  - Success
 */
ZTEST(gpio, test_gpio_enable_dt_interrupt)
{
	const struct gpio_dt_spec *gp = GPIO_DT_FROM_NODELABEL(gpio_test);
	const struct gpio_int_config *intr =
		GPIO_INT_FROM_NODELABEL(int_gpio_test);

	gpio_test_interrupt_triggered = false;

	/* Test valid signal */
	zassert_ok(gpio_disable_dt_interrupt(intr), NULL);
	gpio_pin_set_dt(gp, 0);
	zassert_false(gpio_test_interrupt_triggered, NULL);
	gpio_pin_set_dt(gp, 1);
	zassert_false(gpio_test_interrupt_triggered, NULL);

	zassert_ok(gpio_enable_dt_interrupt(intr), NULL);
	gpio_pin_set_dt(gp, 0);
	zassert_true(gpio_test_interrupt_triggered, NULL);
	gpio_test_interrupt_triggered = false;
	gpio_pin_set_dt(gp, 1);
	zassert_true(gpio_test_interrupt_triggered, NULL);
}

/**
 * @brief GPIO test setup handler.
 */
static void gpio_before(void *state)
{
	ARG_UNUSED(state);
	/** TODO: Reset all signals here. Currently other tests fail when reset
	 * for(int i = 0; i < GPIO_COUNT; i++)	{
	 *	gpio_reset(i);
	 * }
	 */
	gpio_reset(GPIO_SIGNAL(DT_NODELABEL(gpio_test)));
}

/**
 * @brief Test Suite: Verifies GPIO functionality.
 */
ZTEST_SUITE(gpio, drivers_predicate_post_main, NULL, gpio_before, NULL, NULL);

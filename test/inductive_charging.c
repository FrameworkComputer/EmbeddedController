/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test inductive charging module.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "inductive_charging.h"
#include "lid_switch.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#define START_CHARGE_DELAY 5000 /* ms */
#define MONITOR_CHARGE_DONE_DELAY 1000 /* ms */
#define TEST_CHECK_CHARGE_DELAY \
	(START_CHARGE_DELAY + MONITOR_CHARGE_DONE_DELAY + 500) /* ms */

static void wait_for_lid_debounce(void)
{
	while (lid_is_open() != gpio_get_level(GPIO_LID_OPEN))
		crec_msleep(20);
}

static void set_lid_open(int lid_open)
{
	gpio_set_level(GPIO_LID_OPEN, lid_open);
	wait_for_lid_debounce();
}

static int test_lid(void)
{
	/* Lid is open initially */
	set_lid_open(1);
	gpio_set_level(GPIO_CHARGE_DONE, 0);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 0);

	/*
	 * Close the lid. The EC should wait for 5 second before
	 * enabling transmitter.
	 */
	set_lid_open(0);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 0);
	crec_msleep(TEST_CHECK_CHARGE_DELAY);

	/* Transmitter should now be enabled. */
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 1);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 1);

	/* Open the lid. Charging should stop. */
	set_lid_open(1);
	crec_msleep(TEST_CHECK_CHARGE_DELAY);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 0);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 0);

	return EC_SUCCESS;
}

static int test_charge_done(void)
{
	/* Close the lid to start charging */
	set_lid_open(0);
	crec_msleep(TEST_CHECK_CHARGE_DELAY);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 1);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 1);

	/* Charging is done. Stop charging, but don't turn off transmitter. */
	gpio_set_level(GPIO_CHARGE_DONE, 1);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 1);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 0);

	/* Oops, CHARGE_DONE changes again. We should ignore it. */
	gpio_set_level(GPIO_CHARGE_DONE, 0);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 1);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 0);

	/* Open the lid. Charger should be turned off. */
	set_lid_open(1);
	crec_msleep(TEST_CHECK_CHARGE_DELAY);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 0);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 0);

	return EC_SUCCESS;
}

static int test_lid_open_during_charging(void)
{
	/* Close the lid. Start charging. */
	set_lid_open(0);
	crec_msleep(TEST_CHECK_CHARGE_DELAY);
	gpio_set_level(GPIO_CHARGE_DONE, 0);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 1);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 1);

	/* Open the lid. Transmitter should be turned off. */
	set_lid_open(1);
	crec_msleep(TEST_CHECK_CHARGE_DELAY);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 0);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 0);

	/* Toggle charge done signal. Charging should not start. */
	gpio_set_level(GPIO_CHARGE_DONE, 1);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 0);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 0);
	gpio_set_level(GPIO_CHARGE_DONE, 0);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 0);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 0);

	return EC_SUCCESS;
}

static int test_debounce_charge_done(void)
{
	/* Lid is open initially. */
	set_lid_open(1);
	gpio_set_level(GPIO_CHARGE_DONE, 0);
	crec_msleep(TEST_CHECK_CHARGE_DELAY);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 0);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 0);

	/* Close the lid. Charging should start. */
	set_lid_open(0);
	crec_msleep(START_CHARGE_DELAY + 100);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 1);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 1);

	/* Within the first second, changes on CHARGE_DONE should be ignore. */
	gpio_set_level(GPIO_CHARGE_DONE, 1);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 1);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 1);
	crec_msleep(100);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 1);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 1);
	gpio_set_level(GPIO_CHARGE_DONE, 0);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 1);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 1);
	crec_msleep(100);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 1);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 1);

	/* Changes on CHARGE_DONE after take effect. */
	crec_msleep(MONITOR_CHARGE_DONE_DELAY);
	gpio_set_level(GPIO_CHARGE_DONE, 1);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 1);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 0);

	/* Open the lid. Charger should be turned off. */
	set_lid_open(1);
	crec_msleep(TEST_CHECK_CHARGE_DELAY);
	TEST_ASSERT(gpio_get_level(GPIO_BASE_CHG_VDD_EN) == 0);
	TEST_ASSERT(gpio_get_level(GPIO_CHARGE_EN) == 0);

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_lid);
	RUN_TEST(test_charge_done);
	RUN_TEST(test_lid_open_during_charging);
	RUN_TEST(test_debounce_charge_done);

	test_print_result();
}

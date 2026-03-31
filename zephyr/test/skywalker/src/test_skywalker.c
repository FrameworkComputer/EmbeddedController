/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "test_state.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);
FAKE_VOID_FUNC(bma4xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(lis2dw12_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(xhci_interrupt, enum gpio_signal);

static int cbi_get_board_version_mock(uint32_t *value)
{
	*value = 1;
	return 0;
}

static void fake_cbi_init(void)
{
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_mock;
}
DECLARE_HOOK(HOOK_INIT, fake_cbi_init, HOOK_PRIO_POST_I2C - 1);

ZTEST(skywalker, test_lid_accel_interrupt)
{
	const struct gpio_dt_spec *gpio_lid_accel_int_ec_l =
		GPIO_DT_FROM_NODELABEL(gpio_lid_accel_int_ec_l);

	/* Trigger interrupt via GPIO emulator */
	zassert_ok(gpio_emul_input_set_dt(gpio_lid_accel_int_ec_l, 1));
	k_sleep(K_MSEC(1));
	zassert_ok(gpio_emul_input_set_dt(gpio_lid_accel_int_ec_l, 0));
	k_sleep(K_MSEC(1));
	zassert_ok(gpio_emul_input_set_dt(gpio_lid_accel_int_ec_l, 1));
	zassert_equal(lis2dw12_interrupt_fake.call_count, 0);
	zassert_equal(bma4xx_interrupt_fake.call_count, 1);
}

ZTEST_SUITE(skywalker, skywalker_predicate_post_main, NULL, NULL, NULL, NULL);

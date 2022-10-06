/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "variant_db_detection.h"

static void *db_detection_setup(void)
{
	const struct device *wp_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_ALIAS(gpio_wp), gpios));
	const gpio_port_pins_t wp_pin = DT_GPIO_PIN(DT_ALIAS(gpio_wp), gpios);

	/* Make sure that write protect is disabled */
	zassert_ok(gpio_emul_input_set(wp_gpio, wp_pin, 1), NULL);
	/* Set CBI db_config to DB_NONE. */
	zassert_ok(cbi_set_fw_config(DB_NONE << 0), NULL);
	/* Run init hooks to initialize cbi. */
	hook_notify(HOOK_INIT);
	return NULL;
}

ZTEST_SUITE(db_detection, NULL, db_detection_setup, NULL, NULL, NULL);

static int interrupt_count;
void x_ec_interrupt(enum gpio_signal signal)
{
	interrupt_count++;
}

/* test none db case */
ZTEST(db_detection, test_db_detect_none)
{
	gpio_flags_t *flags = (gpio_flags_t *)malloc(sizeof(gpio_flags_t));

	const struct device *ec_x_gpio1 = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_ec_x_gpio1), gpios));
	gpio_pin_t ec_x_pin1 =
		DT_GPIO_PIN(DT_NODELABEL(gpio_ec_x_gpio1), gpios);
	const struct device *x_ec_gpio2 = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_x_ec_gpio2), gpios));
	gpio_pin_t x_ec_pin2 =
		DT_GPIO_PIN(DT_NODELABEL(gpio_x_ec_gpio2), gpios);
	const struct device *ec_x_gpio3 = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_ec_x_gpio3), gpios));
	gpio_pin_t ec_x_pin3 =
		DT_GPIO_PIN(DT_NODELABEL(gpio_ec_x_gpio3), gpios);

	/* Check the DB type is NONE */
	zassert_equal(CORSOLA_DB_NONE, corsola_get_db_type(), NULL);

	/* Verify the floating pins are input with PU to prevent leakage */
	zassert_ok(gpio_emul_flags_get(ec_x_gpio1, ec_x_pin1, flags), NULL);
	zassert_equal(*flags, (GPIO_INPUT | GPIO_PULL_UP), "flags=%d", *flags);
	zassert_ok(gpio_emul_flags_get(x_ec_gpio2, x_ec_pin2, flags), NULL);
	zassert_equal(*flags, (GPIO_INPUT | GPIO_PULL_UP), "flags=%d", *flags);
	zassert_ok(gpio_emul_flags_get(ec_x_gpio3, ec_x_pin3, flags), NULL);
	zassert_equal(*flags, (GPIO_INPUT | GPIO_PULL_UP), "flags=%d", *flags);
	free(flags);

	/* Verify x_ec_interrupt is disabled */
	interrupt_count = 0;
	zassert_ok(gpio_emul_input_set(x_ec_gpio2, x_ec_pin2, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(x_ec_gpio2, x_ec_pin2, 1), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(interrupt_count, 0, "interrupt_count=%d",
		      interrupt_count);
}

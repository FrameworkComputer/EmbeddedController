/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "zephyr/kernel.h"
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "gpio_signal.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

FAKE_VOID_FUNC(typec_set_sbu, int, bool);
/* fake definitions to pass build */
FAKE_VOID_FUNC(bmi3xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(x_ec_interrupt, enum gpio_signal);

#define FFF_FAKES_LIST(FAKE)   \
	FAKE(typec_set_sbu)    \
	FAKE(bmi3xx_interrupt) \
	FAKE(x_ec_interrupt)

struct kingler_ccd_fixture {
	int default_ccd_lvl;
	int default_aux_path_lvl;
};

static void *ccd_setup(void)
{
	static struct kingler_ccd_fixture f;

	return &f;
}

static void kingler_ccd_reset_rule_before(const struct ztest_unit_test *test,
					  void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();
}
ZTEST_RULE(kingler_ccd_reset_rule, kingler_ccd_reset_rule_before, NULL);

static void kingler_ccd_before(void *data)
{
	struct kingler_ccd_fixture *f = (struct kingler_ccd_fixture *)data;

	f->default_ccd_lvl =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ccd_mode_odl));
	f->default_aux_path_lvl =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(dp_aux_path_sel));

	zassert_ok(gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_ccd_mode_odl)));
}

static void kingler_ccd_after(void *data)
{
	struct kingler_ccd_fixture *f = (struct kingler_ccd_fixture *)data;

	zassert_ok(gpio_disable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_ccd_mode_odl)));
	zassert_ok(gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ccd_mode_odl),
				   f->default_ccd_lvl));
	zassert_ok(gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(dp_aux_path_sel),
				   f->default_aux_path_lvl));
}

ZTEST_SUITE(kingler_ccd, NULL, ccd_setup, kingler_ccd_before, kingler_ccd_after,
	    NULL);

ZTEST_F(kingler_ccd, test_dp_aux_path)
{
	const struct device *ccd_mode_odl_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_ccd_mode_odl), gpios));
	const gpio_port_pins_t ccd_mode_odl_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_ccd_mode_odl), gpios);
	const struct device *dp_aux_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(dp_aux_path_sel), gpios));
	const gpio_port_pins_t dp_aux_pin =
		DT_GPIO_PIN(DT_NODELABEL(dp_aux_path_sel), gpios);

	/* reset CCD mode and mux AUX path to CCD port by default */
	zassert_ok(gpio_emul_input_set(ccd_mode_odl_gpio, ccd_mode_odl_pin, 1),
		   NULL);
	zassert_ok(gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(dp_aux_path_sel), 0),
		   NULL);
	zassert_equal(0, gpio_emul_output_get(dp_aux_gpio, dp_aux_pin), NULL);

	/* CCD asserts and trigger ccd_interrupt */
	zassert_ok(gpio_emul_input_set(ccd_mode_odl_gpio, ccd_mode_odl_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(1, typec_set_sbu_fake.call_count, NULL);
	/* CCD triggered, aux path has to be muxed to port 1 */
	zassert_equal(1, gpio_emul_output_get(dp_aux_gpio, dp_aux_pin), NULL);

	/* CCD deasserts */
	zassert_ok(gpio_emul_input_set(ccd_mode_odl_gpio, ccd_mode_odl_pin, 1),
		   NULL);

	k_sleep(K_MSEC(100));
	/* do not touch dp aux path when CCD deasserted */
	zassert_equal(1, gpio_emul_output_get(dp_aux_gpio, dp_aux_pin), NULL);
	zassert_equal(1, typec_set_sbu_fake.call_count, NULL);
}

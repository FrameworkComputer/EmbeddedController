/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "zephyr/kernel.h"
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#include "gpio_signal.h"
#include "hooks.h"
#include "variant_db_detection.h"

static void *db_detection_setup(void)
{
	const struct device *hdmi_prsnt_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_hdmi_prsnt_odl), gpios));
	const gpio_port_pins_t hdmi_prsnt_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_hdmi_prsnt_odl), gpios);
	/* Set the GPIO to low to indicate the DB is HDMI */
	zassert_ok(gpio_emul_input_set(hdmi_prsnt_gpio, hdmi_prsnt_pin, 0),
		   NULL);

	hook_notify(HOOK_INIT);

	return NULL;
}

ZTEST_SUITE(db_detection, NULL, db_detection_setup, NULL, NULL, NULL);

static int interrupt_count;
void x_ec_interrupt(enum gpio_signal signal)
{
	interrupt_count++;
}

/* test hdmi db case */
ZTEST(db_detection, test_db_detect_hdmi)
{
	const struct device *en_hdmi_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_ALIAS(gpio_en_hdmi_pwr), gpios));
	const gpio_port_pins_t en_hdmi_pin =
		DT_GPIO_PIN(DT_ALIAS(gpio_en_hdmi_pwr), gpios);
	const struct device *ps185_pwrdn_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_ALIAS(gpio_ps185_pwrdn_odl), gpios));
	const gpio_port_pins_t ps185_pwrdn_pin =
		DT_GPIO_PIN(DT_ALIAS(gpio_ps185_pwrdn_odl), gpios);
	const struct device *int_x_ec_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_x_ec_gpio2), gpios));
	const gpio_port_pins_t int_x_ec_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_x_ec_gpio2), gpios);

	/* Check the DB type is HDMI */
	zassert_equal(CORSOLA_DB_HDMI, corsola_get_db_type(), NULL);

	/* Verify we can enable or disable hdmi power */
	zassert_ok(gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_en_hdmi_pwr), 1),
		   NULL);
	zassert_equal(1, gpio_emul_output_get(en_hdmi_gpio, en_hdmi_pin), NULL);
	zassert_ok(gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_en_hdmi_pwr), 0),
		   NULL);
	zassert_equal(0, gpio_emul_output_get(en_hdmi_gpio, en_hdmi_pin), NULL);

	/* Verify we can change the gpio_ps185_pwrdn_odl state */
	zassert_ok(gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_pwrdn_odl), 1),
		   NULL);
	zassert_equal(1,
		      gpio_emul_output_get(ps185_pwrdn_gpio, ps185_pwrdn_pin),
		      NULL);
	zassert_ok(gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_ps185_pwrdn_odl), 0),
		   NULL);
	zassert_equal(0,
		      gpio_emul_output_get(ps185_pwrdn_gpio, ps185_pwrdn_pin),
		      NULL);

	/* Verify x_ec_interrupt is enabled */
	interrupt_count = 0;
	zassert_ok(gpio_emul_input_set(int_x_ec_gpio, int_x_ec_pin, 1), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(interrupt_count, 1, "interrupt_count=%d",
		      interrupt_count);
}

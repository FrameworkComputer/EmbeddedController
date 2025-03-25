/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "tablet_mode.h"
#include "zephyr/kernel.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

static void *tablet_setup(void)
{
	uint32_t val;
	const struct device *wp_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_ALIAS(gpio_wp), gpios));
	const gpio_port_pins_t wp_pin = DT_GPIO_PIN(DT_ALIAS(gpio_wp), gpios);

	/* Make sure that write protect is disabled */
	zassert_ok(gpio_emul_input_set(wp_gpio, wp_pin, 1), NULL);
	/* Set CBI form factor to CONVERTIBLE. */
	zassert_ok(cbi_set_fw_config(CONVERTIBLE << 13), NULL);
	/* Re-initialize CBI */
	cros_cbi_ec_init();
	/* Re-initialize sensors and board config */
	hook_notify(HOOK_INIT);

	/* Check if CBI write worked. */
	zassert_ok(cros_cbi_get_fw_config(FORM_FACTOR, &val), NULL);
	zassert_equal(CONVERTIBLE, val, "val=%d", val);

	return NULL;
}

ZTEST_SUITE(steelix_tablet, NULL, tablet_setup, NULL, NULL, NULL);

ZTEST(steelix_tablet, test_gmr_tablet_switch_enabled)
{
	const struct device *tablet_mode_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_tablet_mode_l), gpios);

	/* Verify gmr_tablet_switch is enabled, by checking the side effects
	 * of calling tablet_set_mode, and setting gpio_tablet_mode_l.
	 */
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(1, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 1),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(0, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(1, tablet_get_mode(), NULL);
}

static int interrupt_count;

void bmi3xx_interrupt(enum gpio_signal signal)
{
	interrupt_count++;
}

ZTEST(steelix_tablet, test_base_imu_irq_enabled)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(base_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(base_imu_int_l), gpios);

	/* Verify base_imu_irq is enabled. Interrupt is configured
	 * GPIO_INT_EDGE_FALLING, so set high, then set low.
	 */
	interrupt_count = 0;
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(interrupt_count, 1, "interrupt_count=%d",
		      interrupt_count);
}

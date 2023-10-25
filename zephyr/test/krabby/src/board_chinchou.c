/* Copyright 2023 The ChromiumOS Authors
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
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(bmi3xx_interrupt, enum gpio_signal);

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);

int mock_cros_cbi_get_fw_config_clamshell(enum cbi_fw_config_field_id field_id,
					  uint32_t *value)
{
	*value = CLAMSHELL;
	return 0;
}

int mock_cros_cbi_get_fw_config_error(enum cbi_fw_config_field_id field_id,
				      uint32_t *value)
{
	return -1;
}

static void *clamshell_setup(void)
{
	int val;

	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_clamshell;
	hook_notify(HOOK_INIT);

	/* Check if CBI write worked. */
	zassert_ok(cros_cbi_get_fw_config(FORM_FACTOR, &val), NULL);
	zassert_equal(CLAMSHELL, val, "val=%d", val);

	return NULL;
}

ZTEST_SUITE(chinchou_clamshell, NULL, clamshell_setup, NULL, NULL, NULL);

ZTEST(chinchou_clamshell, test_gmr_tablet_switch_disabled)
{
	const struct device *tablet_mode_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(tablet_mode_l), gpios);

	/* Verify gmr_tablet_switch is disabled, by checking the side effects
	 * of calling tablet_set_mode, and setting tablet_mode_l.
	 */
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 1),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(0, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
}

static int interrupt_count;

ZTEST(chinchou_clamshell, test_base_imu_irq_disabled)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(base_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(base_imu_int_l), gpios);

	/* Verify base_imu_irq is disabled. */
	interrupt_count = 0;
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(interrupt_count, 0, "interrupt_count=%d",
		      interrupt_count);
}

ZTEST_USER(chinchou_clamshell, test_error_reading_cbi)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_error;
	hook_notify(HOOK_INIT);
}

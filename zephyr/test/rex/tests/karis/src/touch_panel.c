/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "lid_switch.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <dt-bindings/gpio_defines.h>

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);

static bool cbi_touch_en;
static bool cbi_read_fail;

static int cbi_get_touch_en_config(enum cbi_fw_config_field_id field,
				   uint32_t *value)
{
	if (field != FW_TOUCH_EN)
		return -EINVAL;

	if (cbi_read_fail)
		return -1;

	*value = cbi_touch_en ? FW_TOUCH_EN_ENABLE : FW_TOUCH_EN_DISABLE;
	return 0;
}

#define TEST_DELAY_MS 1
#define TOUCH_ENABLE_DELAY_MS (500 + TEST_DELAY_MS)
#define TOUCH_DISABLE_DELAY_MS (0 + TEST_DELAY_MS)
#define TEST_LID_DEBOUNCE_MS (LID_DEBOUNCE_US / MSEC + 1)

static void touch_config_before(void *fixture)
{
	RESET_FAKE(cros_cbi_get_fw_config);
}

ZTEST_SUITE(karis_touch, NULL, NULL, touch_config_before, NULL, NULL);

ZTEST(karis_touch, test_touch_enable_config)
{
	const struct gpio_dt_spec *bl_en =
		GPIO_DT_FROM_NODELABEL(gpio_soc_3v3_edp_bl_en);
	const struct gpio_dt_spec *touch_en =
		GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en);
	const struct gpio_dt_spec *lid_open =
		GPIO_DT_FROM_NODELABEL(gpio_lid_open);

	cbi_touch_en = true;
	cbi_read_fail = false;
	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_touch_en_config;

	/* lid is open before init at first boot up */
	zassert_ok(gpio_emul_input_set(lid_open->port, lid_open->pin, 1), NULL);

	hook_notify(HOOK_INIT);

	/* touch_en become high after TOUCH_ENABLE_DELAY_MS delay */
	zassert_ok(gpio_emul_input_set(bl_en->port, bl_en->pin, 1), NULL);
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	k_sleep(K_MSEC(TOUCH_ENABLE_DELAY_MS));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 1);

	/* touch_en become low after TOUCH_DISABLE_DELAY_MS delay */
	zassert_ok(gpio_emul_input_set(bl_en->port, bl_en->pin, 0), NULL);
	k_sleep(K_MSEC(TOUCH_DISABLE_DELAY_MS));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	/* touch_en keep low if fw_config is not enabled */
	cbi_touch_en = false;
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_soc_edp_bl_en));
	hook_notify(HOOK_INIT);

	zassert_ok(gpio_emul_input_set(bl_en->port, bl_en->pin, 1), NULL);
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	k_sleep(K_MSEC(TOUCH_ENABLE_DELAY_MS));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	/* touch_en keep low if fw_config read fail */
	cbi_read_fail = true;
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_soc_edp_bl_en));
	hook_notify(HOOK_INIT);

	zassert_ok(gpio_emul_input_set(bl_en->port, bl_en->pin, 0), NULL);
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	zassert_ok(gpio_emul_input_set(bl_en->port, bl_en->pin, 1), NULL);
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	k_sleep(K_MSEC(TOUCH_ENABLE_DELAY_MS));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);
}

ZTEST(karis_touch, test_touch_lid_change)
{
	const struct gpio_dt_spec *bl_en =
		GPIO_DT_FROM_NODELABEL(gpio_soc_3v3_edp_bl_en);
	const struct gpio_dt_spec *touch_en =
		GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en);
	const struct gpio_dt_spec *lid_open =
		GPIO_DT_FROM_NODELABEL(gpio_lid_open);

	cbi_touch_en = true;
	cbi_read_fail = false;
	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_touch_en_config;

	/* lid is open before init at first boot up */
	zassert_ok(gpio_emul_input_set(lid_open->port, lid_open->pin, 1), NULL);
	/* bl_en is low before SOC is powered on */
	zassert_ok(gpio_emul_input_set(bl_en->port, bl_en->pin, 0), NULL);

	hook_notify(HOOK_INIT);

	zassert_equal(lid_is_open(), 1);

	/* touch_en become high after TOUCH_ENABLE_DELAY_MS delay */
	zassert_ok(gpio_emul_input_set(bl_en->port, bl_en->pin, 1), NULL);
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	k_sleep(K_MSEC(TOUCH_ENABLE_DELAY_MS));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 1);

	/* Close Lid and call HOOK_LID_CHANGE after TEST_LID_DEBOUNCE_MS delay
	 */
	zassert_ok(gpio_emul_input_set(lid_open->port, lid_open->pin, 0), NULL);
	k_sleep(K_MSEC(TEST_LID_DEBOUNCE_MS));
	zassert_equal(lid_is_open(), 0);

	/* HOOK_LID_CHANGE let the touch_en become low after
	 * TOUCH_DISABLE_DELAY_MS delay
	 */
	k_sleep(K_MSEC(TOUCH_DISABLE_DELAY_MS));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	/* Open Lid while bl_en still high */
	zassert_ok(gpio_emul_input_set(lid_open->port, lid_open->pin, 1), NULL);
	k_sleep(K_MSEC(TEST_LID_DEBOUNCE_MS));
	zassert_equal(lid_is_open(), 1);

	/* HOOK_LID_CHANGE let the touch_en become high after
	 * TOUCH_ENABLE_DELAY_MS delay
	 */
	k_sleep(K_MSEC(TOUCH_ENABLE_DELAY_MS));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 1);
}

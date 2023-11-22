/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "fpsensor/fpsensor_detect.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "zephyr/kernel.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/pm/policy.h>
#include <zephyr/ztest.h>

static uint32_t hook_chip_resume_cnt;
static uint32_t hook_chip_suspend_cnt;

FAKE_VALUE_FUNC(enum fp_transport_type, get_fp_transport_type);

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(substate_id);
	ARG_UNUSED(state);
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);
	irq_unlock(0);
}

static void chipset_resume(void)
{
	hook_chip_resume_cnt++;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, chipset_resume, HOOK_PRIO_DEFAULT);

static void chipset_suspend(void)
{
	hook_chip_suspend_cnt++;
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, chipset_suspend, HOOK_PRIO_DEFAULT);

static void *power_setup(void)
{
	RESET_FAKE(get_fp_transport_type);
	get_fp_transport_type_fake.return_val = FP_TRANSPORT_TYPE_SPI;

	return NULL;
}

ZTEST_SUITE(power, NULL, power_setup, NULL, NULL, NULL);

ZTEST(power, test_slp_event)
{
	const struct device *slp_l_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(slp_l), gpios));
	const gpio_port_pins_t slp_l_pin =
		DT_GPIO_PIN(DT_NODELABEL(slp_l), gpios);
	const struct device *slp_alt_l_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(slp_alt_l), gpios));
	const gpio_port_pins_t slp_alt_l_pin =
		DT_GPIO_PIN(DT_NODELABEL(slp_alt_l), gpios);

	RESET_FAKE(get_fp_transport_type);
	get_fp_transport_type_fake.return_val = FP_TRANSPORT_TYPE_SPI;

	/* Set init state */
	gpio_emul_input_set(slp_alt_l_gpio, slp_alt_l_pin, 0);
	gpio_emul_input_set(slp_l_gpio, slp_l_pin, 0);
	sleep(1);
	hook_chip_suspend_cnt = 0;
	hook_chip_resume_cnt = 0;

	/* Set AP S0 */
	gpio_emul_input_set(slp_alt_l_gpio, slp_alt_l_pin, 1);
	sleep(1);
	gpio_emul_input_set(slp_l_gpio, slp_l_pin, 1);
	sleep(1);
	/* One call for enabling slp_alt_l */
	zassert_equal(hook_chip_suspend_cnt, 1,
		      "Incorrect suspend chip hook call count");
	zassert_equal(hook_chip_resume_cnt, 1,
		      "Incorrect resume chip hook call count");
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      1, "Incorrect pm lock state");

	/* Suspend */
	gpio_emul_input_set(slp_l_gpio, slp_l_pin, 0);
	sleep(1);
	zassert_equal(hook_chip_suspend_cnt, 2,
		      "Incorrect suspend chip hook call count");
	zassert_equal(hook_chip_resume_cnt, 1,
		      "Incorrect resume chip hook call count");
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      0, "Incorrect pm lock state");

	gpio_emul_input_set(slp_alt_l_gpio, slp_alt_l_pin, 0);
	sleep(1);
	zassert_equal(hook_chip_suspend_cnt, 3,
		      "Incorrect suspend chip hook call count");
	zassert_equal(hook_chip_resume_cnt, 1,
		      "Incorrect resume chip hook call count");
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      0, "Incorrect pm lock state");

	gpio_emul_input_set(slp_alt_l_gpio, slp_alt_l_pin, 1);
	sleep(1);
	zassert_equal(hook_chip_suspend_cnt, 4,
		      "Incorrect suspend chip hook call count");
	zassert_equal(hook_chip_resume_cnt, 1,
		      "Incorrect resume chip hook call count");
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      0, "Incorrect pm lock state");

	/* Resume */
	gpio_emul_input_set(slp_l_gpio, slp_l_pin, 1);
	sleep(1);
	zassert_equal(hook_chip_suspend_cnt, 4,
		      "Incorrect suspend chip hook call count");
	zassert_equal(hook_chip_resume_cnt, 2,
		      "Incorrect resume chip hook call count");
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      1, "Incorrect pm lock state");
}

ZTEST(power, test_slp_event_broken_slp_l)
{
	const struct device *slp_l_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(slp_l), gpios));
	const gpio_port_pins_t slp_l_pin =
		DT_GPIO_PIN(DT_NODELABEL(slp_l), gpios);
	const struct device *slp_alt_l_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(slp_alt_l), gpios));
	const gpio_port_pins_t slp_alt_l_pin =
		DT_GPIO_PIN(DT_NODELABEL(slp_alt_l), gpios);

	get_fp_transport_type_fake.return_val = FP_TRANSPORT_TYPE_UART;

	/* Set init state */
	gpio_emul_input_set(slp_alt_l_gpio, slp_alt_l_pin, 0);
	gpio_emul_input_set(slp_l_gpio, slp_l_pin, 0);
	sleep(1);
	hook_chip_suspend_cnt = 0;
	hook_chip_resume_cnt = 0;

	/* Set AP S0 */
	gpio_emul_input_set(slp_alt_l_gpio, slp_alt_l_pin, 1);
	sleep(1);
	gpio_emul_input_set(slp_l_gpio, slp_l_pin, 1);
	sleep(1);
	/* One call for enabling slp_alt_l */
	zassert_equal(hook_chip_suspend_cnt, 0,
		      "Incorrect suspend chip hook call count");
	zassert_equal(hook_chip_resume_cnt, 2,
		      "Incorrect resume chip hook call count");
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      1, "Incorrect pm lock state");

	/* Suspend */
	gpio_emul_input_set(slp_l_gpio, slp_l_pin, 0);
	sleep(1);
	zassert_equal(hook_chip_suspend_cnt, 0,
		      "Incorrect suspend chip hook call count");
	zassert_equal(hook_chip_resume_cnt, 3,
		      "Incorrect resume chip hook call count");
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      1, "Incorrect pm lock state");

	gpio_emul_input_set(slp_alt_l_gpio, slp_alt_l_pin, 0);
	sleep(1);
	zassert_equal(hook_chip_suspend_cnt, 1,
		      "Incorrect suspend chip hook call count");
	zassert_equal(hook_chip_resume_cnt, 3,
		      "Incorrect resume chip hook call count");
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      0, "Incorrect pm lock state");

	gpio_emul_input_set(slp_alt_l_gpio, slp_alt_l_pin, 1);
	sleep(1);
	zassert_equal(hook_chip_suspend_cnt, 1,
		      "Incorrect suspend chip hook call count");
	zassert_equal(hook_chip_resume_cnt, 4,
		      "Incorrect resume chip hook call count");
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      1, "Incorrect pm lock state");

	/* Resume */
	gpio_emul_input_set(slp_l_gpio, slp_l_pin, 1);
	sleep(1);
	zassert_equal(hook_chip_suspend_cnt, 1,
		      "Incorrect suspend chip hook call count");
	zassert_equal(hook_chip_resume_cnt, 5,
		      "Incorrect resume chip hook call count");
	zassert_equal(pm_policy_state_lock_is_active(PM_STATE_SUSPEND_TO_IDLE,
						     PM_ALL_SUBSTATES),
		      1, "Incorrect pm lock state");
}

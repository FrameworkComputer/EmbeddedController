/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_tasks.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "power.h"
#include "power_button.h"
#include "system.h"
#include "task.h"
#include "test_state.h"
#include "timer.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(chipset_pre_init_hook);
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, chipset_pre_init_hook, HOOK_PRIO_DEFAULT);
FAKE_VOID_FUNC(chipset_startup_hook);
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, chipset_startup_hook, HOOK_PRIO_DEFAULT);
FAKE_VOID_FUNC(chipset_resume_hook);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, chipset_resume_hook, HOOK_PRIO_DEFAULT);
FAKE_VALUE_FUNC(int, system_jumped_late);

#define S5_INACTIVE_SEC 11
/* S5_INACTIVE_SEC + PMIC_HARD_OFF_DELAY 9.6 sec + 1 sec buffer */
#define POWER_OFF_DELAY_SEC 21

/* mt8186 is_held flag */
extern bool is_held;

static void set_signal_state(enum power_state state)
{
	const struct gpio_dt_spec *ap_ec_sysrst_odl =
		gpio_get_dt_spec(GPIO_AP_EC_SYSRST_ODL);
	const struct gpio_dt_spec *ap_in_sleep_l =
		gpio_get_dt_spec(GPIO_AP_IN_SLEEP_L);

	switch (state) {
	case POWER_S0:
		gpio_emul_input_set(ap_in_sleep_l->port, ap_in_sleep_l->pin, 1);
		gpio_emul_input_set(ap_ec_sysrst_odl->port,
				    ap_ec_sysrst_odl->pin, 1);
		break;
	case POWER_S3:
		gpio_emul_input_set(ap_in_sleep_l->port, ap_in_sleep_l->pin, 0);
		gpio_emul_input_set(ap_ec_sysrst_odl->port,
				    ap_ec_sysrst_odl->pin, 1);
		break;
	case POWER_G3:
		gpio_emul_input_set(ap_in_sleep_l->port, ap_in_sleep_l->pin, 0);
		gpio_emul_input_set(ap_ec_sysrst_odl->port,
				    ap_ec_sysrst_odl->pin, 0);
		break;
	default:
		zassert_unreachable("state %d not supported", state);
	}

	/* reset is_held flag */
	is_held = false;
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_SECONDS(1));
}

#ifdef CONFIG_AP_ARM_MTK_MT8188
static void pp4200_handler(const struct device *port, struct gpio_callback *cb,
			   gpio_port_pins_t pins)
{
	const struct gpio_dt_spec *en_pp4200_s5 =
		GPIO_DT_FROM_NODELABEL(en_pp4200_s5);
	const struct gpio_dt_spec *pg_pp4200_s5_od =
		GPIO_DT_FROM_NODELABEL(pg_pp4200_s5_od);
	int en = gpio_emul_output_get(en_pp4200_s5->port, en_pp4200_s5->pin);

	gpio_emul_input_set(pg_pp4200_s5_od->port, pg_pp4200_s5_od->pin, en);
}

static struct gpio_callback pp4200_callback = {
	.handler = pp4200_handler,
	.pin_mask = BIT(DT_GPIO_PIN(DT_NODELABEL(en_pp4200_s5), gpios)),
};

static void *power_seq_setup(void)
{
	zassert_ok(gpio_add_callback_dt(GPIO_DT_FROM_NODELABEL(en_pp4200_s5),
					&pp4200_callback));

	return NULL;
}

static void power_seq_teardown(void *f)
{
	zassert_ok(gpio_remove_callback_dt(GPIO_DT_FROM_NODELABEL(en_pp4200_s5),
					   &pp4200_callback));
}
#else
static void *power_seq_setup(void)
{
	return NULL;
}

static void power_seq_teardown(void *f)
{
}
#endif /* CONFIG_AP_ARM_MTK_MT8188 */

static void power_seq_before(void *f)
{
	/* Required for deferred callbacks to work */
	set_test_runner_tid();

	/* Start from G3 */
	power_set_state(POWER_G3);
	set_signal_state(POWER_G3);
	k_sleep(K_SECONDS(POWER_OFF_DELAY_SEC));

	RESET_FAKE(chipset_pre_init_hook);
	RESET_FAKE(chipset_startup_hook);
	RESET_FAKE(chipset_resume_hook);
	RESET_FAKE(system_jumped_late);
	FFF_RESET_HISTORY();
}

/* Normal boot sequence, G3 -> S3 -> S0 */
ZTEST(power_seq, test_power_state_machine)
{
	/* G3 -> S3 */
	power_set_state(POWER_G3);
	set_signal_state(POWER_S3);
	zassert_equal(power_get_state(), POWER_S3);

	/* S3 -> S0 */
	power_set_state(POWER_S3);
	set_signal_state(POWER_S0);
	zassert_equal(power_get_state(), POWER_S0);

	/* S0 -> G3 */
	power_set_state(POWER_S0);
	set_signal_state(POWER_G3);
	zassert_equal(power_get_state(), POWER_S5);
	k_sleep(K_SECONDS(S5_INACTIVE_SEC));
	zassert_equal(power_get_state(), POWER_G3);
}

/* Verify power btn short press can boot the device */
ZTEST(power_seq, test_power_btn_short_press)
{
	zassert_equal(power_get_state(), POWER_G3);

	power_button_simulate_press(100);
	k_sleep(K_SECONDS(1));

	/* Verify that power state machine is able to reach S5S3, and back to G3
	 * because power signal is not changed
	 */
	zassert_equal(chipset_pre_init_hook_fake.call_count, 1);
	zassert_equal(chipset_startup_hook_fake.call_count, 0);
	k_sleep(K_SECONDS(POWER_OFF_DELAY_SEC));
	zassert_equal(power_get_state(), POWER_G3);
}

/* Verify lid open can boot the device */
ZTEST(power_seq, test_lid_open)
{
	const struct gpio_dt_spec *lid_open = gpio_get_dt_spec(GPIO_LID_OPEN);

	gpio_emul_input_set(lid_open->port, lid_open->pin, 0);
	k_sleep(K_SECONDS(1));
	zassert_equal(power_get_state(), POWER_G3);

	gpio_emul_input_set(lid_open->port, lid_open->pin, 1);
	k_sleep(K_SECONDS(1));

	/* Verify that power state machine is able to reach S5S3, and back to G3
	 * because power signal is not changed
	 */
	zassert_equal(chipset_pre_init_hook_fake.call_count, 1);
	zassert_equal(chipset_startup_hook_fake.call_count, 0);
	k_sleep(K_SECONDS(POWER_OFF_DELAY_SEC));
	zassert_equal(power_get_state(), POWER_G3);
}

/* Suspend, S0 -> S3 -> S0 */
ZTEST(power_seq, test_host_sleep_success)
{
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT));

	/* Boot AP */
	set_signal_state(POWER_S0);
	zassert_equal(power_get_state(), POWER_S0);

	/* Suspend for 30 seconds */
	zassert_ok(ec_cmd_host_sleep_event(
		NULL, &(struct ec_params_host_sleep_event){
			      .sleep_event = HOST_SLEEP_EVENT_S3_SUSPEND }));
	k_sleep(K_MSEC(1));
	set_signal_state(POWER_S3);
	k_sleep(K_SECONDS(30));
	zassert_equal(power_get_state(), POWER_S3);

	/* Resume */
	set_signal_state(POWER_S0);
	zassert_ok(ec_cmd_host_sleep_event(
		NULL, &(struct ec_params_host_sleep_event){
			      .sleep_event = HOST_SLEEP_EVENT_S3_RESUME }));
	zassert_equal(power_get_state(), POWER_S0);

	/* Verify that EC_HOST_EVENT_HANG_DETECT is not triggered */
	zassert_false(host_is_event_set(EC_HOST_EVENT_HANG_DETECT));
}

/* Sleep hang, send EC_HOST_EVENT_HANG_DETECT */
ZTEST(power_seq, test_host_sleep_hang)
{
	host_clear_events(EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_DETECT));

	/* Boot AP */
	set_signal_state(POWER_S0);
	zassert_equal(power_get_state(), POWER_S0);

	/* Send HOST_SLEEP_EVENT_S3_SUSPEND and hang for 30 seconds */
	zassert_ok(ec_cmd_host_sleep_event(
		NULL, &(struct ec_params_host_sleep_event){
			      .sleep_event = HOST_SLEEP_EVENT_S3_SUSPEND }));
	k_sleep(K_SECONDS(30));

#if defined(SECTION_IS_RW)
	/* Verify that EC_HOST_EVENT_HANG_DETECT is triggered */
	zassert_true(host_is_event_set(EC_HOST_EVENT_HANG_DETECT));
#endif /* SECTION_IS_RW */
}

/* Shutdown from EC, S0 -> power key press (8 secs) -> S3S5 (8 secs) -> S5 -> G3
 */
ZTEST(power_seq, test_force_shutdown)
{
	const struct gpio_dt_spec *sys_rst_odl =
		gpio_get_dt_spec(GPIO_SYS_RST_ODL);
	const struct gpio_dt_spec *ec_pmic_en_odl =
		gpio_get_dt_spec(GPIO_EC_PMIC_EN_ODL);

	gpio_set_level(GPIO_SYS_RST_ODL, 1);
	gpio_set_level(GPIO_EC_PMIC_EN_ODL, 1);

	/* Boot AP */
	set_signal_state(POWER_S0);
	zassert_equal(power_get_state(), POWER_S0);

	/* Verify that ec resets ap and holds power button */
	chipset_force_shutdown(CHIPSET_SHUTDOWN_CONSOLE_CMD);
	k_sleep(K_SECONDS(1));
	zassert_equal(gpio_emul_output_get(sys_rst_odl->port, sys_rst_odl->pin),
		      0);

	/* Emulate AP power down (hw state G3, sw state unchanged),
	 * Verify power state stops at S5
	 */
	set_signal_state(POWER_G3);
	zassert_equal(power_get_state(), POWER_S3S5);
	zassert_equal(gpio_emul_output_get(ec_pmic_en_odl->port,
					   ec_pmic_en_odl->pin),
		      0);

	/* Wait 10 seconds for EC_PMIC_EN_ODL release and drop to S5 then G3 */
	k_sleep(K_SECONDS(10));
	zassert_equal(gpio_emul_output_get(sys_rst_odl->port, sys_rst_odl->pin),
		      0);
	zassert_equal(gpio_emul_output_get(ec_pmic_en_odl->port,
					   ec_pmic_en_odl->pin),
		      1);
	zassert_equal(power_get_state(), POWER_S5);
	k_sleep(K_SECONDS(S5_INACTIVE_SEC));
	zassert_equal(power_get_state(), POWER_G3);
}

/* Shutdown from AP, S0 -> powerkey hold (8 secs) -> S3S5 (8 secs) -> G3 */
ZTEST(power_seq, test_force_shutdown_button)
{
	const struct gpio_dt_spec *sys_rst_odl =
		gpio_get_dt_spec(GPIO_SYS_RST_ODL);
	const struct gpio_dt_spec *ec_pmic_en_odl =
		gpio_get_dt_spec(GPIO_EC_PMIC_EN_ODL);

	gpio_set_level(GPIO_SYS_RST_ODL, 1);
	gpio_set_level(GPIO_EC_PMIC_EN_ODL, 1);

	/* Boot AP */
	set_signal_state(POWER_S0);
	zassert_equal(power_get_state(), POWER_S0);

	power_button_simulate_press(10000); /* 10 seconds */
	zassert_equal(power_get_state(), POWER_S0);
	k_sleep(K_SECONDS(9)); /* AP off after 8 seconds */
	zassert_equal(gpio_emul_output_get(sys_rst_odl->port, sys_rst_odl->pin),
		      0);
	zassert_equal(gpio_emul_output_get(ec_pmic_en_odl->port,
					   ec_pmic_en_odl->pin),
		      0);

	zassert_equal(power_get_state(), POWER_S3S5);
	zassert_equal(gpio_emul_output_get(sys_rst_odl->port, sys_rst_odl->pin),
		      0);
	zassert_equal(gpio_emul_output_get(ec_pmic_en_odl->port,
					   ec_pmic_en_odl->pin),
		      0);

	k_sleep(K_SECONDS(5)); /* Wait for power button release */
	/* Signal has dropped, but PMIC_EN is still held */
	set_signal_state(POWER_G3);
	zassert_equal(power_get_state(), POWER_S3S5);
	zassert_equal(gpio_emul_output_get(ec_pmic_en_odl->port,
					   ec_pmic_en_odl->pin),
		      0);

	k_sleep(K_SECONDS(3)); /* Wait for S5 */

	/* PMIC_EN released */
	zassert_equal(power_get_state(), POWER_S5);
	zassert_equal(gpio_emul_output_get(ec_pmic_en_odl->port,
					   ec_pmic_en_odl->pin),
		      1);
	k_sleep(K_SECONDS(S5_INACTIVE_SEC)); /* Wait for G3 */
	zassert_equal(power_get_state(), POWER_G3);
}

/* AP reset (S0 -> S0).
 * Verify power state doesn't change during reset.
 */
ZTEST(power_seq, test_chipset_reset)
{
	const struct gpio_dt_spec *ap_ec_warm_rst_req =
		gpio_get_dt_spec(GPIO_AP_EC_WARM_RST_REQ);
	const struct gpio_dt_spec *ap_ec_sysrst_odl =
		gpio_get_dt_spec(GPIO_AP_EC_SYSRST_ODL);

	/* Boot AP */
	set_signal_state(POWER_S0);
	zassert_equal(power_get_state(), POWER_S0);
	RESET_FAKE(chipset_resume_hook);
	/* Clear reset reason */
	report_ap_reset(CHIPSET_RESET_UNKNOWN);

	/* Trigger AP reboot */
	gpio_emul_input_set(ap_ec_warm_rst_req->port, ap_ec_warm_rst_req->pin,
			    0);
	gpio_emul_input_set(ap_ec_warm_rst_req->port, ap_ec_warm_rst_req->pin,
			    1);

	/* Simulate sysrst toggle */
	gpio_emul_input_set(ap_ec_sysrst_odl->port, ap_ec_sysrst_odl->pin, 0);
	gpio_emul_input_set(ap_ec_sysrst_odl->port, ap_ec_sysrst_odl->pin, 1);
	k_sleep(K_SECONDS(1));

	/* Back to S0, verify that resume hook is not triggered */
	zassert_equal(power_get_state(), POWER_S0);
	zassert_equal(chipset_resume_hook_fake.call_count, 0);
	/* Also verify that chipset_reset_request_interrupt is called by
	 * checking its side-effect
	 */
	zassert_equal(chipset_get_shutdown_reason(), CHIPSET_RESET_AP_REQ);
}

/* AP reset during suspend (S3 -> S0).
 * Verify state reaches S0 with resume hook triggered.
 */
ZTEST(power_seq, test_chipset_reset_in_s3)
{
	const struct gpio_dt_spec *ap_ec_warm_rst_req =
		gpio_get_dt_spec(GPIO_AP_EC_WARM_RST_REQ);
	const struct gpio_dt_spec *ap_ec_sysrst_odl =
		gpio_get_dt_spec(GPIO_AP_EC_SYSRST_ODL);

	/* Boot AP */
	set_signal_state(POWER_S3);
	zassert_equal(power_get_state(), POWER_S3);
	RESET_FAKE(chipset_resume_hook);
	/* Clear reset reason */
	report_ap_reset(CHIPSET_RESET_UNKNOWN);

	/* Trigger AP reboot */
	gpio_emul_input_set(ap_ec_warm_rst_req->port, ap_ec_warm_rst_req->pin,
			    0);
	gpio_emul_input_set(ap_ec_warm_rst_req->port, ap_ec_warm_rst_req->pin,
			    1);

	/* Simulate sysrst toggle */
	gpio_emul_input_set(ap_ec_sysrst_odl->port, ap_ec_sysrst_odl->pin, 0);
	gpio_emul_input_set(ap_ec_sysrst_odl->port, ap_ec_sysrst_odl->pin, 1);
	set_signal_state(POWER_S0);

	/* Back to S0, verify that resume hook is triggered */
	zassert_equal(power_get_state(), POWER_S0);
	zassert_equal(chipset_resume_hook_fake.call_count, 1);
	/* Also verify that chipset_reset_request_interrupt is called by
	 * checking its side-effect
	 */
	zassert_equal(chipset_get_shutdown_reason(), CHIPSET_RESET_AP_REQ);
}

static void power_chipset_init_subtest(enum power_state signal_state,
				       bool jumped_late, uint32_t reset_flags,
				       enum power_state expected_state,
				       int line)
{
	const struct gpio_dt_spec *sys_rst_odl =
		gpio_get_dt_spec(GPIO_SYS_RST_ODL);

	set_signal_state(signal_state);

	system_jumped_late_fake.return_val = jumped_late;
	system_common_reset_state();
	system_set_reset_flags(reset_flags);

	power_set_state(power_chipset_init());

	RESET_FAKE(chipset_pre_init_hook);
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_SECONDS(1));

	if (signal_state == expected_state) {
		/* need 10 seconds to drop from s5 to g3 */
		k_sleep(K_SECONDS(S5_INACTIVE_SEC));

		/* Expect nothing changed */
		zassert_equal(chipset_pre_init_hook_fake.call_count, 0,
			      "test_power_chipset_init line %d failed", line);
		zassert_equal(power_get_state(), expected_state);
	} else if (expected_state == POWER_S0 && signal_state == POWER_G3) {
		/* Expect boot to S0 and fail at S5->S3 */
		k_sleep(K_SECONDS(POWER_OFF_DELAY_SEC));
		zassert_equal(chipset_pre_init_hook_fake.call_count, 1,
			      "test_power_chipset_init line %d failed", line);
	} else if (expected_state == POWER_G3 && signal_state == POWER_S0) {
		zassert_equal(gpio_emul_output_get(sys_rst_odl->port,
						   sys_rst_odl->pin),
			      0, "test_power_chipset_init line %d failed",
			      line);
	} else {
		zassert_unreachable();
	}
}

/* Verify initial state decision logic.
 * Combination that don't make sense (e.g. wake from hibernate but signal
 * state is already S0) are skipped.
 */
ZTEST(power_seq, test_power_chipset_init)
{
	const struct gpio_dt_spec *ac_present =
		gpio_get_dt_spec(GPIO_AC_PRESENT);

	/* system_jumped_late => ignore all flags and boot to S0 */
	power_chipset_init_subtest(POWER_G3, true, 0, POWER_S0, __LINE__);
	power_chipset_init_subtest(POWER_S0, true, 0, POWER_S0, __LINE__);
	power_chipset_init_subtest(POWER_G3, true, EC_RESET_FLAG_AP_OFF,
				   POWER_S0, __LINE__);
	power_chipset_init_subtest(POWER_S0, true, EC_RESET_FLAG_AP_OFF,
				   POWER_S0, __LINE__);
	power_chipset_init_subtest(POWER_G3, true, EC_RESET_FLAG_HIBERNATE,
				   POWER_S0, __LINE__);
	power_chipset_init_subtest(POWER_S0, true, EC_RESET_FLAG_HIBERNATE,
				   POWER_S0, __LINE__);
	power_chipset_init_subtest(POWER_G3, true, EC_RESET_FLAG_AP_IDLE,
				   POWER_G3, __LINE__);
	power_chipset_init_subtest(POWER_S0, true, EC_RESET_FLAG_AP_IDLE,
				   POWER_S0, __LINE__);

	/* No reset flag => always boot to S0 */
	power_chipset_init_subtest(POWER_G3, false, 0, POWER_S0, __LINE__);
	power_chipset_init_subtest(POWER_S0, false, 0, POWER_S0, __LINE__);

	/* AP off => stay at G3 */
	power_chipset_init_subtest(POWER_G3, false, EC_RESET_FLAG_AP_OFF,
				   POWER_G3, __LINE__);
	power_chipset_init_subtest(POWER_S0, false, EC_RESET_FLAG_AP_OFF,
				   POWER_G3, __LINE__);

	/* Boot from hibernate => stay at G3 */
	gpio_emul_input_set(ac_present->port, ac_present->pin, 1);
	power_chipset_init_subtest(POWER_G3, false, EC_RESET_FLAG_HIBERNATE,
				   POWER_G3, __LINE__);

	/* AP_IDLE => keep current state */
	power_chipset_init_subtest(POWER_G3, false, EC_RESET_FLAG_AP_IDLE,
				   POWER_G3, __LINE__);
	power_chipset_init_subtest(POWER_S0, false, EC_RESET_FLAG_AP_IDLE,
				   POWER_S0, __LINE__);
}

ZTEST_SUITE(power_seq, krabby_predicate_post_main, power_seq_setup,
	    power_seq_before, NULL, power_seq_teardown);

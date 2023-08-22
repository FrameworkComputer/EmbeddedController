/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Unit tests for program/nissa/src/board_power.c.
 */
#include <zephyr/drivers/espi.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/include/emul/emul_power_signals.h>
#include <zephyr/ztest.h>

#include <ap_power_override_functions.h>
#include <common.h>
#include <mock/ap_power_events.h>
#include <mock/power_signals.h>
#include <power_signals.h>

extern bool s0_stable;

int board_power_signal_get(enum power_signal);
int board_power_signal_set(enum power_signal, int);

DEFINE_FAKE_VALUE_FUNC(int, power_signal_get, enum power_signal);
DEFINE_FAKE_VALUE_FUNC(int, power_signal_set, enum power_signal, int);
DEFINE_FAKE_VALUE_FUNC(int, power_wait_mask_signals_timeout,
		       power_signal_mask_t, power_signal_mask_t, int);
FAKE_VALUE_FUNC(int, power_signal_enable, enum power_signal);
FAKE_VALUE_FUNC(int, power_signal_disable, enum power_signal);
FAKE_VOID_FUNC(ap_pwrseq_wake);

LOG_MODULE_REGISTER(ap_pwrseq, LOG_LEVEL_INF);

static void before_test(void *fixture)
{
	RESET_FAKE(power_signal_get);
	RESET_FAKE(power_signal_set);
}

ZTEST_SUITE(nissa_board_power, NULL, NULL, before_test, NULL, NULL);

ZTEST(nissa_board_power, test_power_signal_set)
{
	/* No settable signal is board-defined */
	zassert_equal(board_power_signal_set(PWR_EN_PP3300_A, 1), -EINVAL);
}

ZTEST(nissa_board_power, test_power_signal_get)
{
	const struct gpio_dt_spec *all_sys_pwrgd_in =
		GPIO_DT_FROM_NODELABEL(gpio_all_sys_pwrgd);

	/*
	 * ALL_SYS_PWRGD is asserted when SLP_S3 is deasserted, the
	 * corresponding GPIO is asserted, and PG_PP1P05 is asserted.
	 */
	power_signal_get_fake.return_val_seq = (int[]){ 0, 1 };
	power_signal_get_fake.return_val_seq_len = 2;
	zassert_ok(gpio_emul_input_set(all_sys_pwrgd_in->port,
				       all_sys_pwrgd_in->pin, 1));

	zassert_true(board_power_signal_get(PWR_ALL_SYS_PWRGD));
	zassert_equal(power_signal_get_fake.call_count, 2);
	zassert_equal(power_signal_get_fake.arg0_history[0], PWR_SLP_S3);
	zassert_equal(power_signal_get_fake.arg0_history[1], PWR_PG_PP1P05);

	/* PG_PP1P05 going away causes deassertion */
	power_signal_get_fake.return_val_seq = (int[]){ 0, 0 };
	power_signal_get_fake.return_val_seq_idx = 0;
	zassert_false(board_power_signal_get(PWR_ALL_SYS_PWRGD));

	/* all_sys_pwrgd GPIO going away also causes deassertion */
	power_signal_get_fake.return_val_seq_idx = 0;
	zassert_ok(gpio_emul_input_set(all_sys_pwrgd_in->port,
				       all_sys_pwrgd_in->pin, 0));
	zassert_false(board_power_signal_get(PWR_ALL_SYS_PWRGD));

	/* SLP_S3 being asserted also causes deassertion */
	power_signal_get_fake.return_val = 1;
	power_signal_get_fake.return_val_seq_len = 0;
	zassert_false(board_power_signal_get(PWR_ALL_SYS_PWRGD));

	/* Other signals are invalid */
	zassert_equal(board_power_signal_get(PWR_EN_PP3300_A), -EINVAL);
}

static int fake_get_signal_dsw_pwrok_asserted(enum power_signal signal)
{
	return signal == PWR_DSW_PWROK;
}

ZTEST(nissa_board_power, test_g3_s5_action)
{
	/*
	 * DSW_PWROK (PP3300_A power good) is asserted, to be copied to
	 * DSW_PWROK output to SoC. This uses power_wait_signals internally
	 * and may call power_signal_get() many times, so we use a custom fake
	 * rather than specifying a sequence.
	 */
	power_signal_get_fake.custom_fake = fake_get_signal_dsw_pwrok_asserted;

	board_ap_power_action_g3_s5();
	/* Rails were turned on, and DSW_PWROK to SoC asserted */
	zassert_equal(power_signal_set_fake.call_count, 3,
		      "actual call count was %d",
		      power_signal_set_fake.call_count);
	zassert_equal(power_signal_set_fake.arg0_history[0], PWR_EN_PP5000_A);
	zassert_true(power_signal_set_fake.arg1_history[0]);
	zassert_equal(power_signal_set_fake.arg0_history[1], PWR_EN_PP3300_A);
	zassert_true(power_signal_set_fake.arg1_history[1]);
	zassert_equal(power_signal_set_fake.arg0_history[2],
		      PWR_EC_SOC_DSW_PWROK);
	zassert_true(power_signal_set_fake.arg1_history[2]);
}

ZTEST(nissa_board_power, test_rails_enabled)
{
	power_signal_get_fake.return_val = true;
	zassert_true(board_ap_power_check_power_rails_enabled());
	zassert_equal(power_signal_get_fake.call_count, 3);
	zassert_equal(power_signal_get_fake.arg0_history[0], PWR_EN_PP3300_A);
	zassert_equal(power_signal_get_fake.arg0_history[1], PWR_EN_PP5000_A);
	zassert_equal(power_signal_get_fake.arg0_history[2],
		      PWR_EC_SOC_DSW_PWROK);

	power_signal_get_fake.return_val = false;
	zassert_false(board_ap_power_check_power_rails_enabled());
	zassert_equal(power_signal_get_fake.arg0_val, PWR_EN_PP3300_A);
}

ZTEST(nissa_board_power, test_assert_pch_pwrok)
{
	zassert_ok(board_ap_power_assert_pch_power_ok());
	zassert_equal(power_signal_set_fake.arg0_val, PWR_PCH_PWROK);
	zassert_equal(power_signal_set_fake.arg1_val, 1);
}

ZTEST(nissa_board_power, test_s0_entry_exit)
{
	/* Up from S3 simply flags that we're not yet in S0 */
	s0_stable = true;
	board_ap_power_action_s3_s0();
	zassert_false(s0_stable);

	/* Once stable in S0, flag is set */
	board_ap_power_action_s0();
	zassert_true(s0_stable);
	/* Still set if runs again for some reason */
	board_ap_power_action_s0();
	zassert_true(s0_stable);

	/* Back to S3 is no longer S0 */
	board_ap_power_action_s0_s3();
	zassert_false(s0_stable);
}

ZTEST(nissa_board_power, test_force_shutdown)
{
	int signal_get_results[] = {
		0, /* RSMRST still deasserted */
		0, /* SLP_SUS also still deasserted */
		1, /* RSMRST asserted after a short delay */
		1, /* SLP_SUS for logging */
		0, /* RSMRST again for logging */
		1, /* DSW_PWROK still asserted */
		0, /* deasserts after a short delay */
		0, /* again for logging */
	};
	const enum power_signal signal_get_signals[] = {
		PWR_RSMRST, PWR_SLP_SUS,   PWR_RSMRST,	  PWR_SLP_SUS,
		PWR_RSMRST, PWR_DSW_PWROK, PWR_DSW_PWROK, PWR_DSW_PWROK,
	};
	BUILD_ASSERT(ARRAY_SIZE(signal_get_results) ==
		     ARRAY_SIZE(signal_get_signals));

	s0_stable = true;
	power_signal_get_fake.return_val_seq = signal_get_results;
	power_signal_get_fake.return_val_seq_len =
		ARRAY_SIZE(signal_get_results);

	board_ap_power_force_shutdown();
	zassert_false(s0_stable);

	/* Turned things off in the expected order */
	zassert_equal(power_signal_set_fake.call_count, 4);
	zassert_mem_equal(
		power_signal_set_fake.arg0_history,
		((enum power_signal[]){ PWR_EC_SOC_DSW_PWROK, PWR_EC_PCH_RSMRST,
					PWR_EN_PP3300_A, PWR_EN_PP5000_A }),
		4);
	zassert_mem_equal(power_signal_set_fake.arg1_history,
			  ((int[]){ 0, 0, 0, 0 }), 4,
			  "Output signals were not only deasserted");

	/*
	 * Signals were read in the expected order (the return values were
	 * treated as intended and not as other unexpected values).
	 */
	zassert_equal(power_signal_get_fake.call_count,
		      ARRAY_SIZE(signal_get_results),
		      "recorded %d calls but expected %d",
		      power_signal_get_fake.call_count,
		      ARRAY_SIZE(signal_get_results));
	zassert_mem_equal(power_signal_get_fake.arg0_history,
			  signal_get_signals, ARRAY_SIZE(signal_get_results));
}

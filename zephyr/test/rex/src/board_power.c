/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio/gpio.h"
#include "gpio_signal.h"
#include "timer.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#include <ap_power/ap_power_interface.h>
#include <ap_power_override_functions.h>
#include <mock/ap_power_events.h>
#include <mock/power_signals.h>
#include <power_signals.h>
#include <x86_power_signals.h>

#define X86_NON_DSX_MTL_FORCE_SHUTDOWN_TO_MS 50

DEFINE_FAKE_VALUE_FUNC(int, power_signal_set, enum power_signal, int);
DEFINE_FAKE_VALUE_FUNC(int, power_signal_get, enum power_signal);
DEFINE_FAKE_VALUE_FUNC(int, power_wait_mask_signals_timeout,
		       power_signal_mask_t, power_signal_mask_t, int);
DEFINE_FAKE_VOID_FUNC(ap_power_ev_send_callbacks, enum ap_power_events);

int mock_power_signal_set_ap_force_shutdown(enum power_signal signal, int value)
{
	if (power_signal_set_fake.call_count == 1) {
		zassert_true(signal == PWR_EC_PCH_RSMRST && value == 0,
			     "First call signal: %d, value: %d", signal, value);
		return 0;
	} else if (power_signal_set_fake.call_count == 2) {
		zassert_true(signal == PWR_EN_PP3300_A && value == 0,
			     "Second call signal: %d, value: %d", signal,
			     value);
		return 0;
	}

	zassert_unreachable("Wrong input received");
	return -1;
}

int mock_power_signal_set_ap_power_action_g3_s5(enum power_signal signal,
						int value)
{
	if ((signal == PWR_EN_PP3300_A) && (value == 1)) {
		return 0;
	}

	zassert_unreachable("Wrong input received");
	return -1;
}

int mock_power_signal_get_ap_force_shutdown_retries(enum power_signal signal)
{
	if (signal == PWR_RSMRST) {
		return 1;
	}
	zassert_unreachable("Wrong input received");
	return -1;
}

int mock_power_signal_get_ap_force_shutdown(enum power_signal signal)
{
	if (signal == PWR_RSMRST) {
		if (power_signal_get_fake.call_count <= 5) {
			return 1;
		}
		return 0;
	}
	zassert_unreachable("Wrong input received");
	return -1;
}

int mock_power_signal_get_check_power_rails_enabled_0(enum power_signal signal)
{
	if (signal == PWR_EN_PP3300_A) {
		return 0;
	}
	zassert_unreachable("Wrong input received");
	return -1;
}

int mock_power_signal_get_check_power_rails_enabled_1(enum power_signal signal)
{
	if (signal == PWR_EN_PP3300_A) {
		return 1;
	}
	zassert_unreachable("Wrong input received");
	return -1;
}

int mock_power_wait_mask_signals_timeout_0(power_signal_mask_t want,
					   power_signal_mask_t mask,
					   int timeout)
{
	zassert_equal(want, IN_PGOOD_ALL_CORE);
	zassert_equal(mask, IN_PGOOD_ALL_CORE);
	zassert_equal(timeout, AP_PWRSEQ_DT_VALUE(wait_signal_timeout));
	return 0;
}

int mock_power_wait_mask_signals_timeout_1(power_signal_mask_t want,
					   power_signal_mask_t mask,
					   int timeout)
{
	zassert_equal(want, IN_PGOOD_ALL_CORE);
	zassert_equal(mask, IN_PGOOD_ALL_CORE);
	zassert_equal(timeout, AP_PWRSEQ_DT_VALUE(wait_signal_timeout));
	return 1;
}

void mock_ap_power_ev_send_callbacks(enum ap_power_events event)
{
	zassert_equal(event, AP_POWER_PRE_INIT);
}

static void board_power_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(power_signal_set);
	RESET_FAKE(power_signal_get);
	RESET_FAKE(power_wait_mask_signals_timeout);
	RESET_FAKE(ap_power_ev_send_callbacks);
}

ZTEST_USER(board_power, test_board_ap_power_force_shutdown)
{
	power_signal_set_fake.custom_fake =
		mock_power_signal_set_ap_force_shutdown;
	power_signal_get_fake.custom_fake =
		mock_power_signal_get_ap_force_shutdown;
	board_ap_power_force_shutdown();

	zassert_equal(2, power_signal_set_fake.call_count);
	zassert_equal(7, power_signal_get_fake.call_count);
}

ZTEST_USER(board_power, test_board_ap_power_force_shutdown_timeout)
{
	power_signal_set_fake.custom_fake =
		mock_power_signal_set_ap_force_shutdown;
	power_signal_get_fake.custom_fake =
		mock_power_signal_get_ap_force_shutdown_retries;

	const uint32_t start_ms = k_uptime_get();

	board_ap_power_force_shutdown();

	const uint32_t end_ms = k_uptime_get();

	zassert_equal(power_signal_set_fake.call_count, 2);
	zassert_true((end_ms - start_ms) >=
		     X86_NON_DSX_MTL_FORCE_SHUTDOWN_TO_MS);
	zassert_true(power_signal_get_fake.call_count > 2);
}

ZTEST_USER(board_power, test_board_ap_power_check_power_rails_enabled_0)
{
	power_signal_get_fake.custom_fake =
		mock_power_signal_get_check_power_rails_enabled_0;

	zassert_equal(0, board_ap_power_check_power_rails_enabled());
	zassert_equal(1, power_signal_get_fake.call_count);
}

ZTEST_USER(board_power, test_board_ap_power_check_power_rails_enabled_1)
{
	power_signal_get_fake.custom_fake =
		mock_power_signal_get_check_power_rails_enabled_1;

	zassert_equal(1, board_ap_power_check_power_rails_enabled());
	zassert_equal(1, power_signal_get_fake.call_count);
}

ZTEST_USER(board_power, test_board_ap_power_action_g3_s5_0)
{
	power_signal_set_fake.custom_fake =
		mock_power_signal_set_ap_power_action_g3_s5;
	power_wait_mask_signals_timeout_fake.custom_fake =
		mock_power_wait_mask_signals_timeout_0;
	ap_power_ev_send_callbacks_fake.custom_fake =
		mock_ap_power_ev_send_callbacks;

	board_ap_power_action_g3_s5();

	zassert_equal(1, power_signal_set_fake.call_count);
	zassert_equal(1, power_wait_mask_signals_timeout_fake.call_count);
	zassert_equal(1, ap_power_ev_send_callbacks_fake.call_count);
}

ZTEST_USER(board_power, test_board_ap_power_action_g3_s5_1)
{
	power_signal_set_fake.custom_fake =
		mock_power_signal_set_ap_power_action_g3_s5;
	power_wait_mask_signals_timeout_fake.custom_fake =
		mock_power_wait_mask_signals_timeout_1;
	ap_power_ev_send_callbacks_fake.custom_fake =
		mock_ap_power_ev_send_callbacks;

	board_ap_power_action_g3_s5();

	zassert_equal(1, power_signal_set_fake.call_count);
	zassert_equal(1, power_wait_mask_signals_timeout_fake.call_count);
	zassert_equal(0, ap_power_ev_send_callbacks_fake.call_count);
}

ZTEST_SUITE(board_power, NULL, NULL, board_power_before, NULL, NULL);

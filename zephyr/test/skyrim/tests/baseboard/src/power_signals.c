/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "ap_power/ap_power.h"
#include "charger.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "ioexpander.h"
#include "power.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

void baseboard_suspend_change(struct ap_power_ev_callback *cb,
			      struct ap_power_ev_data data);
void baseboard_init(void);
void baseboard_set_soc_pwr_pgood(enum gpio_signal unused);
bool board_supports_pcore_ocp(void);
void board_pwrbtn_to_pch(int level);
void baseboard_s0_pgood(enum gpio_signal signal);
void baseboard_set_en_pwr_pcore(enum gpio_signal unused);
void baseboard_en_pwr_s0(enum gpio_signal signal);
void baseboard_set_en_pwr_s3(enum gpio_signal signal);
void baseboard_s5_pgood(enum gpio_signal signal);

void baseboard_soc_thermtrip(enum gpio_signal signal);
void baseboard_soc_pcore_ocp(enum gpio_signal signal);

FAKE_VOID_FUNC(chipset_force_shutdown, enum chipset_shutdown_reason);
FAKE_VALUE_FUNC(int, extpower_is_present);
FAKE_VOID_FUNC(print_charger_prochot, int);
FAKE_VOID_FUNC(power_signal_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(power_interrupt_handler, enum gpio_signal);

int chipset_in_state(int mask)
{
	return mask & CHIPSET_STATE_ON;
}

static int gpio_emul_output_get_dt(const struct gpio_dt_spec *dt)
{
	return gpio_emul_output_get(dt->port, dt->pin);
}

static int gpio_emul_input_set_dt(const struct gpio_dt_spec *dt, int value)
{
	return gpio_emul_input_set(dt->port, dt->pin, value);
}

/* Toggles the pin and checks that the interrupt handler was called. */
int test_interrupt(const struct gpio_dt_spec *dt)
{
	int rv;
	int old_count = power_interrupt_handler_fake.call_count;

	rv = gpio_emul_input_set_dt(dt, 0);
	if (rv)
		return rv;

	rv = gpio_emul_input_set_dt(dt, 1);
	if (rv)
		return rv;

	rv = gpio_emul_input_set_dt(dt, 1);
	if (rv)
		return rv;

	return power_interrupt_handler_fake.call_count <= old_count;
}

static void power_signals_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(chipset_force_shutdown);
	RESET_FAKE(extpower_is_present);
	RESET_FAKE(print_charger_prochot);
	RESET_FAKE(power_signal_interrupt);
	RESET_FAKE(power_interrupt_handler);
}

ZTEST_SUITE(power_signals, NULL, NULL, power_signals_before, NULL, NULL);

ZTEST(power_signals, test_baseboard_suspend_change)
{
	const struct gpio_dt_spec *gpio_ec_disable_disp_bl =
		GPIO_DT_FROM_NODELABEL(gpio_ec_disable_disp_bl);

	struct ap_power_ev_data data;

	data.event = AP_POWER_SUSPEND;
	baseboard_suspend_change(NULL, data);
	zassert_true(gpio_emul_output_get_dt(gpio_ec_disable_disp_bl));

	data.event = AP_POWER_RESUME;
	baseboard_suspend_change(NULL, data);
	zassert_false(gpio_emul_output_get_dt(gpio_ec_disable_disp_bl));
}

ZTEST(power_signals, test_baseboard_shutdown_change)
{
	const struct gpio_dt_spec *usb_a1_retimer_en =
		GPIO_DT_FROM_NODELABEL(usb_a1_retimer_en);

	struct ap_power_ev_data data;

	data.event = AP_POWER_SHUTDOWN;
	baseboard_suspend_change(NULL, data);
	zassert_false(gpio_emul_output_get_dt(usb_a1_retimer_en));

	data.event = AP_POWER_STARTUP;
	baseboard_suspend_change(NULL, data);
	zassert_true(gpio_emul_output_get_dt(usb_a1_retimer_en));
}

ZTEST(power_signals, test_baseboard_init)
{
	const struct gpio_dt_spec *gpio_pg_groupc_s0_od =
		GPIO_DT_FROM_NODELABEL(gpio_pg_groupc_s0_od);
	const struct gpio_dt_spec *gpio_pg_lpddr5_s0_od =
		GPIO_DT_FROM_NODELABEL(gpio_pg_lpddr5_s0_od);
	const struct gpio_dt_spec *gpio_pg_lpddr5_s3_od =
		GPIO_DT_FROM_NODELABEL(gpio_pg_lpddr5_s3_od);
	const struct gpio_dt_spec *gpio_soc_thermtrip_odl =
		GPIO_DT_FROM_NODELABEL(gpio_soc_thermtrip_odl);
	const struct gpio_dt_spec *gpio_sfh_ec_int_h =
		GPIO_DT_FROM_NODELABEL(gpio_sfh_ec_int_h);
	const struct gpio_dt_spec *gpio_prochot_odl =
		GPIO_DT_FROM_NODELABEL(gpio_prochot_odl);

	baseboard_init();

	/* Trigger interrupts to validate that they've been enabled. */
	/* These interrupts use the generic test handler. */
	zassert_ok(test_interrupt(gpio_pg_groupc_s0_od));
	zassert_ok(test_interrupt(gpio_pg_lpddr5_s0_od));
	zassert_ok(test_interrupt(gpio_pg_lpddr5_s3_od));
	zassert_ok(test_interrupt(gpio_sfh_ec_int_h));

	/* Verify that the thermal trip interrupt triggers a shutdown. */
	zassert_ok(gpio_emul_input_set_dt(gpio_soc_thermtrip_odl, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_soc_thermtrip_odl, 0));
	zassert_equal(chipset_force_shutdown_fake.call_count, 1);
	zassert_equal(chipset_force_shutdown_fake.arg0_val,
		      CHIPSET_SHUTDOWN_THERMAL);

	/* Test that our prochot handler prints out charger info. */
	zassert_ok(gpio_emul_input_set_dt(gpio_prochot_odl, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_prochot_odl, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_prochot_odl, 1));
	/* Give plenty of time for the deferred logic to run. */
	k_msleep(500);
	zassert_equal(print_charger_prochot_fake.call_count, 1);
}

ZTEST(power_signals, test_baseboard_set_soc_pwr_pgood)
{
	const struct gpio_dt_spec *gpio_en_pwr_pcore_s0_r =
		GPIO_DT_FROM_NODELABEL(gpio_en_pwr_pcore_s0_r);
	const struct gpio_dt_spec *gpio_pg_lpddr5_s0_od =
		GPIO_DT_FROM_NODELABEL(gpio_pg_lpddr5_s0_od);
	const struct gpio_dt_spec *gpio_s0_pgood =
		GPIO_DT_FROM_NODELABEL(gpio_s0_pgood);

	const struct gpio_dt_spec *gpio_ec_soc_pwr_good =
		GPIO_DT_FROM_NODELABEL(gpio_ec_soc_pwr_good);

	/* Test all combinations of these power pins. */
	zassert_ok(gpio_pin_set_dt(gpio_en_pwr_pcore_s0_r, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s0_od, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_s0_pgood, 0));
	baseboard_set_soc_pwr_pgood(0);
	zassert_false(gpio_emul_output_get_dt(gpio_ec_soc_pwr_good));

	zassert_ok(gpio_pin_set_dt(gpio_en_pwr_pcore_s0_r, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s0_od, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_s0_pgood, 0));
	baseboard_set_soc_pwr_pgood(0);
	zassert_false(gpio_emul_output_get_dt(gpio_ec_soc_pwr_good));

	zassert_ok(gpio_pin_set_dt(gpio_en_pwr_pcore_s0_r, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s0_od, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_s0_pgood, 0));
	baseboard_set_soc_pwr_pgood(0);
	zassert_false(gpio_emul_output_get_dt(gpio_ec_soc_pwr_good));

	zassert_ok(gpio_pin_set_dt(gpio_en_pwr_pcore_s0_r, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s0_od, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_s0_pgood, 0));
	baseboard_set_soc_pwr_pgood(0);
	zassert_false(gpio_emul_output_get_dt(gpio_ec_soc_pwr_good));

	zassert_ok(gpio_pin_set_dt(gpio_en_pwr_pcore_s0_r, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s0_od, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_s0_pgood, 1));
	baseboard_set_soc_pwr_pgood(0);
	zassert_false(gpio_emul_output_get_dt(gpio_ec_soc_pwr_good));

	zassert_ok(gpio_pin_set_dt(gpio_en_pwr_pcore_s0_r, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s0_od, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_s0_pgood, 1));
	baseboard_set_soc_pwr_pgood(0);
	zassert_false(gpio_emul_output_get_dt(gpio_ec_soc_pwr_good));

	zassert_ok(gpio_pin_set_dt(gpio_en_pwr_pcore_s0_r, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s0_od, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_s0_pgood, 1));
	baseboard_set_soc_pwr_pgood(0);
	zassert_false(gpio_emul_output_get_dt(gpio_ec_soc_pwr_good));

	zassert_ok(gpio_pin_set_dt(gpio_en_pwr_pcore_s0_r, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s0_od, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_s0_pgood, 1));
	baseboard_set_soc_pwr_pgood(0);
	zassert_true(gpio_emul_output_get_dt(gpio_ec_soc_pwr_good));
}

ZTEST(power_signals, test_board_supports_pcore_ocp)
{
	zassert_true(board_supports_pcore_ocp());
}

ZTEST(power_signals, test_baseboard_set_en_pwr_pcore)
{
	const struct gpio_dt_spec *gpio_pg_lpddr5_s3_od =
		GPIO_DT_FROM_NODELABEL(gpio_pg_lpddr5_s3_od);
	const struct gpio_dt_spec *gpio_pg_groupc_s0_od =
		GPIO_DT_FROM_NODELABEL(gpio_pg_groupc_s0_od);
	const struct gpio_dt_spec *gpio_en_pwr_s0_r =
		GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s0_r);

	const struct gpio_dt_spec *gpio_en_pwr_pcore_s0_r =
		GPIO_DT_FROM_NODELABEL(gpio_en_pwr_pcore_s0_r);

	/* Test all combinations of these power pins. */
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s3_od, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_groupc_s0_od, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_en_pwr_s0_r, 0));
	baseboard_set_en_pwr_pcore(0);
	zassert_false(gpio_emul_output_get_dt(gpio_en_pwr_pcore_s0_r));

	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s3_od, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_groupc_s0_od, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_en_pwr_s0_r, 0));
	baseboard_set_en_pwr_pcore(0);
	zassert_false(gpio_emul_output_get_dt(gpio_en_pwr_pcore_s0_r));

	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s3_od, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_groupc_s0_od, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_en_pwr_s0_r, 0));
	baseboard_set_en_pwr_pcore(0);
	zassert_false(gpio_emul_output_get_dt(gpio_en_pwr_pcore_s0_r));

	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s3_od, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_groupc_s0_od, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_en_pwr_s0_r, 0));
	baseboard_set_en_pwr_pcore(0);
	zassert_false(gpio_emul_output_get_dt(gpio_en_pwr_pcore_s0_r));

	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s3_od, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_groupc_s0_od, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_en_pwr_s0_r, 1));
	baseboard_set_en_pwr_pcore(0);
	zassert_false(gpio_emul_output_get_dt(gpio_en_pwr_pcore_s0_r));

	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s3_od, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_groupc_s0_od, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_en_pwr_s0_r, 1));
	baseboard_set_en_pwr_pcore(0);
	zassert_false(gpio_emul_output_get_dt(gpio_en_pwr_pcore_s0_r));

	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s3_od, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_groupc_s0_od, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_en_pwr_s0_r, 1));
	baseboard_set_en_pwr_pcore(0);
	zassert_false(gpio_emul_output_get_dt(gpio_en_pwr_pcore_s0_r));

	zassert_ok(gpio_emul_input_set_dt(gpio_pg_lpddr5_s3_od, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_groupc_s0_od, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_en_pwr_s0_r, 1));
	baseboard_set_en_pwr_pcore(0);
	zassert_true(gpio_emul_output_get_dt(gpio_en_pwr_pcore_s0_r));
}

ZTEST(power_signals, test_baseboard_en_pwr_s0)
{
	const struct gpio_dt_spec *gpio_slp_s3_l =
		GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l);
	const struct gpio_dt_spec *gpio_pg_pwr_s5 =
		GPIO_DT_FROM_NODELABEL(gpio_pg_pwr_s5);

	const struct gpio_dt_spec *gpio_en_pwr_s0_r =
		GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s0_r);

	/* Test all combinations of these power pins. */
	zassert_ok(gpio_emul_input_set_dt(gpio_slp_s3_l, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_pwr_s5, 0));
	baseboard_en_pwr_s0(0);
	zassert_false(gpio_emul_output_get_dt(gpio_en_pwr_s0_r));

	zassert_ok(gpio_emul_input_set_dt(gpio_slp_s3_l, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_pwr_s5, 0));
	baseboard_en_pwr_s0(0);
	zassert_false(gpio_emul_output_get_dt(gpio_en_pwr_s0_r));

	zassert_ok(gpio_emul_input_set_dt(gpio_slp_s3_l, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_pwr_s5, 1));
	baseboard_en_pwr_s0(0);
	zassert_false(gpio_emul_output_get_dt(gpio_en_pwr_s0_r));

	zassert_ok(gpio_emul_input_set_dt(gpio_slp_s3_l, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_pwr_s5, 1));
	baseboard_en_pwr_s0(0);
	zassert_true(gpio_emul_output_get_dt(gpio_en_pwr_s0_r));

	/* Ensure we always are chaining off the normal handler. */
	zassert_equal(power_signal_interrupt_fake.call_count, 4);
}

ZTEST(power_signals, test_baseboard_set_en_pwr_s3)
{
	const struct gpio_dt_spec *gpio_slp_s5_l =
		GPIO_DT_FROM_NODELABEL(gpio_slp_s5_l);

	const struct gpio_dt_spec *gpio_en_pwr_s3 =
		GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s3);

	zassert_ok(gpio_emul_input_set_dt(gpio_slp_s5_l, 0));
	baseboard_set_en_pwr_s3(0);
	zassert_false(gpio_emul_output_get_dt(gpio_en_pwr_s3));

	zassert_ok(gpio_emul_input_set_dt(gpio_slp_s5_l, 1));
	baseboard_set_en_pwr_s3(0);
	zassert_true(gpio_emul_output_get_dt(gpio_en_pwr_s3));

	/* Ensure we always are chaining off the normal handler. */
	zassert_equal(power_signal_interrupt_fake.call_count, 2);
}

ZTEST(power_signals, test_baseboard_s5_pgood)
{
	const struct gpio_dt_spec *gpio_pg_pwr_s5 =
		GPIO_DT_FROM_NODELABEL(gpio_pg_pwr_s5);
	const struct gpio_dt_spec *gpio_hub_rst =
		GPIO_DT_FROM_NODELABEL(gpio_hub_rst);

	/* Test the path that waits 30 ms after S5 PGOOD. */
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_pwr_s5, 1));
	baseboard_s5_pgood(0);
	k_msleep(300);
	zassert_false(gpio_emul_output_get_dt(gpio_hub_rst));

	/* Test other path. */
	zassert_ok(gpio_emul_input_set_dt(gpio_pg_pwr_s5, 0));
	baseboard_s5_pgood(0);
	zassert_true(gpio_emul_output_get_dt(gpio_hub_rst));
}

static void set_rsmrst_l(struct k_work *work)
{
	const struct gpio_dt_spec *gpio_ec_soc_rsmrst_l =
		GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l);

	k_msleep(10);
	gpio_emul_input_set_dt(gpio_ec_soc_rsmrst_l, 1);
}
K_WORK_DEFINE(set_rsmrst_l_work, set_rsmrst_l);

ZTEST(power_signals, test_board_pwrbtn_to_pch)
{
	const struct gpio_dt_spec *gpio_ec_soc_rsmrst_l =
		GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l);
	const struct gpio_dt_spec *gpio_ec_soc_pwr_btn_l =
		GPIO_DT_FROM_NODELABEL(gpio_ec_soc_pwr_btn_l);

	/* Test delay when asserting PWRBTN_L and RSMRST_L are low. */
	zassert_ok(gpio_emul_input_set_dt(gpio_ec_soc_rsmrst_l, 0));
	k_work_submit(&set_rsmrst_l_work);
	board_pwrbtn_to_pch(0);
	zassert_false(gpio_emul_output_get_dt(gpio_ec_soc_pwr_btn_l));

	/* Test timeout. */
	zassert_ok(gpio_emul_input_set_dt(gpio_ec_soc_rsmrst_l, 0));
	board_pwrbtn_to_pch(0);
	zassert_false(gpio_emul_output_get_dt(gpio_ec_soc_pwr_btn_l));

	/* Test when PWRBTN_L is not asserted. */
	board_pwrbtn_to_pch(1);
	zassert_true(gpio_emul_output_get_dt(gpio_ec_soc_pwr_btn_l));
}

ZTEST(power_signals, test_baseboard_soc_pcore_ocp)
{
	baseboard_soc_pcore_ocp(0);
	zassert_equal(chipset_force_shutdown_fake.call_count, 1);
	zassert_equal(chipset_force_shutdown_fake.arg0_val,
		      CHIPSET_SHUTDOWN_BOARD_CUSTOM);
}

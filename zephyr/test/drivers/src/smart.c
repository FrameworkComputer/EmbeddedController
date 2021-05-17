/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>

#include "common.h"
#include "i2c.h"
#include "emul/emul_smart_battery.h"

#include "battery.h"
#include "battery_smart.h"

#define BATTERY_ORD	DT_DEP_ORD(DT_NODELABEL(battery))

/** Test all simple getters */
static void test_battery_getters(void)
{
	struct sbat_emul_bat_data *bat;
	struct i2c_emul *emul;
	char block[32];
	int expected;
	int word;

	emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(emul);

	zassert_equal(EC_SUCCESS, battery_get_mode(&word), NULL);
	zassert_equal(bat->mode, word, "%d != %d", bat->mode, word);

	expected = 100 * bat->cap / bat->design_cap;
	zassert_equal(EC_SUCCESS, battery_state_of_charge_abs(&word), NULL);
	zassert_equal(expected, word, "%d != %d", expected, word);

	zassert_equal(EC_SUCCESS, battery_remaining_capacity(&word), NULL);
	zassert_equal(bat->cap, word, "%d != %d", bat->cap, word);
	zassert_equal(EC_SUCCESS, battery_full_charge_capacity(&word), NULL);
	zassert_equal(bat->full_cap, word, "%d != %d", bat->full_cap, word);
	zassert_equal(EC_SUCCESS, battery_cycle_count(&word), NULL);
	zassert_equal(bat->cycle_count, word, "%d != %d",
		      bat->cycle_count, word);
	zassert_equal(EC_SUCCESS, battery_design_capacity(&word), NULL);
	zassert_equal(bat->design_cap, word, "%d != %d", bat->design_cap, word);
	zassert_equal(EC_SUCCESS, battery_design_voltage(&word), NULL);
	zassert_equal(bat->design_mv, word, "%d != %d", bat->design_mv, word);
	zassert_equal(EC_SUCCESS, battery_serial_number(&word), NULL);
	zassert_equal(bat->sn, word, "%d != %d", bat->sn, word);
	zassert_equal(EC_SUCCESS, get_battery_manufacturer_name(block, 32),
		      NULL);
	zassert_mem_equal(block, bat->mf_name, bat->mf_name_len,
			  "%s != %s", block, bat->mf_name);
	zassert_equal(EC_SUCCESS, battery_device_name(block, 32), NULL);
	zassert_mem_equal(block, bat->dev_name, bat->dev_name_len,
			  "%s != %s", block, bat->dev_name);
	zassert_equal(EC_SUCCESS, battery_device_chemistry(block, 32), NULL);
	zassert_mem_equal(block, bat->dev_chem, bat->dev_chem_len,
			  "%s != %s", block, bat->dev_chem);
	word = battery_get_avg_current();
	zassert_equal(bat->avg_cur, word, "%d != %d", bat->avg_cur, word);

	bat->avg_cur = 200;
	expected = (bat->full_cap - bat->cap) * 60 / bat->avg_cur;
	zassert_equal(EC_SUCCESS, battery_time_to_full(&word), NULL);
	zassert_equal(expected, word, "%d != %d", expected, word);

	bat->cur = -200;
	expected = bat->cap * 60 / (-bat->cur);
	zassert_equal(EC_SUCCESS, battery_run_time_to_empty(&word), NULL);
	zassert_equal(expected, word, "%d != %d", expected, word);

	bat->avg_cur = -200;
	expected = bat->cap * 60 / (-bat->avg_cur);
	zassert_equal(EC_SUCCESS, battery_time_to_empty(&word), NULL);
	zassert_equal(expected, word, "%d != %d", expected, word);
}

/** Test battery status */
static void test_battery_status(void)
{
	struct sbat_emul_bat_data *bat;
	struct i2c_emul *emul;
	int expected;
	int status;

	emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(emul);

	bat->status = 0;
	bat->cur = -200;
	bat->cap_alarm = 0;
	bat->time_alarm = 0;
	bat->cap = bat->full_cap / 2;
	bat->error_code = STATUS_CODE_OVERUNDERFLOW;

	expected = 0;
	expected |= STATUS_DISCHARGING;
	expected |= STATUS_CODE_OVERUNDERFLOW;

	zassert_equal(EC_SUCCESS, battery_status(&status), NULL);
	zassert_equal(expected, status, "%d != %d", expected, status);
}

/** Custom battery function which always fail */
int fail_func(struct i2c_emul *emul, uint8_t *buf, int *len, int cmd,
	      void *data)
{
	return -EINVAL;
}

/** Test wait for stable function */
static void test_battery_wait_for_stable(void)
{
	struct i2c_emul *emul;

	emul = sbat_emul_get_ptr(BATTERY_ORD);

	/* Should fail when read function always fail */
	sbat_emul_set_custom_read_func(emul, fail_func, NULL);
	zassert_equal(EC_ERROR_NOT_POWERED, battery_wait_for_stable(), NULL);

	/* Should be ok with default handler */
	sbat_emul_set_custom_read_func(emul, NULL, NULL);
	zassert_equal(EC_SUCCESS, battery_wait_for_stable(), NULL);
}

/** Test manufacture date */
static void test_battery_manufacture_date(void)
{
	struct sbat_emul_bat_data *bat;
	struct i2c_emul *emul;
	int day, month, year;
	int exp_month = 5;
	int exp_year = 2018;
	int exp_day = 19;
	uint16_t date;

	emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(emul);

	date = sbat_emul_date_to_word(exp_day, exp_month, exp_year);
	bat->mf_date = date;

	zassert_equal(EC_SUCCESS, battery_manufacture_date(&year, &month, &day),
		      NULL);
	zassert_equal(exp_day, day, "%d != %d", exp_day, day);
	zassert_equal(exp_month, month, "%d != %d", exp_month, month);
	zassert_equal(exp_year, year, "%d != %d", exp_year, year);
}

/** Test time at rate */
static void test_battery_time_at_rate(void)
{
	struct sbat_emul_bat_data *bat;
	struct i2c_emul *emul;
	int expect_time;
	int minutes;
	int rate;

	emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(emul);

	/* 3000mAh at rate 300mA will be discharged in 10h */
	bat->cap = 3000;
	rate = -300;
	expect_time = 600;

	zassert_equal(EC_SUCCESS, battery_time_at_rate(rate, &minutes), NULL);
	zassert_equal(expect_time, minutes, "%d != %d", expect_time, minutes);

	/* 1000mAh at rate 1000mA will be charged in 1h */
	bat->cap = bat->full_cap - 1000;
	rate = 1000;
	/* battery_time_at_rate report time to full as negative number */
	expect_time = -60;

	zassert_equal(EC_SUCCESS, battery_time_at_rate(rate, &minutes), NULL);
	zassert_equal(expect_time, minutes, "%d != %d", expect_time, minutes);
}

/** Parameter for fail_cmd_func */
struct fail_cmd_data {
	int cmd;
};

/** Custom battery function which fail on specific command */
int fail_cmd_func(struct i2c_emul *emul, uint8_t *buf, int *len, int cmd,
		  void *data)
{
	struct fail_cmd_data *p = data;

	if (p->cmd == cmd)
		return -EINVAL;

	/* Use default handler */
	return 1;
}

/** Test battery get params */
static void test_battery_get_params(void)
{
	struct sbat_emul_bat_data *bat;
	struct fail_cmd_data func_data;
	struct batt_params batt;
	struct i2c_emul *emul;
	int flags;

	emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(emul);

	/* Battery wants to charge */
	bat->desired_charg_cur = 1000;
	bat->desired_charg_volt = 5000;

	/* Use function which allows to fail for specific command */
	sbat_emul_set_custom_read_func(emul, fail_cmd_func, &func_data);

	/* Fail temperature read */
	func_data.cmd = SB_TEMPERATURE;
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_TEMPERATURE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail state of charge read; want charge cannot be set */
	func_data.cmd = SB_RELATIVE_STATE_OF_CHARGE;
	flags = BATT_FLAG_RESPONSIVE | BATT_FLAG_BAD_STATE_OF_CHARGE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail voltage read */
	func_data.cmd = SB_VOLTAGE;
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_VOLTAGE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail current read */
	func_data.cmd = SB_CURRENT;
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_CURRENT;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail average current read */
	func_data.cmd = SB_AVERAGE_CURRENT;
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_AVERAGE_CURRENT;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail charging voltage read; want charge cannot be set */
	func_data.cmd = SB_CHARGING_VOLTAGE;
	flags = BATT_FLAG_RESPONSIVE | BATT_FLAG_BAD_DESIRED_VOLTAGE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail charging voltage read; want charge cannot be set */
	func_data.cmd = SB_CHARGING_CURRENT;
	flags = BATT_FLAG_RESPONSIVE | BATT_FLAG_BAD_DESIRED_CURRENT;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail remaining capacity read */
	func_data.cmd = SB_REMAINING_CAPACITY;
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_REMAINING_CAPACITY;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail full capacity read */
	func_data.cmd = SB_FULL_CHARGE_CAPACITY;
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_FULL_CAPACITY;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail status read */
	func_data.cmd = SB_BATTERY_STATUS;
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_STATUS;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail all */
	sbat_emul_set_custom_read_func(emul, fail_func, NULL);
	flags = BATT_FLAG_BAD_ANY;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Use default handler, everything should be ok */
	sbat_emul_set_custom_read_func(emul, NULL, NULL);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);
}

void test_suite_smart_battery(void)
{
	ztest_test_suite(smart_battery,
			 ztest_user_unit_test(test_battery_getters),
			 ztest_user_unit_test(test_battery_status),
			 ztest_user_unit_test(test_battery_wait_for_stable),
			 ztest_user_unit_test(test_battery_manufacture_date),
			 ztest_user_unit_test(test_battery_time_at_rate),
			 ztest_user_unit_test(test_battery_get_params));
	ztest_run_test_suite(smart_battery);
}

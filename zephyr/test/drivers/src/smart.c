/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>
#include <shell/shell.h>
#include <shell/shell_uart.h>

#include "common.h"
#include "i2c.h"
#include "emul/emul_common_i2c.h"
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

/** Test wait for stable function */
static void test_battery_wait_for_stable(void)
{
	struct i2c_emul *emul;

	emul = sbat_emul_get_ptr(BATTERY_ORD);

	/* Should fail when read function always fail */
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_FAIL_ALL_REG);
	zassert_equal(EC_ERROR_NOT_POWERED, battery_wait_for_stable(), NULL);

	/* Should be ok with default handler */
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);
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

/** Test battery get params */
static void test_battery_get_params(void)
{
	struct sbat_emul_bat_data *bat;
	struct batt_params batt;
	struct i2c_emul *emul;
	int flags;

	emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(emul);

	/* Battery wants to charge */
	bat->desired_charg_cur = 1000;
	bat->desired_charg_volt = 5000;

	/* Fail temperature read */
	i2c_common_emul_set_read_fail_reg(emul, SB_TEMPERATURE);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_TEMPERATURE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail state of charge read; want charge cannot be set */
	i2c_common_emul_set_read_fail_reg(emul, SB_RELATIVE_STATE_OF_CHARGE);
	flags = BATT_FLAG_RESPONSIVE | BATT_FLAG_BAD_STATE_OF_CHARGE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail voltage read */
	i2c_common_emul_set_read_fail_reg(emul, SB_VOLTAGE);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_VOLTAGE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail current read */
	i2c_common_emul_set_read_fail_reg(emul, SB_CURRENT);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_CURRENT;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail average current read */
	i2c_common_emul_set_read_fail_reg(emul, SB_AVERAGE_CURRENT);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_AVERAGE_CURRENT;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail charging voltage read; want charge cannot be set */
	i2c_common_emul_set_read_fail_reg(emul, SB_CHARGING_VOLTAGE);
	flags = BATT_FLAG_RESPONSIVE | BATT_FLAG_BAD_DESIRED_VOLTAGE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail charging voltage read; want charge cannot be set */
	i2c_common_emul_set_read_fail_reg(emul, SB_CHARGING_CURRENT);
	flags = BATT_FLAG_RESPONSIVE | BATT_FLAG_BAD_DESIRED_CURRENT;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail remaining capacity read */
	i2c_common_emul_set_read_fail_reg(emul, SB_REMAINING_CAPACITY);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_REMAINING_CAPACITY;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail full capacity read */
	i2c_common_emul_set_read_fail_reg(emul, SB_FULL_CHARGE_CAPACITY);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_FULL_CAPACITY;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail status read */
	i2c_common_emul_set_read_fail_reg(emul, SB_BATTERY_STATUS);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_STATUS;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail all */
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_FAIL_ALL_REG);
	flags = BATT_FLAG_BAD_ANY;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Use default handler, everything should be ok */
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);
}

struct mfgacc_data {
	int reg;
	uint8_t *buf;
	int len;
};

static int mfgacc_read_func(struct i2c_emul *emul, int reg, uint8_t *val,
			    int bytes, void *data)
{
	struct mfgacc_data *conf = data;

	if (bytes == 0 && conf->reg == reg) {
		sbat_emul_set_response(emul, reg, conf->buf, conf->len, false);
	}

	return 1;
}

/** Test battery manufacturer access */
static void test_battery_mfacc(void)
{
	struct sbat_emul_bat_data *bat;
	struct mfgacc_data mfacc_conf;
	struct i2c_emul *emul;
	uint8_t recv_buf[10];
	uint8_t mf_data[10];
	uint16_t cmd;
	int len;

	emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(emul);

	/* Select arbitrary command number for the test */
	cmd = 0x1234;

	/* Test fail on to short receive buffer */
	len = 2;
	zassert_equal(EC_ERROR_INVAL,
		      sb_read_mfgacc(cmd, SB_ALT_MANUFACTURER_ACCESS, recv_buf,
				     len), NULL);

	/* Set correct length for rest of the test */
	len = 10;

	/* Test fail on writing SB_MANUFACTURER_ACCESS register */
	i2c_common_emul_set_write_fail_reg(emul, SB_MANUFACTURER_ACCESS);
	zassert_equal(EC_ERROR_INVAL,
		      sb_read_mfgacc(cmd, SB_ALT_MANUFACTURER_ACCESS, recv_buf,
				     len), NULL);
	i2c_common_emul_set_write_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on reading manufacturer data (custom handler is not set) */
	zassert_equal(EC_ERROR_INVAL,
		      sb_read_mfgacc(cmd, SB_ALT_MANUFACTURER_ACCESS, recv_buf,
				     len), NULL);

	/* Set arbitrary manufacturer data */
	for (int i = 1; i < len; i++) {
		mf_data[i] = i;
	}
	/* Set first byte of message as length */
	mf_data[0] = len;

	/* Setup custom handler */
	mfacc_conf.reg = SB_ALT_MANUFACTURER_ACCESS;
	mfacc_conf.len = len;
	mfacc_conf.buf = mf_data;
	i2c_common_emul_set_read_func(emul, mfgacc_read_func, &mfacc_conf);

	/* Test error when mf_data doesn't start with command */
	zassert_equal(EC_ERROR_UNKNOWN,
		      sb_read_mfgacc(cmd, SB_ALT_MANUFACTURER_ACCESS, recv_buf,
				     len), NULL);

	/* Set beginning of the manufacturer data */
	mf_data[1] = cmd & 0xff;
	mf_data[2] = (cmd >> 8) & 0xff;

	/* Test successful manufacturer data read */
	zassert_equal(EC_SUCCESS,
		      sb_read_mfgacc(cmd, SB_ALT_MANUFACTURER_ACCESS, recv_buf,
				     len), NULL);
	/* Compare received data ignoring length byte */
	zassert_mem_equal(mf_data + 1, recv_buf, len - 1, NULL);

	/* Disable custom read function */
	i2c_common_emul_set_read_func(emul, NULL, NULL);
}

/** Test battery fake charge level set and read */
static void test_battery_fake_charge(void)
{
	struct sbat_emul_bat_data *bat;
	struct batt_params batt;
	struct i2c_emul *emul;
	int remaining_cap;
	int fake_charge;
	int charge;
	int flags;

	emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(emul);

	/* Success on command with no argument */
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(shell_backend_uart_get_ptr(),
					"battfake"), NULL);

	/* Fail on command with argument which is not a number */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(shell_backend_uart_get_ptr(),
					"battfake test"), NULL);

	/* Fail on command with charge level above 100% */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(shell_backend_uart_get_ptr(),
					"battfake 123"), NULL);

	/* Fail on command with charge level below 0% */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(shell_backend_uart_get_ptr(),
					"battfake -23"), NULL);

	/* Set fake charge level */
	fake_charge = 65;
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(shell_backend_uart_get_ptr(),
					"battfake 65"), NULL);

	/* Test that fake charge level is applied */
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);
	zassert_equal(fake_charge, batt.state_of_charge, "%d%% != %d%%",
		      fake_charge, batt.state_of_charge);
	remaining_cap = bat->full_cap * fake_charge / 100;
	zassert_equal(remaining_cap, batt.remaining_capacity, "%d != %d",
		      remaining_cap, batt.remaining_capacity);

	/* Test fake remaining capacity when full capacity is not available */
	i2c_common_emul_set_read_fail_reg(emul, SB_FULL_CHARGE_CAPACITY);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_FULL_CAPACITY;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);
	zassert_equal(fake_charge, batt.state_of_charge, "%d%% != %d%%",
		      fake_charge, batt.state_of_charge);
	remaining_cap = bat->design_cap * fake_charge / 100;
	zassert_equal(remaining_cap, batt.remaining_capacity, "%d != %d",
		      remaining_cap, batt.remaining_capacity);
	i2c_common_emul_set_read_fail_reg(emul, I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Disable fake charge level */
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(shell_backend_uart_get_ptr(),
					"battfake -1"), NULL);

	/* Test that fake charge level is not applied */
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);
	charge = 100 * bat->cap / bat->full_cap;
	zassert_equal(charge, batt.state_of_charge, "%d%% != %d%%",
		      charge, batt.state_of_charge);
	zassert_equal(bat->cap, batt.remaining_capacity, "%d != %d",
		      bat->cap, batt.remaining_capacity);
}

/** Test battery fake temperature set and read */
static void test_battery_fake_temperature(void)
{
	struct sbat_emul_bat_data *bat;
	struct batt_params batt;
	struct i2c_emul *emul;
	int fake_temp;
	int flags;

	emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(emul);

	/* Success on command with no argument */
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(shell_backend_uart_get_ptr(),
					"batttempfake"), NULL);

	/* Fail on command with argument which is not a number */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(shell_backend_uart_get_ptr(),
					"batttempfake test"), NULL);

	/* Fail on command with too high temperature (above 500.0 K) */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(shell_backend_uart_get_ptr(),
					"batttempfake 5001"), NULL);

	/* Fail on command with too low temperature (below 0 K) */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(shell_backend_uart_get_ptr(),
					"batttempfake -23"), NULL);

	/* Set fake temperature */
	fake_temp = 2840;
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(shell_backend_uart_get_ptr(),
					"batttempfake 2840"), NULL);

	/* Test that fake temperature is applied */
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);
	zassert_equal(fake_temp, batt.temperature, "%d != %d",
		      fake_temp, batt.temperature);

	/* Disable fake temperature */
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(shell_backend_uart_get_ptr(),
					"batttempfake -1"), NULL);

	/* Test that fake temperature is not applied */
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);
	zassert_equal(bat->temp, batt.temperature, "%d != %d",
		      bat->temp, batt.temperature);
}

void test_suite_smart_battery(void)
{
	ztest_test_suite(smart_battery,
			 ztest_user_unit_test(test_battery_getters),
			 ztest_user_unit_test(test_battery_status),
			 ztest_user_unit_test(test_battery_wait_for_stable),
			 ztest_user_unit_test(test_battery_manufacture_date),
			 ztest_user_unit_test(test_battery_time_at_rate),
			 ztest_user_unit_test(test_battery_get_params),
			 ztest_user_unit_test(test_battery_mfacc),
			 ztest_user_unit_test(test_battery_fake_charge),
			 ztest_user_unit_test(test_battery_fake_temperature));
	ztest_run_test_suite(smart_battery);
}

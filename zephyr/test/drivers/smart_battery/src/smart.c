/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "common.h"
#include "console.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_smart_battery.h"
#include "i2c.h"
#include "test/drivers/test_state.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/ztest.h>

#define BATTERY_NODE DT_NODELABEL(battery)

FAKE_VALUE_FUNC(int, battery_is_cut_off);
FAKE_VALUE_FUNC(int, battery_cutoff_in_progress);

/** Test all simple getters */
ZTEST_USER(smart_battery, test_battery_getters)
{
	struct sbat_emul_bat_data *bat;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	char block[32];
	int expected;
	int word;

	bat = sbat_emul_get_bat_data(emul);

	zassert_equal(EC_SUCCESS, battery_get_mode(&word));
	zassert_equal(bat->mode, word, "%d != %d", bat->mode, word);

	expected = 100 * bat->cap / bat->design_cap;
	zassert_equal(EC_SUCCESS, battery_state_of_charge_abs(&word));
	zassert_equal(expected, word, "%d != %d", expected, word);

	zassert_equal(EC_SUCCESS, battery_cycle_count(&word));
	zassert_equal(bat->cycle_count, word, "%d != %d", bat->cycle_count,
		      word);
	zassert_equal(EC_SUCCESS, battery_design_voltage(&word));
	zassert_equal(bat->design_mv, word, "%d != %d", bat->design_mv, word);
	zassert_equal(EC_SUCCESS, battery_serial_number(&word));
	zassert_equal(bat->sn, word, "%d != %d", bat->sn, word);
	zassert_equal(EC_SUCCESS, get_battery_manufacturer_name(block, 32),
		      NULL);
	zassert_mem_equal(block, bat->mf_name, bat->mf_name_len, "%s != %s",
			  block, bat->mf_name);
	zassert_equal(EC_SUCCESS, battery_device_name(block, 32));
	zassert_mem_equal(block, bat->dev_name, bat->dev_name_len, "%s != %s",
			  block, bat->dev_name);
	zassert_equal(EC_SUCCESS, battery_device_chemistry(block, 32));
	zassert_mem_equal(block, bat->dev_chem, bat->dev_chem_len, "%s != %s",
			  block, bat->dev_chem);
	word = battery_get_avg_current();
	zassert_equal(bat->avg_cur, word, "%d != %d", bat->avg_cur, word);
	word = battery_get_avg_voltage();
	zassert_equal(bat->volt, word, "%d != %d", bat->volt, word);

	bat->avg_cur = 200;
	expected = (bat->full_cap - bat->cap) * 60 / bat->avg_cur;
	zassert_equal(EC_SUCCESS, battery_time_to_full(&word));
	zassert_equal(expected, word, "%d != %d", expected, word);

	bat->cur = -200;
	expected = bat->cap * 60 / (-bat->cur);
	zassert_equal(EC_SUCCESS, battery_run_time_to_empty(&word));
	zassert_equal(expected, word, "%d != %d", expected, word);

	bat->avg_cur = -200;
	expected = bat->cap * 60 / (-bat->avg_cur);
	zassert_equal(EC_SUCCESS, battery_time_to_empty(&word));
	zassert_equal(expected, word, "%d != %d", expected, word);
}

/** Test getting capacity. These functions should force mAh mode */
ZTEST_USER(smart_battery, test_battery_get_capacity)
{
	struct sbat_emul_bat_data *bat;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	struct i2c_common_emul_data *common_data =
		emul_smart_battery_get_i2c_common_data(emul);
	int word;

	bat = sbat_emul_get_bat_data(emul);

	/* Test fail when checking battery mode */
	i2c_common_emul_set_read_fail_reg(common_data, SB_BATTERY_MODE);
	zassert_equal(EC_ERROR_INVAL, battery_remaining_capacity(&word));
	zassert_equal(EC_ERROR_INVAL, battery_full_charge_capacity(&word),
		      NULL);
	zassert_equal(EC_ERROR_INVAL, battery_design_capacity(&word));
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test getting remaining capacity and if mAh mode is forced */
	bat->mode |= MODE_CAPACITY;
	zassert_equal(EC_SUCCESS, battery_remaining_capacity(&word));
	zassert_equal(bat->cap, word, "%d != %d", bat->cap, word);
	zassert_false(bat->mode & MODE_CAPACITY, "mAh mode not forced");

	/* Test getting full charge capacity and if mAh mode is forced */
	bat->mode |= MODE_CAPACITY;
	zassert_equal(EC_SUCCESS, battery_full_charge_capacity(&word));
	zassert_equal(bat->full_cap, word, "%d != %d", bat->full_cap, word);
	zassert_false(bat->mode & MODE_CAPACITY, "mAh mode not forced");

	/* Test getting design capacity and if mAh mode is forced */
	bat->mode |= MODE_CAPACITY;
	zassert_equal(EC_SUCCESS, battery_design_capacity(&word));
	zassert_equal(bat->design_cap, word, "%d != %d", bat->design_cap, word);
	zassert_false(bat->mode & MODE_CAPACITY, "mAh mode not forced");
}

/** Test battery status */
ZTEST_USER(smart_battery, test_battery_status)
{
	struct sbat_emul_bat_data *bat;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	int expected;
	int status;

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

	zassert_equal(EC_SUCCESS, battery_status(&status));
	zassert_equal(expected, status, "%d != %d", expected, status);
}

/** Test wait for stable function */
ZTEST_USER(smart_battery, test_battery_wait_for_stable)
{
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	struct i2c_common_emul_data *common_data =
		emul_smart_battery_get_i2c_common_data(emul);

	/* Should fail when read function always fail */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_FAIL_ALL_REG);
	zassert_equal(EC_ERROR_NOT_POWERED, battery_wait_for_stable());

	/* Should be ok with default handler */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	zassert_equal(EC_SUCCESS, battery_wait_for_stable());
}

/** Test manufacture date */
ZTEST_USER(smart_battery, test_battery_manufacture_date)
{
	struct sbat_emul_bat_data *bat;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	int day, month, year;
	int exp_month = 5;
	int exp_year = 2018;
	int exp_day = 19;
	uint16_t date;

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
ZTEST_USER(smart_battery, test_battery_time_at_rate)
{
	struct sbat_emul_bat_data *bat;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	struct i2c_common_emul_data *common_data =
		emul_smart_battery_get_i2c_common_data(emul);
	int expect_time;
	int minutes;
	int rate;

	bat = sbat_emul_get_bat_data(emul);

	/* Test fail on rate 0 */
	rate = 0;
	zassert_equal(EC_ERROR_INVAL, battery_time_at_rate(rate, &minutes),
		      NULL);

	/* 10mAh at rate 6000mA will be discharged in 6s */
	bat->cap = 10;
	rate = -6000;

	/* Test fail on writing at rate register */
	i2c_common_emul_set_write_fail_reg(common_data, SB_AT_RATE);
	zassert_equal(EC_ERROR_INVAL, battery_time_at_rate(rate, &minutes),
		      NULL);
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on reading at rate ok register */
	i2c_common_emul_set_read_fail_reg(common_data, SB_AT_RATE_OK);
	zassert_equal(EC_ERROR_INVAL, battery_time_at_rate(rate, &minutes),
		      NULL);
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/*
	 * Expected discharging rate is less then 10s,
	 * so AtRateOk() register should return 0
	 */
	zassert_equal(EC_ERROR_TIMEOUT, battery_time_at_rate(rate, &minutes),
		      NULL);

	/* 3000mAh at rate 300mA will be discharged in 10h */
	bat->cap = 3000;
	rate = -300;
	expect_time = 600;

	zassert_equal(EC_SUCCESS, battery_time_at_rate(rate, &minutes));
	zassert_equal(expect_time, minutes, "%d != %d", expect_time, minutes);

	/* 1000mAh at rate 1000mA will be charged in 1h */
	bat->cap = bat->full_cap - 1000;
	rate = 1000;
	/* battery_time_at_rate report time to full as negative number */
	expect_time = -60;

	zassert_equal(EC_SUCCESS, battery_time_at_rate(rate, &minutes));
	zassert_equal(expect_time, minutes, "%d != %d", expect_time, minutes);
}

/** Test battery get params */
ZTEST_USER(smart_battery, test_battery_get_params)
{
	struct sbat_emul_bat_data *bat;
	struct batt_params batt;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	struct i2c_common_emul_data *common_data =
		emul_smart_battery_get_i2c_common_data(emul);
	int flags;

	bat = sbat_emul_get_bat_data(emul);

	/* Fail temperature read */
	i2c_common_emul_set_read_fail_reg(common_data, SB_TEMPERATURE);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_TEMPERATURE;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail state of charge read; want charge cannot be set */
	i2c_common_emul_set_read_fail_reg(common_data,
					  SB_RELATIVE_STATE_OF_CHARGE);
	flags = BATT_FLAG_RESPONSIVE | BATT_FLAG_BAD_STATE_OF_CHARGE;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail voltage read */
	i2c_common_emul_set_read_fail_reg(common_data, SB_VOLTAGE);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_VOLTAGE;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail current read */
	i2c_common_emul_set_read_fail_reg(common_data, SB_CURRENT);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_CURRENT;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail average current read */
	i2c_common_emul_set_read_fail_reg(common_data, SB_AVERAGE_CURRENT);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_AVERAGE_CURRENT;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail charging voltage read; want charge cannot be set */
	i2c_common_emul_set_read_fail_reg(common_data, SB_CHARGING_VOLTAGE);
	flags = BATT_FLAG_RESPONSIVE | BATT_FLAG_BAD_DESIRED_VOLTAGE;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail charging voltage read; want charge cannot be set */
	i2c_common_emul_set_read_fail_reg(common_data, SB_CHARGING_CURRENT);
	flags = BATT_FLAG_RESPONSIVE | BATT_FLAG_BAD_DESIRED_CURRENT;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail remaining capacity read */
	i2c_common_emul_set_read_fail_reg(common_data, SB_REMAINING_CAPACITY);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_REMAINING_CAPACITY;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail full capacity read */
	i2c_common_emul_set_read_fail_reg(common_data, SB_FULL_CHARGE_CAPACITY);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_FULL_CAPACITY;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail status read */
	i2c_common_emul_set_read_fail_reg(common_data, SB_BATTERY_STATUS);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_STATUS;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Fail all */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_FAIL_ALL_REG);
	flags = BATT_FLAG_BAD_ANY;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);

	/* Use default handler, everything should be ok */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);
}

struct mfgacc_data {
	int reg;
	uint8_t *buf;
	int len;
};

static int mfgacc_read_func(const struct emul *emul, int reg, uint8_t *val,
			    int bytes, void *data)
{
	struct mfgacc_data *conf = data;

	if (bytes == 0 && conf->reg == reg) {
		sbat_emul_set_response(emul, reg, conf->buf, conf->len, false);
	}

	return 1;
}

/** Test battery manufacturer access */
ZTEST_USER(smart_battery, test_battery_mfacc)
{
	struct sbat_emul_bat_data *bat;
	struct mfgacc_data mfacc_conf;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	struct i2c_common_emul_data *common_data =
		emul_smart_battery_get_i2c_common_data(emul);
	uint8_t recv_buf[10];
	uint8_t mf_data[10];
	uint16_t cmd;
	int len;

	bat = sbat_emul_get_bat_data(emul);

	/* Select arbitrary command number for the test */
	cmd = 0x1234;

	/* Test fail on to short receive buffer */
	len = 2;
	zassert_equal(EC_ERROR_INVAL,
		      sb_read_mfgacc(cmd, SB_ALT_MANUFACTURER_ACCESS, recv_buf,
				     len),
		      NULL);

	/* Set correct length for rest of the test */
	len = 10;

	/* Test fail on writing SB_MANUFACTURER_ACCESS register */
	i2c_common_emul_set_write_fail_reg(common_data, SB_MANUFACTURER_ACCESS);
	zassert_equal(EC_ERROR_INVAL,
		      sb_read_mfgacc(cmd, SB_ALT_MANUFACTURER_ACCESS, recv_buf,
				     len),
		      NULL);
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on reading manufacturer data (custom handler is not set) */
	zassert_equal(EC_ERROR_INVAL,
		      sb_read_mfgacc(cmd, SB_ALT_MANUFACTURER_ACCESS, recv_buf,
				     len),
		      NULL);

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
	i2c_common_emul_set_read_func(common_data, mfgacc_read_func,
				      &mfacc_conf);

	/* Test error when mf_data doesn't start with command */
	zassert_equal(EC_ERROR_UNKNOWN,
		      sb_read_mfgacc(cmd, SB_ALT_MANUFACTURER_ACCESS, recv_buf,
				     len),
		      NULL);

	/* Set beginning of the manufacturer data */
	mf_data[1] = cmd & 0xff;
	mf_data[2] = (cmd >> 8) & 0xff;

	/* Test successful manufacturer data read */
	zassert_equal(EC_SUCCESS,
		      sb_read_mfgacc(cmd, SB_ALT_MANUFACTURER_ACCESS, recv_buf,
				     len),
		      NULL);
	/* Compare received data ignoring length byte */
	zassert_mem_equal(mf_data + 1, recv_buf, len - 1, NULL);

	/* Disable custom read function */
	i2c_common_emul_set_read_func(common_data, NULL, NULL);
}

/** Test battery fake charge level set and read */
ZTEST_USER(smart_battery, test_battery_fake_charge)
{
	struct sbat_emul_bat_data *bat;
	struct batt_params batt;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	struct i2c_common_emul_data *common_data =
		emul_smart_battery_get_i2c_common_data(emul);
	int remaining_cap;
	int fake_charge;
	int charge;
	int flags;

	bat = sbat_emul_get_bat_data(emul);

	/* Success on command with no argument */
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "battfake"),
		      NULL);

	/* Fail on command with argument which is not a number */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "battfake test"), NULL);

	/* Fail on command with charge level above 100% */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "battfake 123"), NULL);

	/* Fail on command with charge level below 0% */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "battfake -23"), NULL);

	/* Set fake charge level */
	fake_charge = 65;
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(get_ec_shell(), "battfake 65"), NULL);

	/* Test that fake charge level is applied */
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);
	zassert_equal(fake_charge, batt.state_of_charge, "%d%% != %d%%",
		      fake_charge, batt.state_of_charge);
	remaining_cap = bat->full_cap * fake_charge / 100;
	zassert_equal(remaining_cap, batt.remaining_capacity, "%d != %d",
		      remaining_cap, batt.remaining_capacity);

	/* Test fake remaining capacity when full capacity is not available */
	i2c_common_emul_set_read_fail_reg(common_data, SB_FULL_CHARGE_CAPACITY);
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE |
		BATT_FLAG_BAD_FULL_CAPACITY;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);
	zassert_equal(fake_charge, batt.state_of_charge, "%d%% != %d%%",
		      fake_charge, batt.state_of_charge);
	remaining_cap = bat->design_cap * fake_charge / 100;
	zassert_equal(remaining_cap, batt.remaining_capacity, "%d != %d",
		      remaining_cap, batt.remaining_capacity);
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Disable fake charge level */
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(get_ec_shell(), "battfake -1"), NULL);

	/* Test that fake charge level is not applied */
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);
	charge = 100 * bat->cap / bat->full_cap;
	zassert_equal(charge, batt.state_of_charge, "%d%% != %d%%", charge,
		      batt.state_of_charge);
	zassert_equal(bat->cap, batt.remaining_capacity, "%d != %d", bat->cap,
		      batt.remaining_capacity);
}

/** Test battery fake temperature set and read */
ZTEST_USER(smart_battery, test_battery_fake_temperature)
{
	struct sbat_emul_bat_data *bat;
	struct batt_params batt;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	int fake_temp;
	int flags;

	bat = sbat_emul_get_bat_data(emul);

	/* Success on command with no argument */
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(get_ec_shell(), "batttempfake"), NULL);

	/* Fail on command with argument which is not a number */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "batttempfake test"),
		      NULL);

	/* Fail on command with too high temperature (above 500.0 K) */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "batttempfake 5001"),
		      NULL);

	/* Fail on command with too low temperature (below 0 K) */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "batttempfake -23"),
		      NULL);

	/* Set fake temperature */
	fake_temp = 2840;
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(get_ec_shell(), "batttempfake 2840"),
		      NULL);

	/* Test that fake temperature is applied */
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);
	zassert_equal(fake_temp, batt.temperature, "%d != %d", fake_temp,
		      batt.temperature);

	/* Disable fake temperature */
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(get_ec_shell(), "batttempfake -1"),
		      NULL);

	/* Test that fake temperature is not applied */
	flags = BATT_FLAG_WANT_CHARGE | BATT_FLAG_RESPONSIVE;
	batt.flags = 0;
	battery_get_params(&batt);
	zassert_equal(flags, batt.flags, "0x%x != 0x%x", flags, batt.flags);
	zassert_equal(bat->temp, batt.temperature, "%d != %d", bat->temp,
		      batt.temperature);
}

/* Test that accesses to battery properties are prevented during cutoff. */
ZTEST_USER(smart_battery, test_battery_access_cutoff)
{
	struct batt_params params = {};
	char str[64];

	/*
	 * Accesses are blocked because
	 * they might wake the battery up from cutoff.
	 */
	battery_is_cut_off_fake.return_val = 1;
	battery_get_params(&params);
	zassert_equal(params.flags, BATT_FLAG_BAD_ANY, "actual flags were %#x",
		      params.flags);
	zassert_equal(get_battery_manufacturer_name(str, sizeof(str)),
		      EC_ERROR_ACCESS_DENIED);
	zassert_equal(sb_read_sized_block(0, NULL, 0), EC_ERROR_ACCESS_DENIED);
	/*
	 * Writes are blocked after cutoff but are allowed while in progress
	 * because we need to write to the battery to complete cutoff.
	 */
	zassert_equal(sb_write(0, 0), EC_ERROR_ACCESS_DENIED);
	zassert_equal(sb_write_block(0, NULL, 0), EC_ERROR_ACCESS_DENIED);

	/* Same behavior if cutoff is in progress. */
	RESET_FAKE(battery_is_cut_off);
	battery_cutoff_in_progress_fake.return_val = 1;
	battery_get_params(&params);
	zassert_equal(params.flags, BATT_FLAG_BAD_ANY, "actual flags were %#x",
		      params.flags);
	zassert_equal(get_battery_manufacturer_name(str, sizeof(str)),
		      EC_ERROR_ACCESS_DENIED);
	zassert_equal(sb_read_sized_block(0, str, 1), EC_ERROR_ACCESS_DENIED);
}

static void reset_battfake(void *data)
{
	RESET_FAKE(battery_is_cut_off);
	RESET_FAKE(battery_cutoff_in_progress);
	shell_execute_cmd(get_ec_shell(), "battfake -1");
}

ZTEST_SUITE(smart_battery, drivers_predicate_post_main, NULL, NULL,
	    reset_battfake, NULL);

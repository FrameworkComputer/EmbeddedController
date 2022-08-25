/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/zephyr.h>
#include <zephyr/ztest.h>

#include "battery.h"
#include "battery_smart.h"
#include "ec_commands.h"
#include "emul/emul_smart_battery.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

/* The param buffer has at most 2 msg's (write + read) and 1 byte write len. */
static uint8_t param_buf[sizeof(struct ec_params_i2c_passthru) +
			 sizeof(struct ec_params_i2c_passthru_msg) * 2 + 1];

/* The response buffer has at most 32 bytes returned result. */
static uint8_t response_buf[sizeof(struct ec_response_i2c_passthru) + 32];

static void i2c_passthru_xfer(uint8_t port, uint8_t addr, uint8_t *write_buf,
			      int write_len, uint8_t **read_buf, int read_len)
{
	struct ec_params_i2c_passthru *params =
		(struct ec_params_i2c_passthru *)&param_buf;
	struct ec_response_i2c_passthru *response =
		(struct ec_response_i2c_passthru *)&response_buf;
	struct ec_params_i2c_passthru_msg *msg = params->msg;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_I2C_PASSTHRU, 0);
	uint8_t *pdata;
	int size;

	params->port = port;
	params->num_msgs = (read_len != 0) + (write_len != 0);

	size = sizeof(*params) + params->num_msgs * sizeof(*msg);
	pdata = (uint8_t *)params + size;

	if (write_len) {
		msg->addr_flags = addr;
		msg->len = write_len;
		memcpy(pdata, write_buf, write_len);
		msg++;
	}

	if (read_len) {
		msg->addr_flags = addr | EC_I2C_FLAG_READ;
		msg->len = read_len;
	}

	args.params = params;
	args.params_size = size + write_len;
	args.response = response;
	args.response_max = sizeof(*response) + read_len;

	/* Execute the I2C passthru host command */
	zassert_ok(host_command_process(&args), NULL);
	zassert_ok(args.result, NULL);
	zassert_ok(response->i2c_status, NULL);
	zassert_equal(args.response_size, sizeof(*response) + read_len, NULL);

	/* Return the data portion */
	if (read_len)
		*read_buf = response->data;
}

static inline void virtual_battery_xfer(uint8_t *write_buf, int write_len,
					uint8_t **read_buf, int read_len)
{
	i2c_passthru_xfer(I2C_PORT_VIRTUAL_BATTERY, VIRTUAL_BATTERY_ADDR_FLAGS,
			  write_buf, write_len, read_buf, read_len);
}

static uint16_t virtual_battery_read16(uint8_t command)
{
	uint8_t write_buf[1] = { command };
	uint8_t *read_buf;

	virtual_battery_xfer(write_buf, 1, &read_buf, 2);

	/* Little endian */
	return ((int)read_buf[1] << 8) | read_buf[0];
}

static void virtual_battery_write16(uint8_t command, uint16_t data)
{
	uint8_t write_buf[3] = { command };

	*((uint16_t *)&write_buf[1]) = data;

	virtual_battery_xfer(write_buf, 3, NULL, 0);
}

static int virtual_battery_read_str(uint8_t command, char **read_buf,
				    int read_len)
{
	uint8_t write_buf[1] = { command };
	int len;

	virtual_battery_xfer(write_buf, 1, (uint8_t **)read_buf, read_len);

	/* Battery v2 embeds the strlen in the first byte so shift 1 byte. */
	len = **read_buf;
	(*read_buf)++;

	return len;
}

#define BATTERY_NODE DT_NODELABEL(battery)

ZTEST_USER(virtual_battery, test_read_regs)
{
	struct sbat_emul_bat_data *bat;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	int16_t int16;
	uint16_t word;
	int expected;
	char *str;
	int len;

	bat = sbat_emul_get_bat_data(emul);

	/*
	 * Iterate all the registers, which issues the I2C passthru host
	 * command to query the emulated smart battery. Most of the values
	 * are the same as the emulated battery, but with some exceptions.
	 */
	word = virtual_battery_read16(SB_BATTERY_MODE);
	zassert_equal(bat->mode, word, "%d != %d", bat->mode, word);

	word = virtual_battery_read16(SB_SERIAL_NUMBER);
	zassert_equal(bat->sn, word, "%d != %d", bat->sn, word);

	word = virtual_battery_read16(SB_VOLTAGE);
	zassert_equal(bat->volt, word, "%d != %d", bat->volt, word);

	/* The expected value is calculated */
	expected = 100 * bat->cap / bat->full_cap;
	word = virtual_battery_read16(SB_RELATIVE_STATE_OF_CHARGE);

	/* TODO(b/243816417): Flaky virtual battery test for mocked power */
	if (!IS_ENABLED(CONFIG_POWER_SEQUENCE_MOCK)) {
		zassert_equal(expected, word, "%d != %d", expected, word);
	}

	word = virtual_battery_read16(SB_TEMPERATURE);
	zassert_equal(bat->temp, word, "%d != %d", bat->temp, word);

	int16 = virtual_battery_read16(SB_CURRENT);
	zassert_equal(bat->cur, int16, "%d != %d", bat->cur, int16);

	int16 = virtual_battery_read16(SB_AVERAGE_CURRENT);
	zassert_equal(bat->avg_cur, int16, "%d != %d", bat->avg_cur, int16);

	/* The virtual battery modifies the return value to make kernel happy */
	expected = BATTERY_LEVEL_SHUTDOWN;
	word = virtual_battery_read16(SB_MAX_ERROR);
	zassert_equal(expected, word, "%d != %d", expected, word);

	word = virtual_battery_read16(SB_FULL_CHARGE_CAPACITY);
	zassert_equal(bat->full_cap, word, "%d != %d", bat->full_cap, word);

	word = virtual_battery_read16(SB_CYCLE_COUNT);
	zassert_equal(bat->cycle_count, word, "%d != %d", bat->cycle_count,
		      word);

	word = virtual_battery_read16(SB_DESIGN_CAPACITY);
	zassert_equal(bat->design_cap, word, "%d != %d", bat->design_cap, word);

	word = virtual_battery_read16(SB_REMAINING_CAPACITY);
	/* TODO(b/243816417): Flaky virtual battery test for mocked power */
	if (!IS_ENABLED(CONFIG_POWER_SEQUENCE_MOCK)) {
		zassert_equal(bat->cap, word, "%d != %d", bat->cap, word);
	}

	len = virtual_battery_read_str(SB_MANUFACTURER_NAME, &str, 32);
	zassert_equal(bat->mf_name_len, len, "%d != %d", bat->mf_name_len, len);
	zassert_mem_equal(str, bat->mf_name, bat->mf_name_len, "%s != %s", str,
			  bat->mf_name);

	len = virtual_battery_read_str(SB_DEVICE_NAME, &str, 32);
	zassert_equal(bat->dev_name_len, len, "%d != %d", bat->dev_name_len,
		      len);
	zassert_mem_equal(str, bat->dev_name, bat->dev_name_len, "%s != %s",
			  str, bat->dev_name);

	len = virtual_battery_read_str(SB_DEVICE_CHEMISTRY, &str, 32);
	zassert_equal(bat->dev_chem_len, len, "%d != %d", bat->dev_chem_len,
		      len);
	zassert_mem_equal(str, bat->dev_chem, bat->dev_chem_len, "%s != %s",
			  str, bat->dev_chem);

	/* Use the API to query the expected value */
	battery_time_to_full(&expected);
	word = virtual_battery_read16(SB_AVERAGE_TIME_TO_FULL);
	zassert_equal(expected, word, "%d != %d", expected, word);

	battery_time_to_empty(&expected);
	word = virtual_battery_read16(SB_AVERAGE_TIME_TO_EMPTY);
	zassert_equal(expected, word, "%d != %d", expected, word);

	battery_run_time_to_empty(&expected);
	word = virtual_battery_read16(SB_RUN_TIME_TO_EMPTY);
	zassert_equal(expected, word, "%d != %d", expected, word);

	word = virtual_battery_read16(SB_CHARGING_CURRENT);
	zassert_equal(bat->desired_charg_cur, word, "%d != %d",
		      bat->desired_charg_cur, word);

	word = virtual_battery_read16(SB_CHARGING_VOLTAGE);
	zassert_equal(bat->desired_charg_volt, word, "%d != %d",
		      bat->desired_charg_volt, word);

	word = virtual_battery_read16(SB_MANUFACTURE_DATE);
	zassert_equal(bat->mf_date, word, "%d != %d", bat->mf_date, word);

	/* Hard-coded return value: v1.1 without PEC */
	expected = 0x0011;
	word = virtual_battery_read16(SB_SPECIFICATION_INFO);
	zassert_equal(expected, word, "%d != %d", expected, word);

	/*
	 * TODO: Test the following registers:
	 *   SB_BATTERY_STATUS
	 *   SB_DESIGN_VOLTAGE
	 *   SB_MANUFACTURER_DATA
	 *   SB_MANUFACTURE_INFO
	 */
}

ZTEST_USER(virtual_battery, test_write_mfgacc)
{
	struct sbat_emul_bat_data *bat;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	uint16_t cmd = PARAM_OPERATION_STATUS;

	bat = sbat_emul_get_bat_data(emul);

	/* Write the command to the SB_MANUFACTURER_ACCESS and check */
	virtual_battery_write16(SB_MANUFACTURER_ACCESS, cmd);
	zassert_equal(bat->mf_access, cmd, "%d != %d", bat->mf_access, cmd);
}

ZTEST_SUITE(virtual_battery, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

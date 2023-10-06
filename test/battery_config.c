/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test battery info in CBI
 */

#include "battery_fuel_gauge.h"
#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "ec_commands.h"
#include "test_util.h"
#include "util.h"
#include "write_protect.h"

int batt_conf_read(enum cbi_data_tag tag, uint8_t *data, uint8_t size);
int batt_conf_read_ship_mode(struct board_batt_params *info);
int batt_conf_read_sleep_mode(struct board_batt_params *info);
int batt_conf_read_fet_info(struct board_batt_params *info);
int batt_conf_read_fuel_gauge_info(struct board_batt_params *info);
int batt_conf_read_battery_info(struct board_batt_params *info);
void batt_conf_main(void);

const struct board_batt_params board_battery_info[] = {
	[0] = {
		.fuel_gauge = {
			.manuf_name = "AS1GUXd3KB",
			.device_name = "C214-43",
			.ship_mode = {
				.reg_addr = 0x0,
				.reg_data = { 0x10, 0x10 },
			},
			.fet = {
				.reg_addr = 0x00,
				.reg_mask = 0x2000,
				.disconnect_val = 0x2000,
			},
			.flags = FUEL_GAUGE_FLAG_MFGACC,
		},
		.batt_info = {
			.voltage_max = 13200,
			.voltage_normal = 11550,
			.voltage_min = 9000,
			.precharge_current = 256,
			.start_charging_min_c = 0,
			.start_charging_max_c = 45,
			.charging_min_c = 0,
			.discharging_min_c = 0,
			.discharging_max_c = 60,
		},
	},
};

static bool init_battery_type_called;

void init_battery_type(void)
{
	init_battery_type_called = true;
}

static struct board_batt_params conf_in_cbi = {
	.fuel_gauge = {
		.manuf_name = "abc",
		.device_name = "xyz",
		.ship_mode = {
			.reg_addr = 0xaa,
			.reg_data = {
				[0] = 0x89ab,
				[1] = 0xcdef,
			},
		},
	},
	.batt_info = {
		.voltage_max = 8400,
		.voltage_normal = 7400,
		.voltage_min = 6000,
		.precharge_current = 64, /* mA */
		.start_charging_min_c = 0,
		.start_charging_max_c = 50,
		.charging_min_c = 0,
		.charging_max_c = 50,
		.discharging_min_c = -20,
		.discharging_max_c = 60,
	},
};

static void test_setup(void)
{
	/* Make sure that write protect is disabled */
	write_protect_set(0);

	cbi_create();
	cbi_write();

	memset(&default_battery_conf, 0, sizeof(default_battery_conf));
}

static void test_teardown(void)
{
}

DECLARE_EC_TEST(test_batt_conf_read)
{
	struct ship_mode_info *info = &conf_in_cbi.fuel_gauge.ship_mode;
	struct ship_mode_info *dflt =
		&default_battery_conf.fuel_gauge.ship_mode;
	enum cbi_data_tag tag;

	/* Program data in invalid size. */
	tag = CBI_TAG_BATT_SHIP_MODE_REG_ADDR;
	zassert_equal(cbi_set_board_info(tag, &info->reg_addr,
					 sizeof(info->reg_addr) + 1),
		      EC_SUCCESS);

	/* Read */
	zassert_equal(batt_conf_read(tag, &dflt->reg_addr,
				     sizeof(dflt->reg_addr)),
		      EC_ERROR_INVAL);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_read_fuel_gauge_info)
{
	struct fuel_gauge_info *info = &conf_in_cbi.fuel_gauge;
	struct fuel_gauge_info *dflt = &default_battery_conf.fuel_gauge;
	struct fuel_gauge_reg_addr_data reg;
	uint32_t u32;

	/* Read without data in CBI. Test ERROR_UNKNOWN is correctly ignored. */
	zassert_equal(batt_conf_read_fuel_gauge_info(&default_battery_conf),
		      EC_SUCCESS);

	/*
	 * Validate default info remains unchanged.
	 */
	/* struct fuel_gauge_info */
	zassert_is_null(dflt->manuf_name);
	zassert_is_null(dflt->device_name);
	zassert_equal(dflt->flags, 0);
	/* struct fet_info */
	zassert_equal(dflt->fet.reg_addr, 0);
	zassert_equal(dflt->fet.reg_mask, 0);
	/* struct sleep_mode_info */
	zassert_equal(dflt->sleep_mode.reg_addr, 0);
	zassert_equal(dflt->sleep_mode.reg_data, 0);
	/* struct ship_mode_info */
	zassert_equal(dflt->ship_mode.reg_addr, 0);
	zassert_equal(dflt->ship_mode.reg_data[0], 0);
	zassert_equal(dflt->ship_mode.reg_data[1], 0);

	/*
	 * Set data in CBI.
	 */
	/* struct fuel_gauge_info */
	zassert_equal(cbi_set_board_info(CBI_TAG_FUEL_GAUGE_MANUF_NAME,
					 (uint8_t *)info->manuf_name,
					 strlen(info->manuf_name)),
		      EC_SUCCESS);
	zassert_equal(cbi_set_board_info(CBI_TAG_FUEL_GAUGE_DEVICE_NAME,
					 (uint8_t *)info->device_name,
					 strlen(info->device_name)),
		      EC_SUCCESS);
	u32 = FUEL_GAUGE_FLAG_WRITE_BLOCK | FUEL_GAUGE_FLAG_SLEEP_MODE |
	      FUEL_GAUGE_FLAG_MFGACC | FUEL_GAUGE_FLAG_MFGACC_SMB_BLOCK;
	zassert_equal(cbi_set_board_info(CBI_TAG_FUEL_GAUGE_FLAGS,
					 (uint8_t *)&u32, sizeof(u32)),
		      EC_SUCCESS);
	/* struct fet_info */
	zassert_equal(cbi_set_board_info(CBI_TAG_BATT_FET_REG_ADDR,
					 &info->fet.reg_addr,
					 sizeof(info->fet.reg_addr)),
		      EC_SUCCESS);
	zassert_equal(cbi_set_board_info(CBI_TAG_BATT_FET_REG_MASK,
					 (uint8_t *)&info->fet.reg_mask,
					 sizeof(info->fet.reg_mask)),
		      EC_SUCCESS);
	zassert_equal(cbi_set_board_info(CBI_TAG_BATT_FET_DISCONNECT_VAL,
					 (uint8_t *)&info->fet.disconnect_val,
					 sizeof(info->fet.disconnect_val)),
		      EC_SUCCESS);
	zassert_equal(cbi_set_board_info(CBI_TAG_BATT_FET_CFET_MASK,
					 (uint8_t *)&info->fet.cfet_mask,
					 sizeof(info->fet.cfet_mask)),
		      EC_SUCCESS);
	zassert_equal(cbi_set_board_info(CBI_TAG_BATT_FET_CFET_OFF_VAL,
					 (uint8_t *)&info->fet.cfet_off_val,
					 sizeof(info->fet.cfet_off_val)),
		      EC_SUCCESS);
	/* struct sleep_mode_info */
	reg.addr = info->sleep_mode.reg_addr;
	reg.data = info->sleep_mode.reg_data;
	zassert_equal(cbi_set_board_info(CBI_TAG_BATT_SLEEP_MODE,
					 (uint8_t *)&reg, sizeof(reg)),
		      EC_SUCCESS);
	/* struct ship_mode_info */
	zassert_equal(cbi_set_board_info(CBI_TAG_BATT_SHIP_MODE_REG_ADDR,
					 &info->ship_mode.reg_addr,
					 sizeof(info->ship_mode.reg_addr)),
		      EC_SUCCESS);
	zassert_equal(cbi_set_board_info(CBI_TAG_BATT_SHIP_MODE_REG_DATA,
					 (uint8_t *)&info->ship_mode.reg_data,
					 sizeof(info->ship_mode.reg_data)),
		      EC_SUCCESS);

	/*
	 * Read
	 */
	zassert_equal(batt_conf_read_fuel_gauge_info(&default_battery_conf),
		      EC_SUCCESS);

	/*
	 * Validate default info == info in cbi.
	 */
	/* struct fuel_gauge_info */
	zassert_equal(strncmp(dflt->manuf_name, info->manuf_name,
			      strlen(info->manuf_name)),
		      0);
	zassert_equal(strncmp(dflt->device_name, info->device_name,
			      strlen(info->device_name)),
		      0);
	zassert_equal(dflt->flags, FUEL_GAUGE_FLAG_WRITE_BLOCK |
					   FUEL_GAUGE_FLAG_SLEEP_MODE |
					   FUEL_GAUGE_FLAG_MFGACC |
					   FUEL_GAUGE_FLAG_MFGACC_SMB_BLOCK);
	/* struct fet_info */
	zassert_equal(dflt->fet.reg_addr, info->fet.reg_addr);
	zassert_equal(dflt->fet.reg_mask, info->fet.reg_mask);
	/* struct sleep_mode_info */
	zassert_equal(dflt->sleep_mode.reg_addr, info->sleep_mode.reg_addr);
	zassert_equal(dflt->sleep_mode.reg_data, info->sleep_mode.reg_data);
	/* struct ship_mode_info */
	zassert_equal(dflt->ship_mode.reg_addr, info->ship_mode.reg_addr);
	zassert_equal(dflt->ship_mode.reg_data[0], info->ship_mode.reg_data[0]);
	zassert_equal(dflt->ship_mode.reg_data[1], info->ship_mode.reg_data[1]);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_read_battery_info)
{
	struct battery_info *info = &conf_in_cbi.batt_info;
	struct battery_info *dflt = &default_battery_conf.batt_info;
	enum cbi_data_tag tag;
	struct battery_voltage_current mvma;
	struct battery_temperature_range temp;

	/* Read without data in CBI. Test ERROR_UNKNOWN is correctly ignored. */
	zassert_equal(batt_conf_read_battery_info(&default_battery_conf),
		      EC_SUCCESS);

	/* Validate default info remains unchanged. */
	zassert_equal(dflt->voltage_min, 0);
	zassert_equal(dflt->voltage_normal, 0);
	zassert_equal(dflt->voltage_max, 0);
	zassert_equal(dflt->precharge_voltage, 0);
	zassert_equal(dflt->precharge_current, 0);
	zassert_equal(dflt->start_charging_min_c, 0);
	zassert_equal(dflt->start_charging_max_c, 0);
	zassert_equal(dflt->charging_min_c, 0);
	zassert_equal(dflt->charging_max_c, 0);
	zassert_equal(dflt->discharging_min_c, 0);
	zassert_equal(dflt->discharging_max_c, 0);

	tag = CBI_TAG_BATT_VOLTAGE_MIN;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&info->voltage_min,
					 sizeof(info->voltage_min)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_VOLTAGE_NORMAL;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&info->voltage_normal,
					 sizeof(info->voltage_normal)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_VOLTAGE_MAX;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&info->voltage_max,
					 sizeof(info->voltage_max)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_PRECHARGE_VOLTAGE_CURRENT;
	mvma.mv = info->precharge_voltage;
	mvma.ma = info->precharge_current;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&mvma, sizeof(mvma)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_START_CHARGING_MIN_MAX_C;
	temp.min_c = info->start_charging_min_c;
	temp.max_c = info->start_charging_max_c;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&temp, sizeof(temp)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_CHARGING_MIN_MAX_C;
	temp.min_c = info->charging_min_c;
	temp.max_c = info->charging_max_c;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&temp, sizeof(temp)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_DISCHARGING_MIN_MAX_C;
	temp.min_c = info->discharging_min_c;
	temp.max_c = info->discharging_max_c;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&temp, sizeof(temp)),
		      EC_SUCCESS);

	/* Read */
	zassert_equal(batt_conf_read_battery_info(&default_battery_conf),
		      EC_SUCCESS);

	/* Validate default info == info in cbi. */
	zassert_equal(dflt->voltage_min, info->voltage_min);
	zassert_equal(dflt->voltage_normal, info->voltage_normal);
	zassert_equal(dflt->voltage_max, info->voltage_max);
	zassert_equal(dflt->precharge_voltage, info->precharge_voltage);
	zassert_equal(dflt->precharge_current, info->precharge_current);
	zassert_equal(dflt->start_charging_min_c, info->start_charging_min_c);
	zassert_equal(dflt->start_charging_max_c, info->start_charging_max_c);
	zassert_equal(dflt->charging_min_c, info->charging_min_c);
	zassert_equal(dflt->charging_max_c, info->charging_max_c);
	zassert_equal(dflt->discharging_min_c, info->discharging_min_c);
	zassert_equal(dflt->discharging_max_c, info->discharging_max_c);

	return EC_SUCCESS;
}

static union ec_common_control mock_common_control;
static int cbi_get_common_control_return;

int cbi_get_common_control(union ec_common_control *ctrl)
{
	*ctrl = mock_common_control;

	return cbi_get_common_control_return;
}

DECLARE_EC_TEST(test_batt_conf_main)
{
	mock_common_control.bcic_enabled = 1;
	cbi_get_common_control_return = EC_SUCCESS;
	init_battery_type_called = false;
	batt_conf_main();
	/* Check if the first entry is copied to the default. */
	zassert_equal(memcmp(&board_battery_info[0], &default_battery_conf,
			     sizeof(default_battery_conf)),
		      0);
	zassert_equal(init_battery_type_called, false);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_batt_conf_main_legacy)
{
	/* cbi_get_common_ctrl returns error. */
	mock_common_control.bcic_enabled = 0;
	cbi_get_common_control_return = EC_ERROR_UNKNOWN;
	init_battery_type_called = false;
	batt_conf_main();
	zassert_equal(init_battery_type_called, true);

	/* BCIC_ENABLED isn't set. */
	mock_common_control.bcic_enabled = 0;
	cbi_get_common_control_return = EC_SUCCESS;
	init_battery_type_called = false;
	batt_conf_main();
	zassert_equal(init_battery_type_called, true);

	return EC_SUCCESS;
}

TEST_SUITE(test_suite_battery_config)
{
	ztest_test_suite(
		test_battery_config,
		ztest_unit_test_setup_teardown(test_batt_conf_read, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_read_fuel_gauge_info,
					       test_setup, test_teardown),
		ztest_unit_test_setup_teardown(test_read_battery_info,
					       test_setup, test_teardown),
		ztest_unit_test_setup_teardown(test_batt_conf_main, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_batt_conf_main_legacy,
					       test_setup, test_teardown));
	ztest_run_test_suite(test_battery_config);
}

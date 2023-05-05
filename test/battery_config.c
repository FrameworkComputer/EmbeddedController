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

struct board_batt_params default_battery_conf = {};

static struct board_batt_params conf_in_cbi = {
	.fuel_gauge = {
		.manuf_name = { 'x', 'y', 'z' },
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

DECLARE_EC_TEST(test_read_ship_mode)
{
	struct ship_mode_info *info = &conf_in_cbi.fuel_gauge.ship_mode;
	struct ship_mode_info *dflt =
		&default_battery_conf.fuel_gauge.ship_mode;
	enum cbi_data_tag tag;
	uint8_t d8;

	/* Read without data in CBI. Test ERROR_UNKNOWN is correctly ignored. */
	zassert_equal(batt_conf_read_ship_mode(get_batt_params()), EC_SUCCESS);

	/* Validate default info remains unchanged. */
	zassert_equal(dflt->reg_addr, 0);
	zassert_equal(dflt->reg_data[0], 0);
	zassert_equal(dflt->reg_data[1], 0);
	zassert_equal(dflt->wb_support, 0);

	tag = CBI_TAG_BATT_SHIP_MODE_FLAGS;
	d8 = BIT(0);
	zassert_equal(cbi_set_board_info(tag, &d8, sizeof(d8)), EC_SUCCESS);
	tag = CBI_TAG_BATT_SHIP_MODE_REG_ADDR;
	zassert_equal(cbi_set_board_info(tag, &info->reg_addr,
					 sizeof(info->reg_addr)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_SHIP_MODE_REG_DATA;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&info->reg_data,
					 sizeof(info->reg_data)),
		      EC_SUCCESS);

	/* Read */
	zassert_equal(batt_conf_read_ship_mode(&default_battery_conf),
		      EC_SUCCESS);

	/* Validate default info == info in cbi. */
	zassert_equal(dflt->reg_addr, info->reg_addr);
	zassert_equal(dflt->reg_data[0], info->reg_data[0]);
	zassert_equal(dflt->reg_data[1], info->reg_data[1]);
	zassert_equal(dflt->wb_support, 1);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_read_sleep_mode)
{
	struct sleep_mode_info *info = &conf_in_cbi.fuel_gauge.sleep_mode;
	struct sleep_mode_info *dflt =
		&default_battery_conf.fuel_gauge.sleep_mode;
	enum cbi_data_tag tag;
	uint8_t d8;

	/* Read without data in CBI. Test ERROR_UNKNOWN is correctly ignored. */
	zassert_equal(batt_conf_read_sleep_mode(get_batt_params()), EC_SUCCESS);

	/* Validate default info remains unchanged. */
	zassert_equal(dflt->reg_addr, 0);
	zassert_equal(dflt->reg_data, 0);
	zassert_equal(dflt->sleep_supported, 0);

	tag = CBI_TAG_BATT_SLEEP_MODE_FLAGS;
	d8 = BIT(0);
	zassert_equal(cbi_set_board_info(tag, &d8, sizeof(d8)), EC_SUCCESS);
	tag = CBI_TAG_BATT_SLEEP_MODE_REG_ADDR;
	zassert_equal(cbi_set_board_info(tag, &info->reg_addr,
					 sizeof(info->reg_addr)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_SLEEP_MODE_REG_DATA;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&info->reg_data,
					 sizeof(info->reg_data)),
		      EC_SUCCESS);

	/* Read */
	zassert_equal(batt_conf_read_sleep_mode(get_batt_params()), EC_SUCCESS);

	/* Validate default info == info in cbi. */
	zassert_equal(dflt->reg_addr, info->reg_addr);
	zassert_equal(dflt->reg_data, info->reg_data);
	zassert_equal(dflt->sleep_supported, 1);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_read_fet_info)
{
	struct fet_info *info = &conf_in_cbi.fuel_gauge.fet;
	struct fet_info *dflt = &default_battery_conf.fuel_gauge.fet;
	enum cbi_data_tag tag;
	uint8_t d8;

	/* Read without data in CBI. Test ERROR_UNKNOWN is correctly ignored. */
	zassert_equal(batt_conf_read_fet_info(get_batt_params()), EC_SUCCESS);

	/* Validate default info remains unchanged. */
	zassert_equal(dflt->reg_addr, 0);
	zassert_equal(dflt->reg_mask, 0);
	zassert_equal(dflt->mfgacc_support, 0);

	tag = CBI_TAG_BATT_FET_FLAGS;
	d8 = BIT(0);
	zassert_equal(cbi_set_board_info(tag, &d8, sizeof(d8)), EC_SUCCESS);
	tag = CBI_TAG_BATT_FET_REG_ADDR;
	zassert_equal(cbi_set_board_info(tag, &info->reg_addr,
					 sizeof(info->reg_addr)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_FET_REG_MASK;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&info->reg_mask,
					 sizeof(info->reg_mask)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_FET_DISCONNECT_VAL;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&info->disconnect_val,
					 sizeof(info->disconnect_val)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_FET_CFET_MASK;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&info->cfet_mask,
					 sizeof(info->cfet_mask)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_FET_CFET_OFF_VAL;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&info->cfet_off_val,
					 sizeof(info->cfet_off_val)),
		      EC_SUCCESS);

	/* Read */
	zassert_equal(batt_conf_read_fet_info(get_batt_params()), EC_SUCCESS);

	zassert_equal(dflt->reg_addr, info->reg_addr);
	zassert_equal(dflt->reg_mask, info->reg_mask);
	zassert_equal(dflt->mfgacc_support, 1);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_read_fuel_gauge_info)
{
	struct fuel_gauge_info *info = &conf_in_cbi.fuel_gauge;
	struct fuel_gauge_info *dflt = &default_battery_conf.fuel_gauge;
	enum cbi_data_tag tag;
	uint8_t d8;
	const char empty[32] = {};

	/* Read without data in CBI. Test ERROR_UNKNOWN is correctly ignored. */
	zassert_equal(batt_conf_read_fuel_gauge_info(get_batt_params()),
		      EC_SUCCESS);

	/* Validate default info remains unchanged. */
	zassert_equal(memcmp(dflt->manuf_name, empty, sizeof(empty)), 0);
	zassert_equal(memcmp(dflt->device_name, empty, sizeof(empty)), 0);
	zassert_equal(dflt->override_nil, 0);

	tag = CBI_TAG_FUEL_GAUGE_MANUF_NAME;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&info->manuf_name,
					 sizeof(info->manuf_name)),
		      EC_SUCCESS);
	tag = CBI_TAG_FUEL_GAUGE_DEVICE_NAME;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&info->device_name,
					 sizeof(info->device_name)),
		      EC_SUCCESS);
	tag = CBI_TAG_FUEL_GAUGE_FLAGS;
	d8 = BIT(0);
	zassert_equal(cbi_set_board_info(tag, &d8, sizeof(d8)), EC_SUCCESS);

	/* Read */
	zassert_equal(batt_conf_read_fuel_gauge_info(get_batt_params()),
		      EC_SUCCESS);

	/* Validate default info == info in cbi. */
	zassert_equal(memcmp(dflt->manuf_name, info->manuf_name,
			     sizeof(info->manuf_name)),
		      0);
	zassert_equal(memcmp(dflt->device_name, info->device_name,
			     sizeof(info->device_name)),
		      0);
	zassert_equal(dflt->override_nil, 1);

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_read_battery_info)
{
	struct battery_info *info = &conf_in_cbi.batt_info;
	struct battery_info *dflt = &default_battery_conf.batt_info;
	enum cbi_data_tag tag;

	/* Read without data in CBI. Test ERROR_UNKNOWN is correctly ignored. */
	zassert_equal(batt_conf_read_battery_info(get_batt_params()),
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
	tag = CBI_TAG_BATT_PRECHARGE_VOLTAGE;
	zassert_equal(cbi_set_board_info(tag,
					 (uint8_t *)&info->precharge_voltage,
					 sizeof(info->precharge_voltage)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_PRECHARGE_CURRENT;
	zassert_equal(cbi_set_board_info(tag,
					 (uint8_t *)&info->precharge_current,
					 sizeof(info->precharge_current)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_START_CHARGING_MIN_C;
	zassert_equal(cbi_set_board_info(tag,
					 (uint8_t *)&info->start_charging_min_c,
					 sizeof(info->start_charging_min_c)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_START_CHARGING_MAX_C;
	zassert_equal(cbi_set_board_info(tag,
					 (uint8_t *)&info->start_charging_max_c,
					 sizeof(info->start_charging_max_c)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_CHARGING_MIN_C;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&info->charging_min_c,
					 sizeof(info->charging_min_c)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_CHARGING_MAX_C;
	zassert_equal(cbi_set_board_info(tag, (uint8_t *)&info->charging_max_c,
					 sizeof(info->charging_max_c)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_DISCHARGING_MIN_C;
	zassert_equal(cbi_set_board_info(tag,
					 (uint8_t *)&info->discharging_min_c,
					 sizeof(info->discharging_min_c)),
		      EC_SUCCESS);
	tag = CBI_TAG_BATT_DISCHARGING_MAX_C;
	zassert_equal(cbi_set_board_info(tag,
					 (uint8_t *)&info->discharging_max_c,
					 sizeof(info->discharging_max_c)),
		      EC_SUCCESS);

	/* Read */
	zassert_equal(batt_conf_read_battery_info(get_batt_params()),
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

TEST_SUITE(test_suite_battery_config)
{
	ztest_test_suite(
		test_battery_config,
		ztest_unit_test_setup_teardown(test_batt_conf_read, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_read_ship_mode, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_read_sleep_mode, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_read_fet_info, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_read_fuel_gauge_info,
					       test_setup, test_teardown),
		ztest_unit_test_setup_teardown(test_read_battery_info,
					       test_setup, test_teardown));
	ztest_run_test_suite(test_battery_config);
}

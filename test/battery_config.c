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

void batt_conf_main(void);

const struct batt_conf_embed board_battery_info[] = {
	[BATTERY_C214] = {
		.manuf_name = "AS1GUXd3KB",
		.device_name = "C214-43",
		.config = {
			.fuel_gauge = {
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
	},
};

static struct board_batt_params conf_in_cbi = {
	.fuel_gauge = {
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

static const char *manuf_in_batt = "AS1GUXd3KB";
static const char *device_in_batt = "C214-43";

int battery_manufacturer_name(char *dest, int size)
{
	if (!manuf_in_batt)
		return EC_ERROR_UNKNOWN;
	strncpy(dest, manuf_in_batt, size);
	return EC_SUCCESS;
}

int battery_device_name(char *dest, int size)
{
	if (!device_in_batt)
		return EC_ERROR_UNKNOWN;
	strncpy(dest, device_in_batt, size);
	return EC_SUCCESS;
}

const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_C214;

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

static union ec_common_control mock_common_control;
static int cbi_get_common_control_return;

int cbi_get_common_control(union ec_common_control *ctrl)
{
	*ctrl = mock_common_control;

	return cbi_get_common_control_return;
}

DECLARE_EC_TEST(test_batt_conf_main)
{
	struct batt_conf_export head;
	const struct board_batt_params *param;

	/* On POR, no config in CBI. Legacy mode should choose conf[0]. */
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);

	ccprintf("Blob size = %lu (config = %lu)\n", sizeof(head),
		 sizeof(struct board_batt_params));

	/* Enable BCIC. */
	mock_common_control.bcic_enabled = 1;
	cbi_get_common_control_return = EC_SUCCESS;

	/*
	 * manuf_name != manuf_name
	 */
	ccprintf("\nmanuf_name != manuf_name\n");
	head.struct_version = 0;
	strncpy(head.manuf_name, "foo", sizeof("foo"));
	memset(head.device_name, 0, sizeof(head.device_name));
	memcpy(&head.config, &conf_in_cbi, sizeof(head.config));
	cbi_set_board_info(CBI_TAG_BATTERY_CONFIG, (void *)&head, sizeof(head));
	batt_conf_main();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);

	/*
	 * manuf_name == manuf_name && device_name == ""
	 */
	ccprintf("\nmanuf_name == manuf_name && device_name == \"\"\n");
	strncpy(head.manuf_name, "AS1GUXd3KB", sizeof("AS1GUXd3KB"));
	cbi_set_board_info(CBI_TAG_BATTERY_CONFIG, (void *)&head, sizeof(head));
	batt_conf_main();
	param = get_batt_params();
	zassert_equal(memcmp(param, &conf_in_cbi, sizeof(*param)), 0);

	/*
	 * manuf_name == manuf_name && device_name != device_name
	 */
	ccprintf("\nmanuf_name == manuf_name && device_name != device_name\n");
	strncpy(head.device_name, "foo", sizeof("foo"));
	cbi_set_board_info(CBI_TAG_BATTERY_CONFIG, (void *)&head, sizeof(head));
	batt_conf_main();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);

	/*
	 * manuf_name == manuf_name && device_name == device_name
	 */
	ccprintf("\nmanuf_name == manuf_name && device_name == device_name\n");
	strncpy(head.device_name, "C214-43", sizeof("C214-43"));
	cbi_set_board_info(CBI_TAG_BATTERY_CONFIG, (void *)&head, sizeof(head));
	batt_conf_main();
	param = get_batt_params();
	zassert_equal(memcmp(param, &conf_in_cbi, sizeof(*param)), 0);

	/*
	 * Manuf name not found in battery.
	 */
	ccprintf("\nManuf name not found.\n");
	manuf_in_batt = NULL;
	batt_conf_main();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);
	manuf_in_batt = "AS1GUXd3KB";

	/*
	 * Device name not found in battery.
	 */
	ccprintf("\nDevice name not found.\n");
	device_in_batt = NULL;
	batt_conf_main();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);
	device_in_batt = "C214-43";

	/*
	 * Version mismatch
	 */
	ccprintf("\nVersion mismatch\n");
	head.struct_version = 0x01;
	cbi_set_board_info(CBI_TAG_BATTERY_CONFIG, (void *)&head, sizeof(head));
	batt_conf_main();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);

	return EC_SUCCESS;
}

TEST_SUITE(test_suite_battery_config)
{
	ztest_test_suite(test_battery_config,
			 ztest_unit_test_setup_teardown(test_batt_conf_main,
							test_setup,
							test_teardown));
	ztest_run_test_suite(test_battery_config);
}

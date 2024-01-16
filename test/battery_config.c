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

const struct board_batt_params *get_batt_params(void);

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

static void cbi_set_batt_conf(const struct board_batt_params *conf,
			      const char *manuf_name, const char *device_name)
{
	uint8_t buf[BATT_CONF_MAX_SIZE];
	struct batt_conf_header *head = (void *)buf;
	void *p = buf;
	uint8_t size;

	head->struct_version = 0;
	head->manuf_name_size = strlen(manuf_name);
	head->device_name_size = strlen(device_name);

	/* Copy names. Don't copy the terminating null. */
	p += sizeof(*head);
	memcpy(p, manuf_name, head->manuf_name_size);
	p += head->manuf_name_size;
	memcpy(p, device_name, head->device_name_size);
	p += head->device_name_size;
	memcpy(p, conf, sizeof(*conf));

	size = sizeof(*head) + head->manuf_name_size + head->device_name_size +
	       sizeof(*conf);
	cbi_set_board_info(CBI_TAG_BATTERY_CONFIG, buf, size);
}

DECLARE_EC_TEST(test_batt_conf_main)
{
	const struct board_batt_params *conf;

	/* On POR, no config in CBI. Legacy mode should choose conf[0]. */
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);

	ccprintf("sizeof(struct batt_batt_params) = %lu)\n", sizeof(*conf));

	/* Enable BCIC. */
	mock_common_control.bcic_enabled = 1;
	cbi_get_common_control_return = EC_SUCCESS;

	/*
	 * manuf_name != manuf_name
	 */
	ccprintf("\nmanuf_name != manuf_name\n");
	cbi_set_batt_conf(&conf_in_cbi, "foo", "");
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);

	/*
	 * manuf_name == manuf_name && device_name == ""
	 */
	ccprintf("\nmanuf_name == manuf_name && device_name == \"\"\n");
	cbi_set_batt_conf(&conf_in_cbi, "AS1GUXd3KB", "");
	init_battery_type();
	conf = get_batt_params();
	zassert_equal(memcmp(conf, &conf_in_cbi, sizeof(*conf)), 0);
	zassert_equal(strcmp(get_batt_conf()->manuf_name, "AS1GUXd3KB"), 0);

	/*
	 * manuf_name == manuf_name && device_name != device_name
	 */
	ccprintf("\nmanuf_name == manuf_name && device_name != device_name\n");
	cbi_set_batt_conf(&conf_in_cbi, "AS1GUXd3KB", "foo");
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);

	/*
	 * manuf_name == manuf_name && device_name == device_name
	 */
	ccprintf("\nmanuf_name == manuf_name && device_name == device_name\n");
	cbi_set_batt_conf(&conf_in_cbi, "AS1GUXd3KB", "C214-43");
	init_battery_type();
	conf = get_batt_params();
	zassert_equal(memcmp(conf, &conf_in_cbi, sizeof(*conf)), 0);
	zassert_equal(strcmp(get_batt_conf()->manuf_name, "AS1GUXd3KB"), 0);
	zassert_equal(strcmp(get_batt_conf()->device_name, "C214-43"), 0);

	/*
	 * Battery's device name contains extra chars.
	 */
	ccprintf("\nmanuf_name == manuf_name && device_name has extra chars\n");
	device_in_batt = "C214-43 xyz";
	init_battery_type();
	conf = get_batt_params();
	zassert_equal(memcmp(conf, &conf_in_cbi, sizeof(*conf)), 0);
	zassert_equal(strcmp(get_batt_conf()->manuf_name, "AS1GUXd3KB"), 0);
	zassert_equal(strcmp(get_batt_conf()->device_name, "C214-43"), 0);

	/*
	 * Manuf name not found in battery.
	 */
	ccprintf("\nManuf name not found.\n");
	manuf_in_batt = NULL;
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);
	manuf_in_batt = "AS1GUXd3KB";

	/*
	 * Device name not found in battery.
	 */
	ccprintf("\nDevice name not found.\n");
	device_in_batt = NULL;
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);
	device_in_batt = "C214-43";

	return EC_SUCCESS;
}

DECLARE_EC_TEST(test_batt_conf_main_invalid)
{
	struct batt_conf_header head;

	/*
	 * Version mismatch
	 */
	ccprintf("\nVersion mismatch\n");
	head.struct_version = EC_BATTERY_CONFIG_STRUCT_VERSION + 1;
	cbi_set_board_info(CBI_TAG_BATTERY_CONFIG, (void *)&head, sizeof(head));
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);
	head.struct_version = EC_BATTERY_CONFIG_STRUCT_VERSION;

	/*
	 * Size mismatch
	 */
	ccprintf("\nSize mismatch\n");
	head.manuf_name_size = 0xff;
	cbi_set_board_info(CBI_TAG_BATTERY_CONFIG, (void *)&head, sizeof(head));
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);

	return EC_SUCCESS;
}

TEST_SUITE(test_suite_battery_config)
{
	ztest_test_suite(
		test_battery_config,
		ztest_unit_test_setup_teardown(test_batt_conf_main, test_setup,
					       test_teardown),
		ztest_unit_test_setup_teardown(test_batt_conf_main_invalid,
					       test_setup, test_teardown));
	ztest_run_test_suite(test_battery_config);
}

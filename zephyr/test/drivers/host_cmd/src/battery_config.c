/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST_USER(battery_config, test_get)
{
	uint8_t buf[BATT_CONF_MAX_SIZE];
	struct host_cmd_handler_args args;
	struct batt_conf_header *head;
	struct board_batt_params conf;
	uint8_t *p = buf;
	int expected;

	zassert_ok(ec_cmd_battery_config(&args, buf));

	/* Verify metadata. */
	head = (struct batt_conf_header *)buf;
	zassert_equal(head->struct_version, EC_BATTERY_CONFIG_STRUCT_VERSION);
	expected = sizeof(*head) + head->manuf_name_size +
		   head->device_name_size + sizeof(conf);
	zassert_equal(args.response_size, expected);

	/* Verify manuf name match. */
	p += sizeof(*head);
	zassert_equal(head->manuf_name_size, strlen("LGC"));
	zassert_equal(strncmp(p, "LGC", strlen("LGC")), 0);

	/* Verify device name match. */
	p += head->manuf_name_size;
	zassert_equal(head->device_name_size, strlen("AC17A8M"));
	zassert_equal(strncmp(p, "AC17A8M", strlen("AC17A8M")), 0);

	/* Verify config match. */
	p += head->device_name_size;
	memcpy(&conf, p, sizeof(conf));
	zassert_equal(conf.fuel_gauge.fet.reg_mask, 0x2000);
	zassert_equal(conf.batt_info.voltage_max, 13134);
	zassert_equal(conf.batt_info.precharge_current, 256);
}

ZTEST_SUITE(battery_config, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

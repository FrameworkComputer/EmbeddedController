/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/shell/shell.h>
#include <zephyr/zephyr.h>
#include <zephyr/ztest.h>

#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "host_command.h"

/**
 * @brief TestPurpose: test the TCPC locate valid case.
 */
ZTEST_USER(locate_chip, test_hc_locate_chip_tcpc)
{
	int ret;
	struct ec_params_locate_chip p;
	struct ec_response_locate_chip r;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_LOCATE_CHIP, 0, r, p);

	p.type = EC_CHIP_TYPE_TCPC;
	p.index = 0;

	ret = host_command_process(&args);

	zassert_equal(ret, EC_RES_SUCCESS, "Unexpected return value: %d", ret);
	zassert_equal(r.bus_type, EC_BUS_TYPE_I2C, "Unexpected bus_type: %d",
		      r.bus_type);
	zassert_equal(r.i2c_info.port, 2, "Unexpected port: %d",
		      r.i2c_info.port);
	zassert_equal(r.i2c_info.addr_flags, 0x82, "Unexpected addr_flags: %d",
		      r.i2c_info.addr_flags);

	p.type = EC_CHIP_TYPE_TCPC;
	p.index = 1;

	ret = host_command_process(&args);

	zassert_equal(ret, EC_RES_SUCCESS, "Unexpected return value: %d", ret);
	zassert_equal(r.bus_type, EC_BUS_TYPE_I2C, "Unexpected bus_type: %d",
		      r.bus_type);
	zassert_equal(r.i2c_info.port, 3, "Unexpected port: %d",
		      r.i2c_info.port);
	zassert_equal(r.i2c_info.addr_flags, 0x0b, "Unexpected addr_flags: %d",
		      r.i2c_info.addr_flags);
}

/**
 * @brief TestPurpose: test the TCPC index overflow case.
 */
ZTEST_USER(locate_chip, test_hc_locate_chip_tcpc_overflow)
{
	int ret;
	struct ec_params_locate_chip p;
	struct ec_response_locate_chip r;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_LOCATE_CHIP, 0, r, p);

	p.type = EC_CHIP_TYPE_TCPC;
	p.index = 10;

	ret = host_command_process(&args);

	zassert_equal(ret, EC_RES_OVERFLOW, "Unexpected return value: %d", ret);
}

/**
 * @brief TestPurpose: test the EEPROM locate valid case.
 */
ZTEST_USER(locate_chip, test_hc_locate_chip_eeprom)
{
	int ret;
	struct ec_params_locate_chip p;
	struct ec_response_locate_chip r;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_LOCATE_CHIP, 0, r, p);

	p.type = EC_CHIP_TYPE_CBI_EEPROM;
	p.index = 0;

	ret = host_command_process(&args);

	zassert_equal(ret, EC_RES_SUCCESS, "Unexpected return value: %d", ret);
	zassert_equal(r.bus_type, EC_BUS_TYPE_I2C, "Unexpected bus_type: %d",
		      r.bus_type);
	zassert_equal(r.i2c_info.port, I2C_PORT_EEPROM, "Unexpected port: %d",
		      r.i2c_info.port);
	zassert_equal(r.i2c_info.addr_flags, I2C_ADDR_EEPROM_FLAGS,
		      "Unexpected addr_flags: %d", r.i2c_info.addr_flags);
}

/**
 * @brief TestPurpose: test the EEPROM index overflow case.
 */
ZTEST_USER(locate_chip, test_hc_locate_chip_eeprom_overflow)
{
	int ret;
	struct ec_params_locate_chip p;
	struct ec_response_locate_chip r;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_LOCATE_CHIP, 0, r, p);

	p.type = EC_CHIP_TYPE_CBI_EEPROM;
	p.index = 1;

	ret = host_command_process(&args);

	zassert_equal(ret, EC_RES_OVERFLOW, "Unexpected return value: %d", ret);
}

/**
 * @brief TestPurpose: test the invalid parameter case.
 */
ZTEST_USER(locate_chip, test_hc_locate_chip_invalid)
{
	int ret;
	struct ec_params_locate_chip p;
	struct ec_response_locate_chip r;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_LOCATE_CHIP, 0, r, p);

	p.type = EC_CHIP_TYPE_COUNT;
	ret = host_command_process(&args);

	zassert_equal(ret, EC_RES_INVALID_PARAM, "Unexpected return value: %d",
		      ret);
}

ZTEST_SUITE(locate_chip, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

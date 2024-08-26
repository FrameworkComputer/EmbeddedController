/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "hooks.h"
#include "i2c.h"
#include "rex_fakes.h"

#include <stdbool.h>

#include <zephyr/devicetree.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);

static void update_usb_db(int (*m)(enum cbi_fw_config_field_id field_id,
				   uint32_t *value))
{
	cros_cbi_get_fw_config_fake.custom_fake = m;
	hook_notify(HOOK_INIT);
}

ZTEST_USER(i2c_policy, test_deny_no_cbi)
{
	const struct i2c_cmd_desc_t cmd_desc_ps = {
		.port = I2C_PORT_BY_DEV(DT_NODELABEL(tcpc_ps8815_port1)),
		.addr_flags = 0x0b,
	};
	const struct i2c_cmd_desc_t cmd_desc_anx = {
		.port = I2C_PORT_BY_DEV(DT_NODELABEL(usb_c1_anx7452_retimer)),
		.addr_flags = 0x10,
	};

	update_usb_db(mock_cros_cbi_get_fw_config_fail);

	zassert_equal(board_allow_i2c_passthru(&cmd_desc_ps), false);
	zassert_equal(board_allow_i2c_passthru(&cmd_desc_anx), false);
}

ZTEST_USER(i2c_policy, test_deny_unknown)
{
	/* Some implausible I2C target */
	const struct i2c_cmd_desc_t cmd_desc_99 = {
		.port = 99,
		.addr_flags = 0x99,
	};

	zassert_equal(board_allow_i2c_passthru(&cmd_desc_99), false);
}

ZTEST_USER(i2c_policy, test_deny_tcpc0)
{
	const struct i2c_cmd_desc_t cmd_desc_tcpc0 = {
		.port = I2C_PORT_BY_DEV(DT_NODELABEL(tcpc_port0)),
		.addr_flags = 0x70,
	};

	zassert_equal(board_allow_i2c_passthru(&cmd_desc_tcpc0), false);
}

ZTEST_USER(i2c_policy, test_deny_hb)
{
	const struct i2c_cmd_desc_t cmd_desc_hb0 = {
		.port = I2C_PORT_BY_DEV(DT_NODELABEL(usb_c0_hb_retimer)),
		.addr_flags = 0x56,
	};
	const struct i2c_cmd_desc_t cmd_desc_hb1 = {
		.port = I2C_PORT_BY_DEV(DT_NODELABEL(usb_c1_hb_retimer)),
		.addr_flags = 0x56,
	};

	update_usb_db(mock_cros_cbi_get_fw_config_hb);

	zassert_equal(board_allow_i2c_passthru(&cmd_desc_hb0), false);
	zassert_equal(board_allow_i2c_passthru(&cmd_desc_hb1), false);
}

ZTEST_USER(i2c_policy, test_allow_c1_anx_only)
{
	const struct i2c_cmd_desc_t cmd_desc_anx = {
		.port = I2C_PORT_BY_DEV(DT_NODELABEL(usb_c1_anx7452_retimer)),
		.addr_flags = 0x10,
	};

	zassert_equal(board_allow_i2c_passthru(&cmd_desc_anx), false);

	update_usb_db(mock_cros_cbi_get_fw_config_anx7452_v2);

	zassert_equal(board_allow_i2c_passthru(&cmd_desc_anx), true);
}

ZTEST_USER(i2c_policy, test_allow_c1_usb3_only)
{
	const struct i2c_cmd_desc_t cmd_desc_ps = {
		.port = I2C_PORT_BY_DEV(DT_NODELABEL(tcpc_ps8815_port1)),
		.addr_flags = 0x0b,
	};

	zassert_equal(board_allow_i2c_passthru(&cmd_desc_ps), false);

	update_usb_db(mock_cros_cbi_get_fw_config_usb3);

	zassert_equal(board_allow_i2c_passthru(&cmd_desc_ps), true);
}

static void i2c_policy_before(void *fixture)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_no_usb_db;
}

ZTEST_SUITE(i2c_policy, NULL, NULL, i2c_policy_before, NULL, NULL);

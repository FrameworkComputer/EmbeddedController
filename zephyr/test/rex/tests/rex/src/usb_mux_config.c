/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "driver/tcpm/nct38xx.h"
#include "hooks.h"
#include "rex_fakes.h"
#include "usb_mux.h"
#include "usb_mux_config.h"
#include "usb_pd.h"
#include "usbc/usb_muxes.h"
#include "usbc_config.h"

#include <zephyr/devicetree.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(enum nct38xx_boot_type, nct38xx_get_boot_type, int);
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);
FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);

static void usb_mux_config_before(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(cros_cbi_get_fw_config);

	RESET_FAKE(nct38xx_get_boot_type);
	nct38xx_get_boot_type_fake.return_val = NCT38XX_BOOT_NORMAL;
	ppc_vbus_sink_enable_fake.return_val = EC_SUCCESS;
}

ZTEST_USER(usb_mux_config, test_setup_usb_db)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_usb3;

	hook_notify(HOOK_INIT);

	zassert_equal(1, cros_cbi_get_fw_config_fake.call_count);
	zassert_equal(1, usb_db_type);

	zassert_true(board_is_tbt_usb4_port(USBC_PORT_C0));
	zassert_false(board_is_tbt_usb4_port(USBC_PORT_C1));
}

ZTEST_USER(usb_mux_config, test_setup_usb_db_anx7452)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_anx7452;

	hook_notify(HOOK_INIT);

	zassert_equal(1, cros_cbi_get_fw_config_fake.call_count);
	zassert_equal(3, usb_db_type);
}

ZTEST_USER(usb_mux_config, test_setup_usb_db_hb)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_hb;

	hook_notify(HOOK_INIT);

	zassert_equal(1, cros_cbi_get_fw_config_fake.call_count);
	zassert_equal(4, usb_db_type);

	zassert_true(board_is_tbt_usb4_port(USBC_PORT_C0));
	zassert_true(board_is_tbt_usb4_port(USBC_PORT_C1));
}

ZTEST_USER(usb_mux_config, test_setup_usb_db_kb8010)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_kb8010;

	hook_notify(HOOK_INIT);

	zassert_equal(1, cros_cbi_get_fw_config_fake.call_count);
	zassert_equal(2, usb_db_type);
}

ZTEST_USER(usb_mux_config, test_setup_usb_db_no_usb_db)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_no_usb_db;

	hook_notify(HOOK_INIT);

	zassert_equal(1, cros_cbi_get_fw_config_fake.call_count);
	zassert_equal(0, usb_db_type);
}

ZTEST_USER(usb_mux_config, test_setup_usb_db_error_reading_cbi)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_fail;

	hook_notify(HOOK_INIT);

	zassert_equal(1, cros_cbi_get_fw_config_fake.call_count);
	/* 0 is the initial value */
	zassert_equal(0, usb_db_type);
}

ZTEST_USER(usb_mux_config, test_reset_pd_mcu_usb3)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_usb3;

	hook_notify(HOOK_INIT);

	board_reset_pd_mcu();
}

ZTEST_USER(usb_mux_config, test_reset_pd_mcu_hb)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_hb;

	hook_notify(HOOK_INIT);

	board_reset_pd_mcu();
}

ZTEST_USER(usb_mux_config, test_charge_port_none)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_hb;

	hook_notify(HOOK_INIT);

	zassert_equal(EC_ERROR_INVAL, board_set_active_charge_port(9));
	zassert_equal(EC_SUCCESS,
		      board_set_active_charge_port(CHARGE_PORT_NONE));
	zassert_equal(EC_SUCCESS, board_set_active_charge_port(USBC_PORT_C0));
}

ZTEST_USER(usb_mux_config, test_charge_port_dead_battery)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		mock_cros_cbi_get_fw_config_hb;

	hook_notify(HOOK_INIT);

	nct38xx_get_boot_type_fake.return_val = NCT38XX_BOOT_DEAD_BATTERY;
	zassert_equal(EC_SUCCESS,
		      board_set_active_charge_port(CHARGE_PORT_NONE));
	zassert_equal(EC_SUCCESS, board_set_active_charge_port(USBC_PORT_C0));
}

ZTEST_SUITE(usb_mux_config, NULL, NULL, usb_mux_config_before, NULL, NULL);

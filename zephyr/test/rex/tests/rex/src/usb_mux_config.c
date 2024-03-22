/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "hooks.h"
#include "usb_mux.h"
#include "usb_mux_config.h"
#include "usbc/usb_muxes.h"

#include <zephyr/devicetree.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);

int mock_cros_cbi_get_fw_config(enum cbi_fw_config_field_id field_id,
				uint32_t *value)
{
	*value = FW_USB_DB_USB3;
	return 0;
}

int mock_cros_cbi_get_fw_config_anx7452(enum cbi_fw_config_field_id field_id,
					uint32_t *value)
{
	*value = FW_USB_DB_USB4_ANX7452;
	return 0;
}

int mock_cros_cbi_get_fw_config_hb(enum cbi_fw_config_field_id field_id,
				   uint32_t *value)
{
	*value = FW_USB_DB_USB4_HB;
	return 0;
}

int mock_cros_cbi_get_fw_config_no_usb_db(enum cbi_fw_config_field_id field_id,
					  uint32_t *value)
{
	*value = FW_USB_DB_NOT_CONNECTED;
	return 0;
}

int mock_cros_cbi_get_fw_config_error(enum cbi_fw_config_field_id field_id,
				      uint32_t *value)
{
	*value = FW_USB_DB_NOT_CONNECTED;
	return -1;
}

static void usb_mux_config_before(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(cros_cbi_get_fw_config);
}

ZTEST_USER(usb_mux_config, test_setup_usb_db)
{
	cros_cbi_get_fw_config_fake.custom_fake = mock_cros_cbi_get_fw_config;

	hook_notify(HOOK_INIT);

	zassert_equal(1, cros_cbi_get_fw_config_fake.call_count);
	zassert_equal(1, usb_db_type);
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
		mock_cros_cbi_get_fw_config_error;

	hook_notify(HOOK_INIT);

	zassert_equal(1, cros_cbi_get_fw_config_fake.call_count);
	zassert_equal(0, usb_db_type);
}

ZTEST_SUITE(usb_mux_config, NULL, NULL, usb_mux_config_before, NULL, NULL);

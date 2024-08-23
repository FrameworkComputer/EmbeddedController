/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rex_fakes.h"

DEFINE_FAKE_VALUE_FUNC(int, board_is_sourcing_vbus, int);
DEFINE_FAKE_VALUE_FUNC(int, pd_check_vconn_swap, int);
DEFINE_FAKE_VALUE_FUNC(int, pd_set_power_supply_ready, int);
DEFINE_FAKE_VOID_FUNC(charge_manager_update_charge, int, int,
		      const struct charge_port_info *);
DEFINE_FAKE_VOID_FUNC(host_set_single_event, enum host_event_code);
DEFINE_FAKE_VOID_FUNC(pd_power_supply_reset, int);
DEFINE_FAKE_VOID_FUNC(pd_set_input_current_limit, int, uint32_t, uint32_t);

int mock_cros_cbi_get_fw_config_fail(enum cbi_fw_config_field_id field_id,
				     uint32_t *value)
{
	return -EINVAL;
}

int mock_cros_cbi_get_fw_config_no_usb_db(enum cbi_fw_config_field_id field_id,
					  uint32_t *value)
{
	if (field_id == FW_USB_DB) {
		*value = FW_USB_DB_NOT_CONNECTED;
		return 0;
	}
	return -EINVAL;
}

int mock_cros_cbi_get_fw_config_anx7452(enum cbi_fw_config_field_id field_id,
					uint32_t *value)
{
	if (field_id == FW_USB_DB) {
		*value = FW_USB_DB_USB4_ANX7452;
		return 0;
	}
	return -EINVAL;
}

int mock_cros_cbi_get_fw_config_anx7452_v2(enum cbi_fw_config_field_id field_id,
					   uint32_t *value)
{
	if (field_id == FW_USB_DB) {
		*value = FW_USB_DB_USB4_ANX7452_V2;
		return 0;
	}
	return -EINVAL;
}

int mock_cros_cbi_get_fw_config_hb(enum cbi_fw_config_field_id field_id,
				   uint32_t *value)
{
	if (field_id == FW_USB_DB) {
		*value = FW_USB_DB_USB4_HB;
		return 0;
	}
	return -EINVAL;
}

int mock_cros_cbi_get_fw_config_kb8010(enum cbi_fw_config_field_id field_id,
				       uint32_t *value)
{
	if (field_id == FW_USB_DB) {
		*value = FW_USB_DB_USB4_KB8010;
		return 0;
	}
	return -EINVAL;
}

int mock_cros_cbi_get_fw_config_usb3(enum cbi_fw_config_field_id field_id,
				     uint32_t *value)
{
	if (field_id == FW_USB_DB) {
		*value = FW_USB_DB_USB3;
		return 0;
	}
	return -EINVAL;
}

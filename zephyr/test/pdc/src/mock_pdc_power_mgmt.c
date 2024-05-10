/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock_pdc_power_mgmt.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/fff.h>

/* FFF fake definitions for select functions in `pdc_power_mgmt.c` */

DEFINE_FAKE_VALUE_FUNC(int, pdc_power_mgmt_connector_reset, int,
		       enum connector_reset);
DEFINE_FAKE_VALUE_FUNC(int, pdc_power_mgmt_get_cable_prop, int,
		       union cable_property_t *);
DEFINE_FAKE_VALUE_FUNC(int, pdc_power_mgmt_get_connector_status, int,
		       union connector_status_t *);
DEFINE_FAKE_VALUE_FUNC(int, pdc_power_mgmt_get_info, int, struct pdc_info_t *,
		       bool);
DEFINE_FAKE_VALUE_FUNC(bool, pdc_power_mgmt_get_partner_data_swap_capable, int);
DEFINE_FAKE_VALUE_FUNC(enum pd_power_role, pdc_power_mgmt_get_power_role, int);
DEFINE_FAKE_VALUE_FUNC(const char *, pdc_power_mgmt_get_task_state_name, int);
DEFINE_FAKE_VALUE_FUNC(bool, pdc_power_mgmt_is_connected, int);
DEFINE_FAKE_VALUE_FUNC(enum pd_data_role, pdc_power_mgmt_pd_get_data_role, int);
DEFINE_FAKE_VALUE_FUNC(enum tcpc_cc_polarity, pdc_power_mgmt_pd_get_polarity,
		       int);
DEFINE_FAKE_VOID_FUNC(pdc_power_mgmt_request_data_swap, int);
DEFINE_FAKE_VOID_FUNC(pdc_power_mgmt_request_power_swap, int);
DEFINE_FAKE_VALUE_FUNC(int, pdc_power_mgmt_reset, int);
DEFINE_FAKE_VALUE_FUNC(int, pdc_power_mgmt_set_comms_state, bool);
DEFINE_FAKE_VOID_FUNC(pdc_power_mgmt_set_dual_role, int,
		      enum pd_dual_role_states);
DEFINE_FAKE_VALUE_FUNC(int, pdc_power_mgmt_set_trysrc, int, bool);

void helper_reset_pdc_power_mgmt_fakes(void)
{
	RESET_FAKE(pdc_power_mgmt_connector_reset);
	RESET_FAKE(pdc_power_mgmt_get_cable_prop);
	RESET_FAKE(pdc_power_mgmt_get_connector_status);
	RESET_FAKE(pdc_power_mgmt_get_info);
	RESET_FAKE(pdc_power_mgmt_get_partner_data_swap_capable);
	RESET_FAKE(pdc_power_mgmt_get_power_role);
	RESET_FAKE(pdc_power_mgmt_get_task_state_name);
	RESET_FAKE(pdc_power_mgmt_is_connected);
	RESET_FAKE(pdc_power_mgmt_pd_get_data_role);
	RESET_FAKE(pdc_power_mgmt_pd_get_polarity);
	RESET_FAKE(pdc_power_mgmt_request_data_swap);
	RESET_FAKE(pdc_power_mgmt_request_power_swap);
	RESET_FAKE(pdc_power_mgmt_reset);
	RESET_FAKE(pdc_power_mgmt_set_comms_state);
	RESET_FAKE(pdc_power_mgmt_set_dual_role);
	RESET_FAKE(pdc_power_mgmt_set_trysrc);
}

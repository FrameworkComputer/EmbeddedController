/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/intel_altmode.h"
#include "emul/emul_pdc.h"
#include "hooks.h"
#include "test/util.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(pdc_pmc_debug_api);

#define PDC_TEST_TIMEOUT 2500
#define RTS5453P_NODE DT_NODELABEL(pdc_emul1)

static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);
#define TEST_PORT 0

void pdc_pmc_setup(void)
{
	zassume(TEST_PORT < CONFIG_USB_PD_PORT_MAX_COUNT,
		"TEST_PORT is invalid");
}

ZTEST_SUITE(pdc_pmc_debug_api, NULL, pdc_pmc_setup, NULL, NULL, NULL);

ZTEST_USER(pdc_pmc_debug_api, test_data_connection)
{
	union connector_status_t connector_status;
	static union data_status_reg status;

	zassert_false(pd_is_connected(CONFIG_USB_PD_PORT_MAX_COUNT));
	zassert_false(!pdc_power_mgmt_get_pch_data_status(2, status.raw_value));
	zassert_false(!pdc_power_mgmt_get_pch_data_status(TEST_PORT, NULL));

	connector_status.connect_status = 1;
	connector_status.orientation = 1;
	/* USB=1, DP=1, USB4_gen_3=1 */
	connector_status.conn_partner_flags = 0x7;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);

	zassert_true(TEST_WAIT_FOR(EC_SUCCESS ==
					   pdc_power_mgmt_get_pch_data_status(
						   TEST_PORT, status.raw_value),
				   PDC_TEST_TIMEOUT));
	zassert_true(status.conn_ori == connector_status.orientation);
	zassert_true(status.usb2 ==
		     (connector_status.conn_partner_flags & BIT(0)));
	zassert_true(status.usb3_2 ==
		     (connector_status.conn_partner_flags & BIT(0)));
	zassert_true(status.dp ==
		     (connector_status.conn_partner_flags & BIT(1) >> 1));
	zassert_true(status.usb4 ==
		     ((connector_status.conn_partner_flags & BIT(2) >> 2) ||
		      (connector_status.conn_partner_flags & BIT(3) >> 3)));

	zassert_ok(shell_execute_cmd(get_ec_shell(), "typec 0"), NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "altmode read 0"), NULL);

	emul_pdc_disconnect(emul);
}

ZTEST_USER(pdc_pmc_debug_api, test_typec_console_cmd_invalid_arg)
{
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "typec"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "typec 2"), NULL);
}

ZTEST_USER(pdc_pmc_debug_api, test_altmode_console_cmd_invalid_arg)
{
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "altmode read"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "altmode read 2"), NULL);
}

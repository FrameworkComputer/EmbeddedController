/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "emul/emul_pdc.h"
#include "test/util.h"
#include "usb_mux.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(pdc_pmc_usb_mux);

#define PDC_TEST_TIMEOUT 2500
#define RTS5453P_NODE DT_NODELABEL(pdc_emul1)

/* TODO(b/345292003): The tests below fail with the TPS6699x emulator/driver. */
#ifndef CONFIG_TODO_B_345292002

static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);
#define TEST_PORT 0

void *pdc_pmc_usb_mux_setup(void)
{
	zassume(TEST_PORT < CONFIG_USB_PD_PORT_MAX_COUNT,
		"TEST_PORT is invalid");

	return NULL;
}

ZTEST_SUITE(pdc_pmc_usb_mux, NULL, pdc_pmc_usb_mux_setup, NULL, NULL, NULL);

ZTEST_USER(pdc_pmc_usb_mux, test_usb_mux_data_connection)
{
	union connector_status_t connector_status = { 0 };
	struct ec_params_usb_pd_mux_info param;
	struct ec_response_usb_pd_mux_info resp;

	zassert_false(pd_is_connected(CONFIG_USB_PD_PORT_MAX_COUNT));

	param.port = TEST_PORT;
	zassert_ok(ec_cmd_usb_pd_mux_info(NULL, &param, &resp));
	zassert_equal(resp.flags, USB_PD_MUX_NONE);

	connector_status.connect_status = 1;
	connector_status.orientation = 1;
	/* USB=1, DP=1, USB4_gen_3=1 */
	connector_status.conn_partner_flags = 0x7;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);

	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_PORT, -1));
	zassert_ok(ec_cmd_usb_pd_mux_info(NULL, &param, &resp));
	zassert_equal(resp.flags,
		      USB_PD_MUX_USB_ENABLED | USB_PD_MUX_POLARITY_INVERTED);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "typec 0"), NULL);

	emul_pdc_disconnect(emul);

	zassert_ok(pdc_power_mgmt_wait_for_sync(TEST_PORT, -1));
	zassert_ok(ec_cmd_usb_pd_mux_info(NULL, &param, &resp));
	zassert_equal(resp.flags, USB_PD_MUX_NONE);
}

#endif /* CONFIG_TODO_B_345292002 */

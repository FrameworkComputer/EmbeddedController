/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

#define RTS5453P_NODE DT_NODELABEL(rts5453p_emul)
#define EMUL_DATA rts5453p_emul_get_i2c_common_data(EMUL)

static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);
#define TEST_PORT 0

void pdc_power_mgmt_setup(void)
{
	zassume(TEST_PORT < CONFIG_USB_PD_PORT_MAX_COUNT,
		"TEST_PORT is invalid");

	emul_pdc_set_response_delay(emul, 0);
	emul_pdc_disconnect(emul);
	k_sleep(K_MSEC(1000));
}

ZTEST_SUITE(pdc_power_mgmt_api, NULL, pdc_power_mgmt_setup, NULL, NULL, NULL);

ZTEST_USER(pdc_power_mgmt_api, test_get_usb_pd_port_count)
{
	zassert_equal(CONFIG_USB_PD_PORT_MAX_COUNT,
		      pdc_power_mgmt_get_usb_pd_port_count());
}

ZTEST_USER(pdc_power_mgmt_api, test_is_connected)
{
	struct connector_status_t connector_status;

	zassert_false(
		pdc_power_mgmt_is_connected(CONFIG_USB_PD_PORT_MAX_COUNT));

	zassert_false(pdc_power_mgmt_is_connected(TEST_PORT));

	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_true(pdc_power_mgmt_is_connected(TEST_PORT));

	emul_pdc_disconnect(emul);
	k_sleep(K_MSEC(1000));
	zassert_false(pdc_power_mgmt_is_connected(TEST_PORT));

/* TODO: Add subcommands to support SNK Mode */
#ifdef EMUL_SNK_CMDS_SUPPORTED
	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_charger(emul, &connector_status);
	k_sleep(K_MSEC(2000));
	zassert_true(pdc_power_mgmt_is_connected(TEST_PORT));
#endif /* EMUL_SNK_CMDS_SUPPORTED */
}

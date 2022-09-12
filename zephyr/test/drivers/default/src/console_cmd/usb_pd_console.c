/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_prl_sm.h"

static void console_cmd_usb_pd_after(void *fixture)
{
	ARG_UNUSED(fixture);

	/* TODO (b/230059737) */
	test_set_chipset_to_g3();
	k_sleep(K_SECONDS(1));
	test_set_chipset_to_s0();
	k_sleep(K_SECONDS(10));

	/* Keep port used by testsuite enabled (default state) */
	pd_comm_enable(0, 1);
}

ZTEST_SUITE(console_cmd_usb_pd, drivers_predicate_post_main, NULL, NULL,
	    console_cmd_usb_pd_after, NULL);

ZTEST_USER(console_cmd_usb_pd, test_too_few_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_dump)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd dump 0");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd dump 4");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd dump -4");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd dump x");
	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_trysrc)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd trysrc 0");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd trysrc 2");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd trysrc 5");
	zassert_equal(rv, EC_ERROR_PARAM3, "Expected %d, but got %d",
		      EC_ERROR_PARAM3, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_version)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd version");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_bad_port)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 5");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 5 tx");
	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_tx)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 tx");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_charger)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 charger");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_dev)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dev");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dev 20");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dev x");
	zassert_equal(rv, EC_ERROR_PARAM3, "Expected %d, but got %d",
		      EC_ERROR_PARAM3, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_disable)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 disable");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_enable)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 enable");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_hard)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 hard");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_soft)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 soft");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_swap)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 swap");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 swap power");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 swap data");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 swap vconn");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 swap x");
	zassert_equal(rv, EC_ERROR_PARAM3, "Expected %d, but got %d",
		      EC_ERROR_PARAM3, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_dualrole)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole on");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole off");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole freeze");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole sink");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole source");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 dualrole x");
	zassert_equal(rv, EC_ERROR_PARAM4, "Expected %d, but got %d",
		      EC_ERROR_PARAM4, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_state)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 state");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_srccaps)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 srccaps");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_usb_pd, test_timer)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pd 0 timer");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

static void set_device_vdo(int port, enum tcpci_msg_type type)
{
	union tbt_mode_resp_device device_resp;
	struct pd_discovery *dev_disc;

	dev_disc = pd_get_am_discovery_and_notify_access(port, type);
	dev_disc->svid_cnt = 1;
	dev_disc->svids[0].svid = USB_VID_INTEL;
	dev_disc->svids[0].discovery = PD_DISC_COMPLETE;
	dev_disc->svids[0].mode_cnt = 1;
	device_resp.tbt_alt_mode = TBT_ALTERNATE_MODE;
	device_resp.tbt_adapter = TBT_ADAPTER_TBT3;
	device_resp.intel_spec_b0 = VENDOR_SPECIFIC_NOT_SUPPORTED;
	device_resp.vendor_spec_b0 = VENDOR_SPECIFIC_NOT_SUPPORTED;
	device_resp.vendor_spec_b1 = VENDOR_SPECIFIC_NOT_SUPPORTED;
	dev_disc->svids[0].mode_vdo[0] = device_resp.raw_value;
}

static void set_active_cable_type(int port, enum tcpci_msg_type type,
				  enum idh_ptype ptype)
{
	struct pd_discovery *dev_disc;

	dev_disc = pd_get_am_discovery_and_notify_access(port, type);
	dev_disc->identity.idh.product_type = ptype;
	prl_set_rev(port, type, PD_REV30);
}

ZTEST_USER(console_cmd_usb_pd, test_pe)
{
	int rv;

	pd_set_identity_discovery(0, TCPCI_MSG_SOP, PD_DISC_COMPLETE);

	rv = shell_execute_cmd(get_ec_shell(), "pe 0 dump");
	zassert_ok(rv, "Expected %d, but got %d", EC_SUCCESS, rv);

	set_device_vdo(0, TCPCI_MSG_SOP);
	rv = shell_execute_cmd(get_ec_shell(), "pe 0 dump");
	zassert_ok(rv, "Expected %d, but got %d", EC_SUCCESS, rv);

	/* Handle error scenarios */
	rv = shell_execute_cmd(get_ec_shell(), "pe 0");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);

	rv = shell_execute_cmd(get_ec_shell(), "pe x dump");
	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_usb_pd, test_pdcable)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pdcable 0");
	zassert_ok(rv, "Expected %d, but got %d", EC_SUCCESS, rv);

	set_device_vdo(0, TCPCI_MSG_SOP_PRIME);

	/* Set active cable type IDH_PTYPE_ACABLE */
	set_active_cable_type(0, TCPCI_MSG_SOP_PRIME, IDH_PTYPE_ACABLE);
	rv = shell_execute_cmd(get_ec_shell(), "pdcable 0");
	zassert_ok(rv, "Expected %d, but got %d", EC_SUCCESS, rv);

	/* Set active cable type IDH_PTYPE_PCABLE */
	set_active_cable_type(0, TCPCI_MSG_SOP_PRIME, IDH_PTYPE_PCABLE);
	rv = shell_execute_cmd(get_ec_shell(), "pdcable 0");
	zassert_ok(rv, "Expected %d, but got %d", EC_SUCCESS, rv);

	/* Handle error scenarios */
	rv = shell_execute_cmd(get_ec_shell(), "pdcable");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);

	rv = shell_execute_cmd(get_ec_shell(), "pdcable t");
	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

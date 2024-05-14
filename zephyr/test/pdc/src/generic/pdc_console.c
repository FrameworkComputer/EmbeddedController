/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc.h"
#include "mock_pdc_power_mgmt.h"

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0
#define SLEEP_MS 120
#define RTS5453P_NODE DT_NODELABEL(rts5453p_emul)

static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);

static void console_cmd_pdc_setup(void)
{
	struct pdc_info_t info = {
		.fw_version = 0x001a2b3c,
		.pd_version = 0xabcd,
		.pd_revision = 0x1234,
		.vid_pid = 0x12345678,
	};

	/* Set a FW version in the emulator for `test_info` */
	emul_pdc_set_info(emul, &info);

	zassume(TEST_PORT < CONFIG_USB_PD_PORT_MAX_COUNT,
		"TEST_PORT is invalid");
}

static void console_cmd_pdc_reset(void *fixture)
{
	shell_backend_dummy_clear_output(get_ec_shell());

	helper_reset_pdc_power_mgmt_fakes();
}

ZTEST_SUITE(console_cmd_pdc, NULL, console_cmd_pdc_setup, console_cmd_pdc_reset,
	    console_cmd_pdc_reset, NULL);

ZTEST_USER(console_cmd_pdc, test_no_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "pdc");
	zassert_equal(rv, SHELL_CMD_HELP_PRINTED, "Expected %d, but got %d",
		      SHELL_CMD_HELP_PRINTED, rv);
}

ZTEST_USER(console_cmd_pdc, test_pd_version)
{
	const char *outbuffer;
	size_t buffer_size;

	/* `pd version` should return 3 on PDC devices. This is used by TAST to
	 * detect PDC DUTs.
	 */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "pd version"));

	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(strstr(outbuffer, "3\r\n"));
}

/**
 * @brief Custom fake for pdc_power_mgmt_get_cable_prop that outputs some test
 *        cable property info.
 */
static int
custom_fake_pdc_power_mgmt_get_cable_prop(int port, union cable_property_t *out)
{
	zassert_not_null(out);

	*out = (union cable_property_t){
		.bm_speed_supported = 0xabcd,
		/* 50mA units. This should represent 500mA */
		.b_current_capability = 10,
		.vbus_in_cable = 1,
		.cable_type = 1,
		.directionality = 1,
		.plug_end_type = USB_TYPE_C,
		.mode_support = 1,
		.cable_pd_revision = 3,
		.latency = 0xf,
	};

	return 0;
}

ZTEST_USER(console_cmd_pdc, test_cable_prop)
{
	int rv;
	const char *outbuffer;
	size_t buffer_size;

	/* Invalid port number */
	rv = shell_execute_cmd(get_ec_shell(), "pdc cable_prop 99");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);

	/* Internal pdc_power_mgmt_get_cable_prop() failure */
	pdc_power_mgmt_get_cable_prop_fake.return_val = 1;

	rv = shell_execute_cmd(get_ec_shell(), "pdc cable_prop 0");
	zassert_equal(rv, pdc_power_mgmt_get_cable_prop_fake.return_val,
		      "Expected %d, but got %d",
		      pdc_power_mgmt_get_cable_prop_fake.return_val, rv);

	RESET_FAKE(pdc_power_mgmt_get_cable_prop);

	/* Happy case */
	pdc_power_mgmt_get_cable_prop_fake.custom_fake =
		custom_fake_pdc_power_mgmt_get_cable_prop;

	rv = shell_execute_cmd(get_ec_shell(), "pdc cable_prop 0");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	/* Sample command output:
	 *
	 * ec:> pdc cable_prop 0
	 * Port 0 GET_CABLE_PROP:
	 *    bm_speed_supported               : 0x0000
	 *    b_current_capability             : 0 mA
	 *    vbus_in_cable                    : 0
	 *    cable_type                       : 0
	 *    directionality                   : 0
	 *    plug_end_type                    : 0
	 *    mode_support                     : 0
	 *    cable_pd_revision                : 0
	 *    latency                          : 0
	 */

	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(strstr(outbuffer, "Port 0 GET_CABLE_PROP:"));
	zassert_not_null(
		strstr(outbuffer, "bm_speed_supported               : 0xabcd"));
	zassert_not_null(
		strstr(outbuffer, "b_current_capability             : 500 mA"));
	zassert_not_null(
		strstr(outbuffer, "vbus_in_cable                    : 1"));
	zassert_not_null(
		strstr(outbuffer, "cable_type                       : 1"));
	zassert_not_null(
		strstr(outbuffer, "directionality                   : 1"));
	zassert_not_null(
		strstr(outbuffer, "plug_end_type                    : 2"));
	zassert_not_null(
		strstr(outbuffer, "mode_support                     : 1"));
	zassert_not_null(
		strstr(outbuffer, "cable_pd_revision                : 3"));
	zassert_not_null(
		strstr(outbuffer, "latency                          : 15"));
}

ZTEST_USER(console_cmd_pdc, test_trysrc)
{
	int rv;
	const char *outbuffer;
	size_t buffer_size;

	/* Invalid param */
	rv = shell_execute_cmd(get_ec_shell(), "pdc trysrc enable");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);

	/* Invalid param */
	rv = shell_execute_cmd(get_ec_shell(), "pdc trysrc 2");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);

	/* Internal failure of pdc_power_mgmt_set_trysrc() */
	pdc_power_mgmt_set_trysrc_fake.return_val = 1;

	rv = shell_execute_cmd(get_ec_shell(), "pdc trysrc 0");
	zassert_equal(rv, pdc_power_mgmt_set_trysrc_fake.return_val,
		      "Expected %d, but got %d",
		      pdc_power_mgmt_set_trysrc_fake.return_val, rv);

	RESET_FAKE(pdc_power_mgmt_set_trysrc);

	/* Disable Try.SRC */
	shell_backend_dummy_clear_output(get_ec_shell());
	rv = shell_execute_cmd(get_ec_shell(), "pdc trysrc 0");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
	k_sleep(K_MSEC(SLEEP_MS));

	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(strstr(outbuffer, "Try.SRC Forced OFF"));

	/* Enable Try.SRC */
	shell_backend_dummy_clear_output(get_ec_shell());
	rv = shell_execute_cmd(get_ec_shell(), "pdc trysrc 1");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
	k_sleep(K_MSEC(SLEEP_MS));

	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(strstr(outbuffer, "Try.SRC Forced ON"));
}

ZTEST_USER(console_cmd_pdc, test_info)
{
	int rv;

	/* Bad chip number */
	rv = shell_execute_cmd(get_ec_shell(), "pdc info x");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);

	/* Bad live param (should be int) */
	rv = shell_execute_cmd(get_ec_shell(), "pdc info 0 y");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);

	/* Get chip #0 info live */
	rv = shell_execute_cmd(get_ec_shell(), "pdc info 0");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	/* Get chip #0 info cached */
	rv = shell_execute_cmd(get_ec_shell(), "pdc info 0 0");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
}

ZTEST_USER(console_cmd_pdc, test_comms_state)
{
	int rv;

	/* Invalid param */
	rv = shell_execute_cmd(get_ec_shell(), "pdc comms xyz");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);

	/* Successful suspend */
	rv = shell_execute_cmd(get_ec_shell(), "pdc comms suspend");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
	zassert_equal(1, pdc_power_mgmt_set_comms_state_fake.call_count);
	zassert_false(pdc_power_mgmt_set_comms_state_fake.arg0_history[0]);

	RESET_FAKE(pdc_power_mgmt_set_comms_state);

	/* Successful resume */
	rv = shell_execute_cmd(get_ec_shell(), "pdc comms resume");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
	zassert_equal(1, pdc_power_mgmt_set_comms_state_fake.call_count);
	zassert_true(pdc_power_mgmt_set_comms_state_fake.arg0_history[0]);

	RESET_FAKE(pdc_power_mgmt_set_comms_state);

	/* Error while setting comms state */
	pdc_power_mgmt_set_comms_state_fake.return_val = 1;

	rv = shell_execute_cmd(get_ec_shell(), "pdc comms suspend");
	zassert_equal(rv, pdc_power_mgmt_set_comms_state_fake.return_val,
		      "Expected %d, but got %d",
		      pdc_power_mgmt_set_comms_state_fake.return_val, rv);
	zassert_equal(1, pdc_power_mgmt_set_comms_state_fake.call_count);
}

ZTEST_USER(console_cmd_pdc, test_conn_reset)
{
	int rv;

	/* Invalid port number */
	rv = shell_execute_cmd(get_ec_shell(), "pdc conn_reset 99 hard");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);

	/* Invalid param */
	rv = shell_execute_cmd(get_ec_shell(), "pdc conn_reset 0 xyz");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);

	/* Successful hard reset */
	rv = shell_execute_cmd(get_ec_shell(), "pdc conn_reset 0 hard");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
	zassert_equal(1, pdc_power_mgmt_connector_reset_fake.call_count);
	zassert_equal(0, pdc_power_mgmt_connector_reset_fake.arg0_history[0]);
	zassert_equal(PD_HARD_RESET,
		      pdc_power_mgmt_connector_reset_fake.arg1_history[0]);

	RESET_FAKE(pdc_power_mgmt_connector_reset);

	/* Successful data reset */
	rv = shell_execute_cmd(get_ec_shell(), "pdc conn_reset 0 data");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
	zassert_equal(1, pdc_power_mgmt_connector_reset_fake.call_count);
	zassert_equal(0, pdc_power_mgmt_connector_reset_fake.arg0_history[0]);
	zassert_equal(PD_DATA_RESET,
		      pdc_power_mgmt_connector_reset_fake.arg1_history[0]);

	RESET_FAKE(pdc_power_mgmt_connector_reset);

	/* Error while triggering reset*/
	pdc_power_mgmt_connector_reset_fake.return_val = 1;

	rv = shell_execute_cmd(get_ec_shell(), "pdc conn_reset 0 data");
	zassert_equal(rv, pdc_power_mgmt_connector_reset_fake.return_val,
		      "Expected %d, but got %d",
		      pdc_power_mgmt_connector_reset_fake.return_val, rv);
	zassert_equal(1, pdc_power_mgmt_connector_reset_fake.call_count);
}

ZTEST_USER(console_cmd_pdc, test_reset)
{
	int rv;

	/* Invalid port number */
	rv = shell_execute_cmd(get_ec_shell(), "pdc reset 99");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);

	/* Successful reset */
	rv = shell_execute_cmd(get_ec_shell(), "pdc reset 0");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);
	zassert_equal(1, pdc_power_mgmt_reset_fake.call_count);
	zassert_equal(0, pdc_power_mgmt_reset_fake.arg0_history[0]);

	RESET_FAKE(pdc_power_mgmt_reset);

	/* Error while triggering reset */
	pdc_power_mgmt_reset_fake.return_val = 1;

	rv = shell_execute_cmd(get_ec_shell(), "pdc reset 0");
	zassert_equal(rv, pdc_power_mgmt_reset_fake.return_val,
		      "Expected %d, but got %d",
		      pdc_power_mgmt_reset_fake.return_val, rv);
	zassert_equal(1, pdc_power_mgmt_reset_fake.call_count);
}

ZTEST_USER(console_cmd_pdc, test_status)
{
	int rv;
	const char *outbuffer;
	size_t buffer_size;

	/* Invalid port number */
	rv = shell_execute_cmd(get_ec_shell(), "pdc status 99");
	zassert_equal(rv, -EINVAL, "Expected %d, but got %d", -EINVAL, rv);

	/* Successful path */
	pdc_power_mgmt_get_power_role_fake.return_val = PD_ROLE_SINK;
	pdc_power_mgmt_pd_get_data_role_fake.return_val = PD_ROLE_DFP;
	pdc_power_mgmt_pd_get_polarity_fake.return_val = POLARITY_CC2;
	pdc_power_mgmt_is_connected_fake.return_val = true;
	pdc_power_mgmt_get_task_state_name_fake.return_val = "StateName";

	rv = shell_execute_cmd(get_ec_shell(), "pdc status 0");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	zassert_equal(1, pdc_power_mgmt_get_power_role_fake.call_count);
	zassert_equal(1, pdc_power_mgmt_pd_get_data_role_fake.call_count);
	zassert_equal(1, pdc_power_mgmt_pd_get_polarity_fake.call_count);
	zassert_equal(1, pdc_power_mgmt_is_connected_fake.call_count);
	zassert_equal(1, pdc_power_mgmt_get_task_state_name_fake.call_count);

	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(strstr(
		outbuffer,
		"Port C0 CC2, Enable - Role: SNK-DFP PDC State: StateName"));

	helper_reset_pdc_power_mgmt_fakes();

	/* Successful path with different values */
	pdc_power_mgmt_get_power_role_fake.return_val = PD_ROLE_SOURCE;
	pdc_power_mgmt_pd_get_data_role_fake.return_val = PD_ROLE_UFP;
	pdc_power_mgmt_pd_get_polarity_fake.return_val = POLARITY_CC1;
	pdc_power_mgmt_is_connected_fake.return_val = false;
	pdc_power_mgmt_get_task_state_name_fake.return_val = "StateName2";

	rv = shell_execute_cmd(get_ec_shell(), "pdc status 0");
	zassert_equal(rv, EC_SUCCESS, "Expected %d, but got %d", EC_SUCCESS,
		      rv);

	zassert_equal(1, pdc_power_mgmt_get_power_role_fake.call_count);
	zassert_equal(1, pdc_power_mgmt_pd_get_data_role_fake.call_count);
	zassert_equal(1, pdc_power_mgmt_pd_get_polarity_fake.call_count);
	zassert_equal(1, pdc_power_mgmt_is_connected_fake.call_count);
	zassert_equal(1, pdc_power_mgmt_get_task_state_name_fake.call_count);

	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(strstr(
		outbuffer,
		"Port C0 CC1, Disable - Role: SRC-UFP PDC State: StateName2"));
}

/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc.h"
#include "emul/emul_smbus_ara.h"
#include "hooks.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

#define RTS5453P_NODE DT_NODELABEL(rts5453p_emul)

static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);
#define TEST_PORT 0

#define SMBUS_ARA_NODE DT_NODELABEL(smbus_ara_emul)
static const struct emul *ara = EMUL_DT_GET(SMBUS_ARA_NODE);

void pdc_power_mgmt_setup(void)
{
	zassume(TEST_PORT < CONFIG_USB_PD_PORT_MAX_COUNT,
		"TEST_PORT is invalid");
}

void pdc_power_mgmt_before(void *fixture)
{
	uint8_t addr = DT_REG_ADDR(RTS5453P_NODE);

	emul_smbus_ara_set_address(ara, addr);
	emul_pdc_set_response_delay(emul, 0);
	emul_pdc_disconnect(emul);
	k_sleep(K_MSEC(1000));
}

ZTEST_SUITE(pdc_power_mgmt_api, NULL, pdc_power_mgmt_setup,
	    pdc_power_mgmt_before, NULL, NULL);

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

	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(2000));
	zassert_true(pdc_power_mgmt_is_connected(TEST_PORT));
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_get_polarity)
{
	struct connector_status_t connector_status;

	zassert_equal(POLARITY_COUNT, pdc_power_mgmt_pd_get_polarity(
					      CONFIG_USB_PD_PORT_MAX_COUNT));

	connector_status.orientation = 1;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_equal(POLARITY_CC2, pdc_power_mgmt_pd_get_polarity(TEST_PORT));

	connector_status.orientation = 0;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_equal(POLARITY_CC1, pdc_power_mgmt_pd_get_polarity(TEST_PORT));
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_get_data_role)
{
	struct connector_status_t connector_status;

	zassert_equal(
		PD_ROLE_DISCONNECTED,
		pdc_power_mgmt_pd_get_data_role(CONFIG_USB_PD_PORT_MAX_COUNT));

	connector_status.conn_partner_type = DFP_ATTACHED;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_equal(PD_ROLE_UFP, pdc_power_mgmt_pd_get_data_role(TEST_PORT));

	connector_status.conn_partner_type = UFP_ATTACHED;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_equal(PD_ROLE_DFP, pdc_power_mgmt_pd_get_data_role(TEST_PORT));
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_get_power_role)
{
	struct connector_status_t connector_status;
	zassert_equal(PD_ROLE_SINK, pdc_power_mgmt_get_power_role(
					    CONFIG_USB_PD_PORT_MAX_COUNT));

	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_equal(PD_ROLE_SOURCE, pdc_power_mgmt_get_power_role(TEST_PORT));

	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_equal(PD_ROLE_SINK, pdc_power_mgmt_get_power_role(TEST_PORT));
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_get_task_cc_state)
{
	int i;
	struct {
		enum conn_partner_type_t in;
		enum pd_cc_states out;
	} test[] = {
		{ .in = DFP_ATTACHED, .out = PD_CC_DFP_ATTACHED },
		{ .in = UFP_ATTACHED, .out = PD_CC_UFP_ATTACHED },
		{ .in = POWERED_CABLE_NO_UFP_ATTACHED, .out = PD_CC_NONE },
		{ .in = POWERED_CABLE_UFP_ATTACHED, .out = PD_CC_UFP_ATTACHED },
		{ .in = DEBUG_ACCESSORY_ATTACHED, .out = PD_CC_UFP_DEBUG_ACC },
		{ .in = AUDIO_ADAPTER_ACCESSORY_ATTACHED,
		  .out = PD_CC_UFP_AUDIO_ACC },
	};

	zassert_equal(PD_CC_NONE, pdc_power_mgmt_get_task_cc_state(
					  CONFIG_USB_PD_PORT_MAX_COUNT));

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		struct connector_status_t connector_status;

		connector_status.conn_partner_type = test[i].in;
		emul_pdc_configure_src(emul, &connector_status);
		emul_pdc_connect_partner(emul, &connector_status);
		k_sleep(K_MSEC(1000));
		zassert_equal(test[i].out,
			      pdc_power_mgmt_get_task_cc_state(TEST_PORT));
	}
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_capable)
{
	struct connector_status_t connector_status;
	zassert_equal(false,
		      pdc_power_mgmt_pd_capable(CONFIG_USB_PD_PORT_MAX_COUNT));

	emul_pdc_disconnect(emul);
	k_sleep(K_MSEC(1000));
	zassert_false(pdc_power_mgmt_pd_capable(TEST_PORT));

	connector_status.power_operation_mode = USB_DEFAULT_OPERATION;
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_false(pdc_power_mgmt_pd_capable(TEST_PORT));

	connector_status.power_operation_mode = PD_OPERATION;
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));
	zassert_true(pdc_power_mgmt_pd_capable(TEST_PORT));
}

ZTEST_USER(pdc_power_mgmt_api, test_get_partner_usb_comm_capable)
{
	int i;
	struct connector_status_t connector_status;
	struct {
		union connector_capability_t ccap;
		bool expected;
	} test[] = {
		{ .ccap = { .raw_value = 0 }, .expected = false },
		{ .ccap = { .op_mode_usb2 = 1 }, .expected = true },
		{ .ccap = { .op_mode_usb3 = 1 }, .expected = true },
		{ .ccap = { .ext_op_mode_usb4_gen2 = 1 }, .expected = true },
		{ .ccap = { .ext_op_mode_usb4_gen3 = 1 }, .expected = true },
		{ .ccap = { .op_mode_debug_acc = 1 }, .expected = false },
		{ .ccap = { .op_mode_analog_audio = 1 }, .expected = false },
		{ .ccap = { .op_mode_rp_only = 1 }, .expected = false },
		{ .ccap = { .op_mode_rd_only = 1 }, .expected = false },
	};

	zassert_false(pdc_power_mgmt_get_partner_usb_comm_capable(
		CONFIG_USB_PD_PORT_MAX_COUNT));

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		emul_pdc_set_connector_capability(emul, &test[i].ccap);
		emul_pdc_configure_src(emul, &connector_status);
		emul_pdc_connect_partner(emul, &connector_status);
		k_sleep(K_MSEC(1000));
		zassert_equal(
			test[i].expected,
			pdc_power_mgmt_get_partner_usb_comm_capable(TEST_PORT));
		emul_pdc_disconnect(emul);
		k_sleep(K_MSEC(1000));
	}
}

ZTEST_USER(pdc_power_mgmt_api, test_get_partner_data_swap_capable)
{
	int i;
	struct connector_status_t connector_status;
	struct {
		union connector_capability_t ccap;
		bool expected;
	} test[] = {
		{ .ccap = { .raw_value = 0 }, .expected = false },
		{ .ccap = { .op_mode_drp = 1,
			    .op_mode_rp_only = 0,
			    .op_mode_rd_only = 0,
			    .swap_to_ufp = 1 },
		  .expected = true },
		{ .ccap = { .op_mode_drp = 0,
			    .op_mode_rp_only = 1,
			    .op_mode_rd_only = 0,
			    .swap_to_dfp = 1 },
		  .expected = true },
		{ .ccap = { .op_mode_drp = 0,
			    .op_mode_rp_only = 0,
			    .op_mode_rd_only = 1,
			    .swap_to_dfp = 1 },
		  .expected = true },
		{ .ccap = { .op_mode_drp = 0,
			    .op_mode_rp_only = 0,
			    .op_mode_rd_only = 1,
			    .swap_to_dfp = 0 },
		  .expected = false },
		{ .ccap = { .op_mode_drp = 0,
			    .op_mode_rp_only = 0,
			    .op_mode_rd_only = 0,
			    .swap_to_ufp = 1 },
		  .expected = false },
		{ .ccap = { .op_mode_drp = 0,
			    .op_mode_rp_only = 0,
			    .op_mode_rd_only = 0,
			    .swap_to_dfp = 1 },
		  .expected = false },
	};

	zassert_false(pdc_power_mgmt_get_partner_data_swap_capable(
		CONFIG_USB_PD_PORT_MAX_COUNT));

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		emul_pdc_set_connector_capability(emul, &test[i].ccap);
		emul_pdc_configure_src(emul, &connector_status);
		emul_pdc_connect_partner(emul, &connector_status);
		k_sleep(K_MSEC(1000));
		zassert_equal(
			test[i].expected,
			pdc_power_mgmt_get_partner_data_swap_capable(TEST_PORT),
			"[%d] expected=%d, ccap=0x%X", i, test[i].expected,
			test[i].ccap);
		emul_pdc_disconnect(emul);
		k_sleep(K_MSEC(1000));
	}
}

ZTEST_USER(pdc_power_mgmt_api, test_get_info)
{
	struct pdc_info_t in, out;
	struct connector_status_t connector_status;

	in.fw_version = 0x010203;
	in.pd_version = 0x0506;
	in.pd_revision = 0x0708;
	in.vid_pid = 0xFEEDBEEF;

	zassert_equal(-ERANGE, pdc_power_mgmt_get_info(
				       CONFIG_USB_PD_PORT_MAX_COUNT, &out));
	zassert_equal(-EINVAL, pdc_power_mgmt_get_info(TEST_PORT, NULL));

	emul_pdc_set_info(emul, &in);
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(1000));

	zassert_ok(pdc_power_mgmt_get_info(TEST_PORT, &out));
	k_sleep(K_MSEC(1000));

	zassert_equal(in.fw_version, out.fw_version, "in=0x%X, out=0x%X",
		      in.fw_version, out.fw_version);
	zassert_equal(in.pd_version, out.pd_version);
	zassert_equal(in.pd_revision, out.pd_revision);
	zassert_equal(in.vid_pid, out.vid_pid, "in=0x%X, out=0x%X", in.vid_pid,
		      out.vid_pid);

	emul_pdc_disconnect(emul);
	k_sleep(K_MSEC(1000));
}

ZTEST_USER(pdc_power_mgmt_api, test_request_power_swap)
{
	int i;
	struct setup_t {
		enum conn_partner_type_t conn_partner_type;
		emul_pdc_set_connector_status_t configure;
	};
	struct expect_t {
		union pdr_t pdr;
	};
	struct {
		struct setup_t s;
		struct expect_t e;
	} test[] = {
		{ .s = { .conn_partner_type = DFP_ATTACHED,
			 .configure = emul_pdc_configure_snk },
		  .e = { .pdr = { .swap_to_src = 1,
				  .swap_to_snk = 0,
				  .accept_pr_swap = 1 } } },
		{ .s = { .conn_partner_type = DFP_ATTACHED,
			 .configure = emul_pdc_configure_src },
		  .e = { .pdr = { .swap_to_src = 0,
				  .swap_to_snk = 1,
				  .accept_pr_swap = 1 } } },
		{ .s = { .conn_partner_type = UFP_ATTACHED,
			 .configure = emul_pdc_configure_snk },
		  .e = { .pdr = { .swap_to_src = 1,
				  .swap_to_snk = 0,
				  .accept_pr_swap = 1 } } },
		{ .s = { .conn_partner_type = UFP_ATTACHED,
			 .configure = emul_pdc_configure_src },
		  .e = { .pdr = { .swap_to_src = 0,
				  .swap_to_snk = 1,
				  .accept_pr_swap = 1 } } },
	};

	struct connector_status_t connector_status;
	union pdr_t pdr;

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		memset(&connector_status, 0, sizeof(connector_status));
		connector_status.conn_partner_type =
			test[i].s.conn_partner_type;

		test[i].s.configure(emul, &connector_status);
		emul_pdc_connect_partner(emul, &connector_status);
		k_sleep(K_MSEC(2000));

		pdc_power_mgmt_request_power_swap(TEST_PORT);
		k_sleep(K_MSEC(1000));

		emul_pdc_get_pdr(emul, &pdr);
		zassert_equal(pdr.swap_to_src, test[i].e.pdr.swap_to_src);
		zassert_equal(pdr.swap_to_snk, test[i].e.pdr.swap_to_snk);
		zassert_equal(pdr.accept_pr_swap, test[i].e.pdr.accept_pr_swap);

		emul_pdc_disconnect(emul);
		k_sleep(K_MSEC(1000));
	}
}

ZTEST_USER(pdc_power_mgmt_api, test_request_data_swap)
{
	int i;
	struct setup_t {
		enum conn_partner_type_t conn_partner_type;
		emul_pdc_set_connector_status_t configure;
	};
	struct expect_t {
		union uor_t uor;
	};
	struct {
		struct setup_t s;
		struct expect_t e;
	} test[] = {
		{ .s = { .conn_partner_type = DFP_ATTACHED,
			 .configure = emul_pdc_configure_src },
		  .e = { .uor = { .swap_to_dfp = 1,
				  .swap_to_ufp = 0,
				  .accept_dr_swap = 1 } } },
		{ .s = { .conn_partner_type = DFP_ATTACHED,
			 .configure = emul_pdc_configure_snk },
		  .e = { .uor = { .swap_to_dfp = 1,
				  .swap_to_ufp = 0,
				  .accept_dr_swap = 1 } } },
		{ .s = { .conn_partner_type = UFP_ATTACHED,
			 .configure = emul_pdc_configure_src },
		  .e = { .uor = { .swap_to_dfp = 0,
				  .swap_to_ufp = 1,
				  .accept_dr_swap = 1 } } },
		{ .s = { .conn_partner_type = UFP_ATTACHED,
			 .configure = emul_pdc_configure_snk },
		  .e = { .uor = { .swap_to_dfp = 0,
				  .swap_to_ufp = 1,
				  .accept_dr_swap = 1 } } },
	};

	struct connector_status_t connector_status;
	union uor_t uor;

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		memset(&connector_status, 0, sizeof(connector_status));
		connector_status.conn_partner_type =
			test[i].s.conn_partner_type;

		test[i].s.configure(emul, &connector_status);
		emul_pdc_connect_partner(emul, &connector_status);
		k_sleep(K_MSEC(2000));

		pdc_power_mgmt_request_data_swap(TEST_PORT);
		k_sleep(K_MSEC(1000));

		emul_pdc_get_uor(emul, &uor);
		zassert_equal(uor.swap_to_ufp, test[i].e.uor.swap_to_ufp);
		zassert_equal(uor.swap_to_dfp, test[i].e.uor.swap_to_dfp);
		zassert_equal(uor.accept_dr_swap, test[i].e.uor.accept_dr_swap);

		emul_pdc_disconnect(emul);
		k_sleep(K_MSEC(1000));
	}
}

ZTEST_USER(pdc_power_mgmt_api, test_get_partner_unconstr_power)
{
	struct connector_status_t connector_status;

	zassert_false(pdc_power_mgmt_get_partner_unconstr_power(
		CONFIG_USB_PD_PORT_MAX_COUNT));

	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(2000));

	zassert_false(pdc_power_mgmt_get_partner_unconstr_power(TEST_PORT));

	emul_pdc_disconnect(emul);
	k_sleep(K_MSEC(1000));

	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(2000));

	zassert_false(pdc_power_mgmt_get_partner_unconstr_power(TEST_PORT));
}

ZTEST_USER(pdc_power_mgmt_api, test_get_vbus_voltage)
{
	struct connector_status_t connector_status;
	uint32_t mv_units = 50;
	uint32_t expected_voltage_mv = 5000;
	uint16_t out;

	zassert_equal(0, pdc_power_mgmt_get_vbus_voltage(TEST_PORT));

	connector_status.voltage_scale = 10; /* 50 mv units*/
	connector_status.voltage_reading = expected_voltage_mv / mv_units;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(2000));

	out = pdc_power_mgmt_get_vbus_voltage(TEST_PORT);
	zassert_equal(expected_voltage_mv, out, "expected=%d, out=%d",
		      expected_voltage_mv, out);

	emul_pdc_disconnect(emul);
	k_sleep(K_MSEC(1000));
}

ZTEST_USER(pdc_power_mgmt_api, test_set_dual_role)
{
	int i;
	struct setup_t {
		enum pd_dual_role_states state;
		emul_pdc_set_connector_status_t configure;
	};
	struct expect_t {
		bool check_cc_mode;
		enum ccom_t cc_mode;
		bool check_pdr;
		union pdr_t pdr;
	};
	struct {
		struct setup_t s;
		struct expect_t e;
	} test[] = {
		{ .s = { .state = PD_DRP_TOGGLE_ON, .configure = NULL },
		  .e = { .check_cc_mode = true, .cc_mode = CCOM_DRP } },
		{ .s = { .state = PD_DRP_TOGGLE_OFF, .configure = NULL },
		  .e = { .check_cc_mode = true, .cc_mode = CCOM_RD } },
		{ .s = { .state = PD_DRP_FREEZE, .configure = NULL },
		  .e = { .check_cc_mode = true, .cc_mode = CCOM_RD } },
		{ .s = { .state = PD_DRP_FREEZE,
			 .configure = emul_pdc_configure_snk },
		  .e = { .check_cc_mode = true, .cc_mode = CCOM_RD } },
#ifdef TODO_B_323589615
		/* TODO(b/323589615) - una_policy is not applied in attached
		 * states
		 */
		{ .s = { .state = PD_DRP_FREEZE,
			 .configure = emul_pdc_configure_src },
		  .e = { .check_cc_mode = true, .cc_mode = CCOM_RP } },
#endif
		{ .s = { .state = PD_DRP_FORCE_SINK,
			 .configure = emul_pdc_configure_src },
		  .e = { .check_pdr = true,
			 .pdr = { .swap_to_src = 0, .swap_to_snk = 1 } } },
		{ .s = { .state = PD_DRP_FORCE_SOURCE,
			 .configure = emul_pdc_configure_snk },
		  .e = { .check_pdr = true,
			 .pdr = { .swap_to_src = 1, .swap_to_snk = 0 } } },
	};

	struct connector_status_t connector_status;
	enum ccom_t ccom;
	enum drp_mode_t dm;
	union pdr_t pdr;

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		memset(&connector_status, 0, sizeof(connector_status));
		if (test[i].s.configure) {
			test[i].s.configure(emul, &connector_status);
			emul_pdc_connect_partner(emul, &connector_status);
			k_sleep(K_MSEC(2000));
		}

		pdc_power_mgmt_set_dual_role(TEST_PORT, test[i].s.state);
		k_sleep(K_MSEC(4000));

		if (test[i].e.check_cc_mode) {
			emul_pdc_get_ccom(emul, &ccom, &dm);
			zassert_equal(test[i].e.cc_mode, ccom,
				      "[%d] expected=%d, received=%d", i,
				      test[i].e.cc_mode, ccom);
		}
		if (test[i].e.check_pdr) {
			emul_pdc_get_pdr(emul, &pdr);
			zassert_equal(test[i].e.pdr.swap_to_snk,
				      pdr.swap_to_snk);
			zassert_equal(test[i].e.pdr.swap_to_src,
				      pdr.swap_to_src);
		}
		emul_pdc_disconnect(emul);
		k_sleep(K_MSEC(2000));
	}
}

ZTEST_USER(pdc_power_mgmt_api, test_chipset_suspend)
{
	struct connector_status_t connector_status;
	enum ccom_t ccom;
	enum drp_mode_t dm;

	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(2000));

	hook_notify(HOOK_CHIPSET_SUSPEND);
	k_sleep(K_MSEC(2000));

	emul_pdc_disconnect(emul);
	k_sleep(K_MSEC(2000));

	emul_pdc_get_ccom(emul, &ccom, &dm);
	zassert_equal(CCOM_RD, ccom);
}

ZTEST_USER(pdc_power_mgmt_api, test_chipset_resume)
{
	struct connector_status_t connector_status;
	enum ccom_t ccom;
	enum drp_mode_t dm;

	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	k_sleep(K_MSEC(2000));

	hook_notify(HOOK_CHIPSET_RESUME);
	k_sleep(K_MSEC(2000));

	emul_pdc_get_ccom(emul, &ccom, &dm);
	zassert_equal(CCOM_DRP, ccom);
}

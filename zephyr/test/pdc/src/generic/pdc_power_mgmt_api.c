/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc.h"
#include "emul/emul_smbus_ara.h"
#include "hooks.h"
#include "test/util.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(pdc_power_mgmt_api);

#define PDC_TEST_TIMEOUT 2000
#define RTS5453P_NODE DT_NODELABEL(rts5453p_emul)

static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);
#define TEST_PORT 0

#define SMBUS_ARA_NODE DT_NODELABEL(smbus_ara_emul)
static const struct emul *ara = EMUL_DT_GET(SMBUS_ARA_NODE);

bool pdc_power_mgmt_test_wait_unattached(void);
bool pdc_rts54xx_test_idle_wait(void);

bool test_pdc_power_mgmt_is_snk_typec_attached_run(int port);
bool test_pdc_power_mgmt_is_src_typec_attached_run(int port);

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
	TEST_WORKING_DELAY(PDC_TEST_TIMEOUT);

	zassert_true(pdc_power_mgmt_test_wait_unattached());
	zassert_true(pdc_rts54xx_test_idle_wait());
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
	union connector_status_t connector_status;

	zassert_false(pd_is_connected(CONFIG_USB_PD_PORT_MAX_COUNT));

	zassert_false(pd_is_connected(TEST_PORT));

	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(
		TEST_WAIT_FOR(pd_is_connected(TEST_PORT), PDC_TEST_TIMEOUT));

	emul_pdc_disconnect(emul);
	zassert_true(
		TEST_WAIT_FOR(!pd_is_connected(TEST_PORT), PDC_TEST_TIMEOUT));

	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(
		TEST_WAIT_FOR(pd_is_connected(TEST_PORT), PDC_TEST_TIMEOUT));
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_get_polarity)
{
	union connector_status_t connector_status;

	zassert_false(
		pdc_power_mgmt_is_connected(CONFIG_USB_PD_PORT_MAX_COUNT));

	connector_status.orientation = 1;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(TEST_WAIT_FOR(POLARITY_CC2 == pd_get_polarity(TEST_PORT),
				   PDC_TEST_TIMEOUT));

	connector_status.orientation = 0;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(TEST_WAIT_FOR(POLARITY_CC1 == pd_get_polarity(TEST_PORT),
				   PDC_TEST_TIMEOUT));
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_get_data_role)
{
	union connector_status_t connector_status;

	zassert_equal(PD_ROLE_DISCONNECTED,
		      pd_get_data_role(CONFIG_USB_PD_PORT_MAX_COUNT));

	connector_status.conn_partner_type = DFP_ATTACHED;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(TEST_WAIT_FOR(PD_ROLE_UFP == pd_get_data_role(TEST_PORT),
				   PDC_TEST_TIMEOUT));

	connector_status.conn_partner_type = UFP_ATTACHED;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(TEST_WAIT_FOR(PD_ROLE_DFP == pd_get_data_role(TEST_PORT),
				   PDC_TEST_TIMEOUT));
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_get_power_role)
{
	union connector_status_t connector_status;
	zassert_equal(PD_ROLE_SINK,
		      pd_get_power_role(CONFIG_USB_PD_PORT_MAX_COUNT));

	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(
		TEST_WAIT_FOR(PD_ROLE_SOURCE == pd_get_power_role(TEST_PORT),
			      PDC_TEST_TIMEOUT));

	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(TEST_WAIT_FOR(PD_ROLE_SINK == pd_get_power_role(TEST_PORT),
				   PDC_TEST_TIMEOUT));
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

	zassert_equal(PD_CC_NONE,
		      pd_get_task_cc_state(CONFIG_USB_PD_PORT_MAX_COUNT));

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		union connector_status_t connector_status;

		connector_status.conn_partner_type = test[i].in;
		emul_pdc_configure_src(emul, &connector_status);
		emul_pdc_connect_partner(emul, &connector_status);
		zassert_true(TEST_WAIT_FOR(
			test[i].out == pd_get_task_cc_state(TEST_PORT),
			PDC_TEST_TIMEOUT));
	}
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_capable)
{
	union connector_status_t connector_status;
	zassert_equal(false, pd_capable(CONFIG_USB_PD_PORT_MAX_COUNT));

	emul_pdc_disconnect(emul);
	zassert_false(TEST_WAIT_FOR(pd_capable(TEST_PORT), PDC_TEST_TIMEOUT));

	connector_status.power_operation_mode = USB_DEFAULT_OPERATION;
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_false(TEST_WAIT_FOR(pd_capable(TEST_PORT), PDC_TEST_TIMEOUT));

	connector_status.power_operation_mode = PD_OPERATION;
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(TEST_WAIT_FOR(pd_capable(TEST_PORT), PDC_TEST_TIMEOUT));
}

K_THREAD_STACK_DEFINE(test_toggle_stack, 256);
static bool test_toggle_done;
static union connector_status_t test_toggle_status;

static void test_thread_toggle(void *a, void *b, void *c)
{
	union conn_status_change_bits_t status_change_bits;

	memset(&status_change_bits, 0, sizeof(union conn_status_change_bits_t));
	test_toggle_status.raw_conn_status_change_bits =
		status_change_bits.raw_value;

	LOG_INF("Emul PDC disconnect partner");
	emul_pdc_connect_partner(emul, &test_toggle_status);

	while (!test_toggle_done) {
		k_msleep(50);

		/* Toggle attention on each pass to keep the PDC busy */
		status_change_bits.attention ^= status_change_bits.attention;
		test_toggle_status.raw_conn_status_change_bits =
			status_change_bits.raw_value;

		LOG_INF("Emul PDC toggle attention");
		emul_pdc_connect_partner(emul, &test_toggle_status);
	}
}

static k_tid_t start_toggle_thread(struct k_thread *thread,
				   union connector_status_t *connector_status)
{
	memcpy(&test_toggle_status, connector_status,
	       sizeof(union connector_status_t));
	test_toggle_done = false;

	return k_thread_create(thread, test_toggle_stack,
			       K_THREAD_STACK_SIZEOF(test_toggle_stack),
			       test_thread_toggle, NULL, NULL, NULL, -1, 0,
			       K_NO_WAIT);
}

static int join_toggle_thread(k_tid_t thread)
{
	test_toggle_done = true;
	return k_thread_join(thread, K_MSEC(100));
}

static void run_toggle_test(union connector_status_t *connector_status)
{
	struct pdc_info_t pdc_info;
	struct k_thread test_thread_data;
	int ret;

	LOG_INF("Emul PDC disconnect partner");
	emul_pdc_disconnect(emul);
	zassert_false(TEST_WAIT_FOR(pd_capable(TEST_PORT), PDC_TEST_TIMEOUT));

	/*
	 * Create a new thread to toggle keep the PDC busy with interrupts.
	 * Thread priority set to cooperative to ensure it preempts the PDC
	 * subsystem.
	 */
	memset(connector_status, 0, sizeof(union connector_status_t));
	k_tid_t test_thread =
		start_toggle_thread(&test_thread_data, connector_status);

	/* Allow the test thread some cycles to run. */
	k_msleep(100);

	LOG_INF("Sending GET INFO");
	ret = pdc_power_mgmt_get_info(TEST_PORT, &pdc_info, true);
	zassert_equal(-EBUSY, ret,
		      "pdc_power_mgmt_get_info() returned %d (expected %d)",
		      ret, -EBUSY);

	/* Allow the test thread to exit. */
	zassert_ok(join_toggle_thread(test_thread));

	/* All the PDC subsystem to settle. */
	k_msleep(250);

	/* Public API command should now succeed. */
	ret = pdc_power_mgmt_get_info(TEST_PORT, &pdc_info, true);
	zassert_false(ret, "pdc_power_mgmt_get_info() failed (%d)", ret);
}

/* Verify that public commands complete when a non PD partner is connected */
ZTEST_USER(pdc_power_mgmt_api, test_non_pd_snk_public_cmd)
{
	union connector_status_t connector_status;

	memset(&connector_status, 0, sizeof(union connector_status_t));
	connector_status.power_operation_mode = USB_TC_CURRENT_5A;
	connector_status.power_direction = 0;

	run_toggle_test(&connector_status);
}

ZTEST_USER(pdc_power_mgmt_api, test_non_pd_src_public_cmd)
{
	union connector_status_t connector_status;

	memset(&connector_status, 0, sizeof(union connector_status_t));
	connector_status.power_operation_mode = USB_TC_CURRENT_5A;
	connector_status.power_direction = 1;

	run_toggle_test(&connector_status);
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_snk_public_cmd)
{
	union connector_status_t connector_status;

	memset(&connector_status, 0, sizeof(union connector_status_t));
	connector_status.power_operation_mode = PD_OPERATION;
	connector_status.power_direction = 0;

	run_toggle_test(&connector_status);
}

ZTEST_USER(pdc_power_mgmt_api, test_pd_src_public_cmd)
{
	union connector_status_t connector_status;

	memset(&connector_status, 0, sizeof(union connector_status_t));
	connector_status.power_operation_mode = PD_OPERATION;
	connector_status.power_direction = 1;

	run_toggle_test(&connector_status);
}

ZTEST_USER(pdc_power_mgmt_api, test_unattached_public_cmd)
{
	union connector_status_t connector_status;

	memset(&connector_status, 0, sizeof(union connector_status_t));

	run_toggle_test(&connector_status);
}

ZTEST_USER(pdc_power_mgmt_api, test_get_partner_usb_comm_capable)
{
	int i;
	union connector_status_t connector_status;
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

	zassert_false(
		pd_get_partner_usb_comm_capable(CONFIG_USB_PD_PORT_MAX_COUNT));

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		emul_pdc_set_connector_capability(emul, &test[i].ccap);
		emul_pdc_configure_src(emul, &connector_status);
		emul_pdc_connect_partner(emul, &connector_status);
		zassert_true(TEST_WAIT_FOR(
			test[i].expected ==
				pd_get_partner_usb_comm_capable(TEST_PORT),
			PDC_TEST_TIMEOUT));

		emul_pdc_disconnect(emul);
		zassert_true(TEST_WAIT_FOR(!pd_is_connected(TEST_PORT),
					   PDC_TEST_TIMEOUT));
	}
}

ZTEST_USER(pdc_power_mgmt_api, test_get_partner_data_swap_capable)
{
	int i;
	union connector_status_t connector_status;
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
	uint32_t timeout = k_ms_to_cyc_ceil32(PDC_TEST_TIMEOUT);
	uint32_t start;

	zassert_false(
		pd_get_partner_data_swap_capable(CONFIG_USB_PD_PORT_MAX_COUNT));

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		emul_pdc_set_connector_capability(emul, &test[i].ccap);
		emul_pdc_configure_src(emul, &connector_status);
		emul_pdc_connect_partner(emul, &connector_status);

		start = k_cycle_get_32();
		while (k_cycle_get_32() - start < timeout) {
			k_msleep(TEST_WAIT_FOR_INTERVAL_MS);

			if (test[i].expected !=
			    pd_get_partner_data_swap_capable(TEST_PORT))
				continue;

			break;
		}

		zassert_equal(test[i].expected,
			      pd_get_partner_data_swap_capable(TEST_PORT),
			      "[%d] expected=%d, ccap=0x%X", i,
			      test[i].expected, test[i].ccap);

		emul_pdc_disconnect(emul);
		zassert_true(TEST_WAIT_FOR(!pd_is_connected(TEST_PORT),
					   PDC_TEST_TIMEOUT));
	}
}

ZTEST_USER(pdc_power_mgmt_api, test_get_info)
{
	struct pdc_info_t in1 = {
		.fw_version = 0x001a2b3c,
		.pd_version = 0xabcd,
		.pd_revision = 0x1234,
		.vid_pid = 0x12345678,
	};
	struct pdc_info_t in2 = {
		.fw_version = 0x002a3b4c,
		.pd_version = 0xef01,
		.pd_revision = 0x5678,
		.vid_pid = 0x9abcdef0,
	};
	struct pdc_info_t out = { 0 };
	union connector_status_t connector_status;

	zassert_equal(-ERANGE,
		      pdc_power_mgmt_get_info(CONFIG_USB_PD_PORT_MAX_COUNT,
					      &out, true));
	zassert_equal(-EINVAL, pdc_power_mgmt_get_info(TEST_PORT, NULL, true));

	emul_pdc_set_info(emul, &in1);
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(
		TEST_WAIT_FOR(pd_is_connected(TEST_PORT), PDC_TEST_TIMEOUT));

	zassert_ok(pdc_power_mgmt_get_info(TEST_PORT, &out, true));
	zassert_equal(in1.fw_version, out.fw_version, "in=0x%X, out=0x%X",
		      in1.fw_version, out.fw_version);
	zassert_equal(in1.pd_version, out.pd_version);
	zassert_equal(in1.pd_revision, out.pd_revision);
	zassert_equal(in1.vid_pid, out.vid_pid, "in=0x%X, out=0x%X",
		      in1.vid_pid, out.vid_pid);

	/* Repeat but non-live. The cached info should match the original
	 * read instead of `in2`.
	 */
	emul_pdc_set_info(emul, &in2);
	zassert_ok(pdc_power_mgmt_get_info(TEST_PORT, &out, false));
	zassert_equal(in1.fw_version, out.fw_version, "in=0x%X, out=0x%X",
		      in1.fw_version, out.fw_version);
	zassert_equal(in1.pd_version, out.pd_version);
	zassert_equal(in1.pd_revision, out.pd_revision);
	zassert_equal(in1.vid_pid, out.vid_pid, "in=0x%X, out=0x%X",
		      in1.vid_pid, out.vid_pid);

	/* Live read again. This time we should get `in2`. */
	zassert_ok(pdc_power_mgmt_get_info(TEST_PORT, &out, true));
	zassert_equal(in2.fw_version, out.fw_version, "in=0x%X, out=0x%X",
		      in2.fw_version, out.fw_version);
	zassert_equal(in2.pd_version, out.pd_version);
	zassert_equal(in2.pd_revision, out.pd_revision);
	zassert_equal(in2.vid_pid, out.vid_pid, "in=0x%X, out=0x%X",
		      in2.vid_pid, out.vid_pid);

	emul_pdc_disconnect(emul);
	zassert_true(
		TEST_WAIT_FOR(!pd_is_connected(TEST_PORT), PDC_TEST_TIMEOUT));
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

	union connector_status_t connector_status;
	union pdr_t pdr;
	uint32_t timeout = k_ms_to_cyc_ceil32(PDC_TEST_TIMEOUT);
	uint32_t start;

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		memset(&connector_status, 0, sizeof(connector_status));
		connector_status.conn_partner_type =
			test[i].s.conn_partner_type;

		test[i].s.configure(emul, &connector_status);
		emul_pdc_connect_partner(emul, &connector_status);
		zassert_true(TEST_WAIT_FOR(pd_is_connected(TEST_PORT),
					   PDC_TEST_TIMEOUT));

		pd_request_power_swap(TEST_PORT);

		start = k_cycle_get_32();
		while (k_cycle_get_32() - start < timeout) {
			k_msleep(TEST_WAIT_FOR_INTERVAL_MS);

			emul_pdc_get_pdr(emul, &pdr);

			if (pdr.swap_to_src != test[i].e.pdr.swap_to_src)
				continue;

			if (pdr.swap_to_snk != test[i].e.pdr.swap_to_snk)
				continue;

			if (pdr.accept_pr_swap != test[i].e.pdr.accept_pr_swap)
				continue;

			break;
		}

		zassert_equal(pdr.swap_to_src, test[i].e.pdr.swap_to_src);
		zassert_equal(pdr.swap_to_snk, test[i].e.pdr.swap_to_snk);
		zassert_equal(pdr.accept_pr_swap, test[i].e.pdr.accept_pr_swap);

		emul_pdc_disconnect(emul);
		zassert_true(TEST_WAIT_FOR(!pd_is_connected(TEST_PORT),
					   PDC_TEST_TIMEOUT));
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

	union connector_status_t connector_status;
	union uor_t uor;
	uint32_t timeout = k_ms_to_cyc_ceil32(PDC_TEST_TIMEOUT);
	uint32_t start;

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		memset(&connector_status, 0, sizeof(connector_status));
		connector_status.conn_partner_type =
			test[i].s.conn_partner_type;

		test[i].s.configure(emul, &connector_status);
		emul_pdc_connect_partner(emul, &connector_status);
		zassert_true(TEST_WAIT_FOR(pd_is_connected(TEST_PORT),
					   PDC_TEST_TIMEOUT));

		pd_request_data_swap(TEST_PORT);
		start = k_cycle_get_32();
		while (k_cycle_get_32() - start < timeout) {
			k_msleep(TEST_WAIT_FOR_INTERVAL_MS);
			emul_pdc_get_uor(emul, &uor);

			if (uor.swap_to_ufp != test[i].e.uor.swap_to_ufp)
				continue;

			if (uor.swap_to_dfp != test[i].e.uor.swap_to_dfp)
				continue;

			if (uor.accept_dr_swap != test[i].e.uor.accept_dr_swap)
				continue;

			break;
		}

		emul_pdc_get_uor(emul, &uor);
		zassert_equal(uor.swap_to_ufp, test[i].e.uor.swap_to_ufp);
		zassert_equal(uor.swap_to_dfp, test[i].e.uor.swap_to_dfp);
		zassert_equal(uor.accept_dr_swap, test[i].e.uor.accept_dr_swap);

		emul_pdc_disconnect(emul);
		zassert_true(TEST_WAIT_FOR(!pd_is_connected(TEST_PORT),
					   PDC_TEST_TIMEOUT));
	}
}

ZTEST_USER(pdc_power_mgmt_api, test_get_partner_unconstr_power)
{
	union connector_status_t connector_status;

	zassert_false(
		pd_get_partner_unconstr_power(CONFIG_USB_PD_PORT_MAX_COUNT));

	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);

	zassert_false(TEST_WAIT_FOR(pd_get_partner_unconstr_power(TEST_PORT),
				    PDC_TEST_TIMEOUT));

	emul_pdc_disconnect(emul);
	zassert_true(
		TEST_WAIT_FOR(!pd_is_connected(TEST_PORT), PDC_TEST_TIMEOUT));

	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);

	zassert_false(TEST_WAIT_FOR(pd_get_partner_unconstr_power(TEST_PORT),
				    PDC_TEST_TIMEOUT));
}

ZTEST_USER(pdc_power_mgmt_api, test_get_vbus_voltage)
{
	union connector_status_t connector_status;
	uint32_t mv_units = 50;
	const uint32_t expected_voltage_mv = 5000;
	uint16_t out;
	uint32_t timeout = k_ms_to_cyc_ceil32(PDC_TEST_TIMEOUT);
	uint32_t start;

	zassert_equal(0, pdc_power_mgmt_get_vbus_voltage(TEST_PORT));

	connector_status.voltage_scale = 10; /* 50 mv units*/
	connector_status.voltage_reading = expected_voltage_mv / mv_units;
	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);

	start = k_cycle_get_32();
	while (k_cycle_get_32() - start < timeout) {
		k_msleep(TEST_WAIT_FOR_INTERVAL_MS);
		out = pdc_power_mgmt_get_vbus_voltage(TEST_PORT);
		if (out != expected_voltage_mv)
			continue;

		break;
	}

	zassert_equal(expected_voltage_mv, out, "expected=%d, out=%d",
		      expected_voltage_mv, out);

	emul_pdc_disconnect(emul);
	zassert_true(
		TEST_WAIT_FOR(!pd_is_connected(TEST_PORT), PDC_TEST_TIMEOUT));
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

	union connector_status_t connector_status;
	enum ccom_t ccom;
	union pdr_t pdr;
	uint32_t timeout = k_ms_to_cyc_ceil32(4000);
	uint32_t start;

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		memset(&connector_status, 0, sizeof(connector_status));
		if (test[i].s.configure) {
			test[i].s.configure(emul, &connector_status);
			emul_pdc_connect_partner(emul, &connector_status);
			zassert_true(TEST_WAIT_FOR(pd_is_connected(TEST_PORT),
						   PDC_TEST_TIMEOUT));
		}

		pd_set_dual_role(TEST_PORT, test[i].s.state);
		start = k_cycle_get_32();

		while (k_cycle_get_32() - start < timeout) {
			k_msleep(TEST_WAIT_FOR_INTERVAL_MS);

			if (test[i].e.check_cc_mode) {
				emul_pdc_get_ccom(emul, &ccom);

				if (test[i].e.cc_mode != ccom)
					continue;
			}

			if (test[i].e.check_pdr) {
				emul_pdc_get_pdr(emul, &pdr);

				if (test[i].e.pdr.swap_to_snk !=
				    pdr.swap_to_snk)
					continue;
			}

			break;
		}

		if (test[i].e.check_cc_mode) {
			zassert_equal(test[i].e.cc_mode, ccom,
				      "[%d] expected=%d, received=%d", i,
				      test[i].e.cc_mode, ccom);
		}
		if (test[i].e.check_pdr) {
			zassert_equal(test[i].e.pdr.swap_to_snk,
				      pdr.swap_to_snk);
			zassert_equal(test[i].e.pdr.swap_to_src,
				      pdr.swap_to_src);
		}
		emul_pdc_disconnect(emul);
		zassert_true(TEST_WAIT_FOR(!pd_is_connected(TEST_PORT),
					   PDC_TEST_TIMEOUT));
	}
}

ZTEST_USER(pdc_power_mgmt_api, test_chipset_suspend)
{
	union connector_status_t connector_status;
	enum ccom_t ccom;
	uint32_t timeout = k_ms_to_cyc_ceil32(PDC_TEST_TIMEOUT);
	uint32_t start;

	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(
		TEST_WAIT_FOR(pd_is_connected(TEST_PORT), PDC_TEST_TIMEOUT));

	hook_notify(HOOK_CHIPSET_SUSPEND);
	TEST_WORKING_DELAY(PDC_TEST_TIMEOUT);

	emul_pdc_disconnect(emul);

	start = k_cycle_get_32();
	while (k_cycle_get_32() - start < timeout) {
		k_msleep(TEST_WAIT_FOR_INTERVAL_MS);
		emul_pdc_get_ccom(emul, &ccom);

		if (ccom != CCOM_RD)
			continue;

		break;
	}

	zassert_equal(CCOM_RD, ccom);
}

ZTEST_USER(pdc_power_mgmt_api, test_chipset_resume)
{
	union connector_status_t connector_status;
	enum ccom_t ccom;

	emul_pdc_configure_snk(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(
		TEST_WAIT_FOR(pd_is_connected(TEST_PORT), PDC_TEST_TIMEOUT));

	hook_notify(HOOK_CHIPSET_RESUME);
	TEST_WORKING_DELAY(PDC_TEST_TIMEOUT);

	emul_pdc_get_ccom(emul, &ccom);
	zassert_equal(CCOM_DRP, ccom);
}

ZTEST_USER(pdc_power_mgmt_api, test_chipset_startup)
{
	union connector_status_t connector_status;
	enum ccom_t ccom;
	uint32_t timeout = k_ms_to_cyc_ceil32(PDC_TEST_TIMEOUT);
	uint32_t start;

	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(
		TEST_WAIT_FOR(pd_is_connected(TEST_PORT), PDC_TEST_TIMEOUT));

	hook_notify(HOOK_CHIPSET_STARTUP);
	TEST_WORKING_DELAY(PDC_TEST_TIMEOUT);

	emul_pdc_disconnect(emul);

	start = k_cycle_get_32();
	while (k_cycle_get_32() - start < timeout) {
		k_msleep(TEST_WAIT_FOR_INTERVAL_MS);
		emul_pdc_get_ccom(emul, &ccom);

		if (ccom != CCOM_RD)
			continue;

		break;
	}

	zassert_equal(CCOM_RD, ccom);
}

ZTEST_USER(pdc_power_mgmt_api, test_chipset_shutdown)
{
	union connector_status_t connector_status;
	union pdr_t pdr;
	uint32_t timeout = k_ms_to_cyc_ceil32(PDC_TEST_TIMEOUT);
	uint32_t start;

	emul_pdc_configure_src(emul, &connector_status);
	emul_pdc_connect_partner(emul, &connector_status);
	zassert_true(
		TEST_WAIT_FOR(pd_is_connected(TEST_PORT), PDC_TEST_TIMEOUT));

	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	TEST_WORKING_DELAY(PDC_TEST_TIMEOUT);

	emul_pdc_disconnect(emul);

	start = k_cycle_get_32();
	while (k_cycle_get_32() - start < timeout) {
		k_msleep(TEST_WAIT_FOR_INTERVAL_MS);
		emul_pdc_get_pdr(emul, &pdr);

		if (pdr.swap_to_snk != 1)
			continue;

		if (pdr.swap_to_src != 0)
			continue;

		break;
	}

	zassert_equal(1, pdr.swap_to_snk);
	zassert_equal(0, pdr.swap_to_src);
}

static bool wait_state_name(int port, const char *target_name)
{
	const uint32_t timeout = k_ms_to_cyc_ceil32(PDC_TEST_TIMEOUT);
	uint32_t start = k_cycle_get_32();
	const char *state_name = pd_get_task_state_name(TEST_PORT);

	while (k_cycle_get_32() - start < timeout) {
		k_msleep(TEST_WAIT_FOR_INTERVAL_MS);
		state_name = pd_get_task_state_name(TEST_PORT);

		if (strcmp(state_name, target_name) != 0)
			continue;

		return true;
	}

	return false;
}

ZTEST_USER(pdc_power_mgmt_api, test_get_task_state_name_typec_snk_attached)
{
	union connector_status_t connector_status;

	zassert_true(wait_state_name(TEST_PORT, "Unattached"));

	memset(&connector_status, 0, sizeof(connector_status));
	emul_pdc_configure_snk(emul, &connector_status);
	connector_status.power_operation_mode = USB_DEFAULT_OPERATION;
	emul_pdc_connect_partner(emul, &connector_status);

	zassert_true(wait_state_name(TEST_PORT, "TypeCSnkAttached"));

	/* Allow for debouncing time. */
	TEST_WORKING_DELAY(PD_T_SINK_WAIT_CAP);
	TEST_WORKING_DELAY(PDC_TEST_TIMEOUT);
	zassert_true(test_pdc_power_mgmt_is_snk_typec_attached_run(TEST_PORT));
}

ZTEST_USER(pdc_power_mgmt_api, test_get_task_state_name_typec_src_attached)
{
	union connector_status_t connector_status;

	zassert_true(wait_state_name(TEST_PORT, "Unattached"));

	memset(&connector_status, 0, sizeof(connector_status));
	emul_pdc_configure_src(emul, &connector_status);
	connector_status.power_operation_mode = USB_DEFAULT_OPERATION;
	emul_pdc_connect_partner(emul, &connector_status);

	zassert_true(wait_state_name(TEST_PORT, "TypeCSrcAttached"));

	/* Allow for debouncing time. */
	TEST_WORKING_DELAY(PD_T_SINK_WAIT_CAP);
	TEST_WORKING_DELAY(PDC_TEST_TIMEOUT);
	zassert_true(test_pdc_power_mgmt_is_src_typec_attached_run(TEST_PORT));
}

ZTEST_USER(pdc_power_mgmt_api, test_get_task_state_name_attached_snk)
{
	union connector_status_t connector_status;

	zassert_true(wait_state_name(TEST_PORT, "Unattached"));

	memset(&connector_status, 0, sizeof(connector_status));
	emul_pdc_configure_snk(emul, &connector_status);
	connector_status.power_operation_mode = PD_OPERATION;
	emul_pdc_connect_partner(emul, &connector_status);

	zassert_true(wait_state_name(TEST_PORT, "Attached.SNK"));
}

ZTEST_USER(pdc_power_mgmt_api, test_get_task_state_name_attached_src)
{
	union connector_status_t connector_status;

	zassert_true(wait_state_name(TEST_PORT, "Unattached"));

	memset(&connector_status, 0, sizeof(connector_status));
	emul_pdc_configure_src(emul, &connector_status);
	connector_status.power_operation_mode = PD_OPERATION;
	emul_pdc_connect_partner(emul, &connector_status);

	zassert_true(wait_state_name(TEST_PORT, "Attached.SRC"));
}

ZTEST_USER(pdc_power_mgmt_api, test_get_connector_status)
{
	union connector_status_t in, out;
	union conn_status_change_bits_t in_conn_status_change_bits;
	union conn_status_change_bits_t out_conn_status_change_bits;

	zassert_equal(-ERANGE, pdc_power_mgmt_get_connector_status(
				       CONFIG_USB_PD_PORT_MAX_COUNT, &out));
	zassert_equal(-EINVAL,
		      pdc_power_mgmt_get_connector_status(TEST_PORT, NULL));

	in_conn_status_change_bits.external_supply_change = 1;
	in_conn_status_change_bits.connector_partner = 1;
	in_conn_status_change_bits.connect_change = 1;
	in.raw_conn_status_change_bits = in_conn_status_change_bits.raw_value;

	in.conn_partner_flags = 1;
	in.conn_partner_type = UFP_ATTACHED;
	in.rdo = 0x01234567;

	emul_pdc_configure_snk(emul, &in);
	emul_pdc_connect_partner(emul, &in);
	zassert_true(TEST_WAIT_FOR(pdc_power_mgmt_is_connected(TEST_PORT),
				   PDC_TEST_TIMEOUT));

	zassert_ok(pdc_power_mgmt_get_connector_status(TEST_PORT, &out));

	out_conn_status_change_bits.raw_value = out.raw_conn_status_change_bits;

	zassert_equal(out_conn_status_change_bits.external_supply_change,
		      in_conn_status_change_bits.external_supply_change);
	zassert_equal(out_conn_status_change_bits.connector_partner,
		      in_conn_status_change_bits.connector_partner);
	zassert_equal(out_conn_status_change_bits.connect_change,
		      in_conn_status_change_bits.connect_change);
	zassert_equal(out.power_operation_mode, in.power_operation_mode);
	zassert_equal(out.connect_status, in.connect_status);
	zassert_equal(out.power_direction, in.power_direction);
	zassert_equal(out.conn_partner_flags, in.conn_partner_flags,
		      "out=0x%X != in=0x%X", out.conn_partner_flags,
		      in.conn_partner_flags);
	zassert_equal(out.conn_partner_type, in.conn_partner_type);
	zassert_equal(out.rdo, in.rdo);

	emul_pdc_disconnect(emul);
	zassert_true(TEST_WAIT_FOR(!pdc_power_mgmt_is_connected(TEST_PORT),
				   PDC_TEST_TIMEOUT));
}

ZTEST_USER(pdc_power_mgmt_api, test_get_cable_prop)
{
	union cable_property_t in, out, exp;
	union connector_status_t in_conn_status, out_conn_status;
	union conn_status_change_bits_t in_conn_status_change_bits;

	zassert_equal(-ERANGE, pdc_power_mgmt_get_cable_prop(
				       CONFIG_USB_PD_PORT_MAX_COUNT, &out));
	zassert_equal(-EINVAL, pdc_power_mgmt_get_cable_prop(TEST_PORT, NULL));

	in.raw_value[0] = 0x1a2b3c4d;
	in.raw_value[1] = 0x5a6b7c8d;
	emul_pdc_set_cable_property(emul, in);

	in_conn_status_change_bits.external_supply_change = 1;
	in_conn_status_change_bits.connector_partner = 1;
	in_conn_status_change_bits.connect_change = 1;
	in_conn_status.raw_conn_status_change_bits =
		in_conn_status_change_bits.raw_value;

	in_conn_status.conn_partner_flags = 1;
	in_conn_status.conn_partner_type = UFP_ATTACHED;
	in_conn_status.rdo = 0x01234567;

	emul_pdc_configure_snk(emul, &in_conn_status);
	emul_pdc_connect_partner(emul, &in_conn_status);
	zassert_true(TEST_WAIT_FOR(pdc_power_mgmt_is_connected(TEST_PORT),
				   PDC_TEST_TIMEOUT));

	zassert_ok(pdc_power_mgmt_get_connector_status(TEST_PORT,
						       &out_conn_status));

	zassert_ok(pdc_power_mgmt_get_cable_prop(TEST_PORT, &out));

	/*
	 * The RTS54xx only returns 5 bytes of cable property.
	 */
	zassert_mem_equal(in.raw_value, out.raw_value, 5,
			  "Returned cable property did not match input "
			  "in 0x%08X:%08X != out 0x%08X:%08X",
			  in.raw_value[0], in.raw_value[1], out.raw_value[0],
			  out.raw_value[1]);

	exp.raw_value[0] = in.raw_value[0];
	exp.raw_value[1] = in.raw_value[1] & 0xff;
	zassert_mem_equal(exp.raw_value, out.raw_value, sizeof(exp),
			  "Returned cable property included extra data "
			  "exp 0x%08X:%08X != out 0x%08X:%08X",
			  exp.raw_value[0], exp.raw_value[1], out.raw_value[0],
			  out.raw_value[1]);

	emul_pdc_disconnect(emul);
	zassert_true(TEST_WAIT_FOR(!pdc_power_mgmt_is_connected(TEST_PORT),
				   PDC_TEST_TIMEOUT));
}

/*
 * Validate that all possible PDC power management states have a name
 * assigned.  This could possibly be done with some macrobatics, but
 * a runtime unit test is easier to maintain.
 */
ZTEST_USER(pdc_power_mgmt_api, test_names)
{
	for (int i = 0; i < pdc_cmd_types; i++) {
		zassert_not_null(pdc_cmd_names[i],
				 "PDC command %d missing name", i);
	}
}

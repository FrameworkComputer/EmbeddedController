/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB Type-C VPD and CTVPD module.
 */
#include "charge_manager.h"
#include "mock/tcpc_mock.h"
#include "mock/usb_mux_mock.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usb_sm_checks.h"
#include "usb_tc_sm.h"

#define PORT0 0

/*
 * Amount of time to wait after a specified timeout. Allows for an extra loop
 * through statemachine plus 1000 calls to clock
 */
#define FUDGE (6 * MSEC)

/* TODO(b/153071799): Move these pd_* and pe_* function into mock */
__overridable void pd_request_power_swap(int port)
{}

uint8_t pd_get_src_cap_cnt(int port)
{
	return 0;
}

const uint32_t * const pd_get_src_caps(int port)
{
	return NULL;
}

void pd_set_src_caps(int port, int cnt, uint32_t *src_caps)
{
}

__overridable void pe_invalidate_explicit_contract(int port)
{
}
/* End pd_ mock section */

/* Install Mock TCPC and MUX drivers */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.drv = &mock_tcpc_driver,
	},
};

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.driver = &mock_usb_mux_driver,
	}
};

void charge_manager_set_ceil(int port, enum ceil_requestor requestor, int ceil)
{
	/* Do Nothing, but needed for linking */
}

__maybe_unused static int test_mux_con_dis_as_src(void)
{
	mock_tcpc.should_print_call = false;
	mock_usb_mux.num_set_calls = 0;

	/* Update CC lines send state machine event to process */
	mock_tcpc.cc1 = TYPEC_CC_VOLT_RD;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_OPEN;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);
	pd_set_dual_role(0, PD_DRP_TOGGLE_ON);

	/* This wait trainsitions through AttachWait.SRC then Attached.SRC */
	task_wait_event(SECOND);

	/* We are in Attached.SRC now */
	TEST_EQ(mock_usb_mux.state, USB_PD_MUX_USB_ENABLED, "%d");
	TEST_EQ(mock_usb_mux.num_set_calls, 1, "%d");

	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_OPEN;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* This wait will go through TryWait.SNK then to Unattached.SNK */
	task_wait_event(10 * SECOND);

	/* We are in Unattached.SNK. The mux should have detached */
	TEST_EQ(mock_usb_mux.state, USB_PD_MUX_NONE, "%d");
	TEST_EQ(mock_usb_mux.num_set_calls, 2, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_mux_con_dis_as_snk(void)
{
	mock_tcpc.should_print_call = false;
	mock_usb_mux.num_set_calls = 0;

	/* Update CC lines send state machine event to process */
	mock_tcpc.cc1 = TYPEC_CC_VOLT_RP_3_0;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* This wait will go through AttachWait.SNK to Attached.SNK */
	task_wait_event(5 * SECOND);

	/*
	 * We are in Attached.SNK now, but the port partner isn't data capable
	 * so we should not connect the USB data mux.
	 */
	TEST_EQ(mock_usb_mux.state, USB_PD_MUX_NONE, "%d");

	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.vbus_level = 0;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* This wait will go through TryWait.SNK then to Unattached.SNK */
	task_wait_event(10 * SECOND);

	/* We are in Unattached.SNK. The mux should have detached */
	TEST_EQ(mock_usb_mux.state, USB_PD_MUX_NONE, "%d");
	TEST_LE(mock_usb_mux.num_set_calls, 2, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_power_role_set(void)
{
	mock_tcpc.num_calls_to_set_header = 0;

	/* Update CC lines send state machine event to process */
	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RD;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);
	task_wait_event(10 * SECOND);

	/* We are in Attached.SRC now */
	TEST_EQ(mock_tcpc.last.power_role, PD_ROLE_SOURCE, "%d");
	TEST_EQ(mock_tcpc.last.data_role, PD_ROLE_DFP, "%d");

	/*
	 * We allow 2 separate calls to update the header since power and data
	 * role updates can be separate calls depending on the state is came
	 * from.
	 */
	TEST_LE(mock_tcpc.num_calls_to_set_header, 2, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_polarity_cc1_default(void)
{
	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC USB-DEF on CC1");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_RP_DEF;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE + FUDGE);
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC1, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_polarity_cc1_1A5(void)
{
	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC USB-1A5 on CC1");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_RP_1_5;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE + FUDGE);
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC1, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_polarity_cc1_3A0(void)
{
	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC USB-3A0 on CC1");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_RP_3_0;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE + FUDGE);
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC1, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_polarity_cc2_default(void)
{
	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC USB-DEF on CC2");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_DEF;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE + FUDGE);
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC2, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_polarity_cc2_1A5(void)
{
	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC USB-1A5 on CC2");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_1_5;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE + FUDGE);
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC2, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_polarity_cc2_3A0(void)
{
	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC USB-3A0 on CC2");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_3_0;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE + FUDGE);
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC2, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_polarity_dts_cc1_default(void)
{
	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC DTS-Default on CC1");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_RP_3_0;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_1_5;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE + FUDGE);
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC1_DTS, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_polarity_dts_cc1_1A5(void)
{
	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC DTS-1A5 on CC1");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_RP_1_5;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_DEF;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE + FUDGE);
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC1_DTS, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_polarity_dts_cc1_3A0(void)
{
	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC DTS-1A5 on CC1");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_RP_3_0;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_DEF;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE + FUDGE);
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC1_DTS, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_polarity_dts_cc2_default(void)
{
	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC DTS-Default on CC2");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_RP_1_5;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_3_0;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE + FUDGE);
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC2_DTS, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_polarity_dts_cc2_1A5(void)
{
	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC DTS-1A5 on CC2");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_RP_DEF;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_1_5;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE + FUDGE);
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC2_DTS, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_polarity_dts_cc2_3A0(void)
{
	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC DTS-1A5 on CC2");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_RP_DEF;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_3_0;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE + FUDGE);
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC2_DTS, "%d");

	return EC_SUCCESS;
}

/* Record any calls that would change our CCs to Rp */
static int changes_to_rp;
static int record_changes_to_rp(int port, int pull)
{
	if (pull == TYPEC_CC_RP)
		++changes_to_rp;

	return EC_SUCCESS;
};

__maybe_unused static int test_try_src_disabled(void)
{
	changes_to_rp = 0;
	mock_tcpc.callbacks.set_cc = &record_changes_to_rp;
	tc_try_src_override(TRY_SRC_OVERRIDE_OFF);

	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_3_0;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Wait a long time past many potential transitions */
	task_wait_event(10 * SECOND);

	TEST_EQ(mock_tcpc.last.cc, TYPEC_CC_RD, "%d");
	TEST_EQ(changes_to_rp, 0, "%d");
	TEST_EQ(mock_tcpc.last.power_role, PD_ROLE_SINK, "%d");
	TEST_EQ(mock_tcpc.last.data_role, PD_ROLE_UFP, "%d");
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC2, "%d");
	TEST_EQ(tc_is_attached_snk(PORT0), true, "%d");

	return EC_SUCCESS;
}

/* Act like a PD device that switches to opposite role */
static int switch_to_opposite_role(int port, int pull)
{
	static enum tcpc_cc_pull last_pull = -1;

	if (pull == last_pull)
		return EC_SUCCESS;

	last_pull = pull;

	if (pull == TYPEC_CC_RP) {
		/* If host is setting Rp, then CCs will negotiate as SNK */
		mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
		mock_tcpc.cc2 = TYPEC_CC_VOLT_RD;
		mock_tcpc.vbus_level = 0;
		ccprints("[Test] Partner presents SNK");
	} else if (pull == TYPEC_CC_RD) {
		/* If host is setting Rd, then CCs will negotiate as SRC */
		mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
		mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_3_0;
		mock_tcpc.vbus_level = 1;
		ccprints("[Test] Partner presents SRC with Vbus ON");
	}

	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	return EC_SUCCESS;
};

__maybe_unused static int test_try_src_partner_switches(void)
{
	mock_tcpc.callbacks.set_cc = &switch_to_opposite_role;
	tc_try_src_override(TRY_SRC_OVERRIDE_ON);

	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_3_0;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* We are in AttachWait.SNK now */
	/* Before tCCDebounce elapses, we should still be a SNK */
	task_wait_event(PD_T_CC_DEBOUNCE / 2);
	TEST_EQ(mock_tcpc.last.cc, TYPEC_CC_RD, "%d");
	task_wait_event(PD_T_CC_DEBOUNCE / 2);

	/* We are in Try.SRC now */
	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE / 2);
	TEST_EQ(mock_tcpc.last.cc, TYPEC_CC_RP, "%d");

	/* Wait for tCCDebounce to elapse, then should be SRC */
	task_wait_event(PD_T_CC_DEBOUNCE);
	TEST_EQ(mock_tcpc.last.power_role, PD_ROLE_SOURCE, "%d");
	TEST_EQ(mock_tcpc.last.data_role, PD_ROLE_DFP, "%d");
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC2, "%d");
	TEST_EQ(tc_is_attached_src(PORT0), true, "%d");

	return EC_SUCCESS;
}

/* Act like a non-PD charger that always presents Vbus and Rp lines */
static int dumb_src_charger_cc_response(int port, int pull)
{
	static enum tcpc_cc_pull last_pull = -1;

	if (pull == last_pull)
		return EC_SUCCESS;

	last_pull = pull;

	if (pull == TYPEC_CC_RP) {
		/* If host is setting Rp, then CCs will open */
		mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
		mock_tcpc.cc2 = TYPEC_CC_VOLT_OPEN;
	} else if (pull == TYPEC_CC_RD) {
		/* If host is setting Rd, then CCs will negotiate */
		mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
		mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_3_0;
	}
	mock_tcpc.vbus_level = 1;

	ccprints("[Test] Partner presents SRC with Vbus ON");

	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	return EC_SUCCESS;
};

__maybe_unused static int test_try_src_partner_does_not_switch_vbus(void)
{
	tc_try_src_override(TRY_SRC_OVERRIDE_ON);
	mock_tcpc.callbacks.set_cc = &dumb_src_charger_cc_response;

	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_3_0;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* We are in AttachWait.SNK now */
	/* Before tCCDebounce elapses, we should still be a SNK */
	task_wait_event(PD_T_CC_DEBOUNCE / 2);
	TEST_EQ(mock_tcpc.last.cc, TYPEC_CC_RD, "%d");
	task_wait_event(PD_T_CC_DEBOUNCE / 2);

	/* We are in Try.SRC now */
	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE / 2);
	TEST_EQ(mock_tcpc.last.cc, TYPEC_CC_RP, "%d");

	/*
	 * Wait for tTryTimeout to elapse, then should be
	 * presenting SNK resistors again but not connected yet, until we
	 * debounce Vbus.
	 */
	task_wait_event(PD_T_TRY_TIMEOUT);
	TEST_EQ(mock_tcpc.last.power_role, PD_ROLE_SINK, "%d");
	TEST_EQ(tc_is_attached_snk(PORT0), false, "%d");

	/* Once we debouce Vbus, then we should be connected */
	task_wait_event(PD_T_CC_DEBOUNCE);
	TEST_EQ(mock_tcpc.last.power_role, PD_ROLE_SINK, "%d");
	TEST_EQ(mock_tcpc.last.data_role, PD_ROLE_UFP, "%d");
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC2, "%d");
	TEST_EQ(tc_is_attached_snk(PORT0), true, "%d");

	return EC_SUCCESS;
}

/* Act like a PD charger that will drop Vbus when CC lines are open */
static int src_charger_drops_vbus_cc_response(int port, int pull)
{
	static enum tcpc_cc_pull last_pull = -1;

	if (pull == last_pull)
		return EC_SUCCESS;

	last_pull = pull;

	if (pull == TYPEC_CC_RP) {
		/* If host is setting Rp, then CCs will open */
		mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
		mock_tcpc.cc2 = TYPEC_CC_VOLT_OPEN;
		mock_tcpc.vbus_level = 0;
		ccprints("[Test] Partner presents SRC with Vbus OFF");
	} else if (pull == TYPEC_CC_RD) {
		/* If host is setting Rd, then CCs will negotiate */
		mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
		mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_3_0;
		mock_tcpc.vbus_level = 1;
		ccprints("[Test] Partner presents SRC with Vbus ON");
	}

	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	return EC_SUCCESS;
};

__maybe_unused static int test_try_src_partner_does_not_switch_no_vbus(void)
{
	tc_try_src_override(TRY_SRC_OVERRIDE_ON);
	mock_tcpc.callbacks.set_cc = &src_charger_drops_vbus_cc_response;

	/* Update CC lines send state machine event to process */
	ccprints("[Test] Partner connects as SRC");
	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_3_0;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* We are in AttachWait.SNK now */
	/* Before tCCDebounce elapses, we should still be a SNK */
	task_wait_event(PD_T_CC_DEBOUNCE / 2);
	TEST_EQ(mock_tcpc.last.cc, TYPEC_CC_RD, "%d");
	task_wait_event(PD_T_CC_DEBOUNCE / 2);

	/* We are in Try.SRC now */
	/* Before tCCDebounce elapses, we should SRC */
	task_wait_event(PD_T_CC_DEBOUNCE / 2);
	TEST_EQ(mock_tcpc.last.cc, TYPEC_CC_RP, "%d");

	/*
	 * Wait for tTryTimeout to elapse, then should be
	 * presenting SNK resistors again but not connected yet, until we
	 * debounce Vbus.
	 */
	task_wait_event(PD_T_DRP_TRY);
	TEST_EQ(mock_tcpc.last.power_role, PD_ROLE_SINK, "%d");
	TEST_EQ(tc_is_attached_snk(PORT0), false, "%d");

	/* Once we debouce Vbus, then we should be connected */
	task_wait_event(PD_T_CC_DEBOUNCE);
	TEST_EQ(mock_tcpc.last.power_role, PD_ROLE_SINK, "%d");
	TEST_EQ(mock_tcpc.last.data_role, PD_ROLE_UFP, "%d");
	TEST_EQ(mock_tcpc.last.polarity, POLARITY_CC2, "%d");
	TEST_EQ(tc_is_attached_snk(PORT0), true, "%d");

	return EC_SUCCESS;
}

/* TODO(b/153071799): test as SNK monitor for Vbus disconnect (not CC line) */
/* TODO(b/153071799): test as SRC monitor for CC line state change */

/* Reset the mocks before each test */
void before_test(void)
{
	mock_usb_mux_reset();
	mock_tcpc_reset();

	tc_try_src_override(TRY_SRC_NO_OVERRIDE);

	tc_restart_tcpc(PORT0);

	/* Ensure that PD task initializes its state machine and settles */
	task_wake(TASK_ID_PD_C0);
	task_wait_event(SECOND);

	/* Print out TCPC calls for easier debugging */
	mock_tcpc.should_print_call = true;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_polarity_cc1_default);
	RUN_TEST(test_polarity_cc1_1A5);
	RUN_TEST(test_polarity_cc1_3A0);

	RUN_TEST(test_polarity_cc2_default);
	RUN_TEST(test_polarity_cc2_1A5);
	RUN_TEST(test_polarity_cc2_3A0);

	RUN_TEST(test_polarity_dts_cc1_default);
	RUN_TEST(test_polarity_dts_cc1_1A5);
	RUN_TEST(test_polarity_dts_cc1_3A0);

	RUN_TEST(test_polarity_dts_cc2_default);
	RUN_TEST(test_polarity_dts_cc2_1A5);
	RUN_TEST(test_polarity_dts_cc2_3A0);

	RUN_TEST(test_mux_con_dis_as_src);
	RUN_TEST(test_mux_con_dis_as_snk);
	RUN_TEST(test_power_role_set);

	RUN_TEST(test_try_src_disabled);
	RUN_TEST(test_try_src_partner_switches);
	RUN_TEST(test_try_src_partner_does_not_switch_vbus);
	RUN_TEST(test_try_src_partner_does_not_switch_no_vbus);

	/* Do basic state machine sanity checks last. */
	RUN_TEST(test_tc_no_parent_cycles);
	RUN_TEST(test_tc_no_empty_state);
	RUN_TEST(test_tc_all_states_named);

	test_print_result();
}

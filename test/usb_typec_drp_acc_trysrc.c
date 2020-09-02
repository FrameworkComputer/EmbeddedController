/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB Type-C Dual Role Port, Audio Accessory, and Try.SRC Device module.
 */
#include "charge_manager.h"
#include "mock/tcpc_mock.h"
#include "mock/usb_mux_mock.h"
#include "system.h"
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

/* Unreachable time in future */
#define TIMER_DISABLED 0xffffffffffffffff

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

	/*
	 * We need to allow auto toggling to see the port partner attach
	 * as a sink
	 */
	pd_set_dual_role(PORT0, PD_DRP_TOGGLE_ON);

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

	/*
	 * In this test we are expecting value of polarity, which is set by
	 * default for tcpc mock. Initialize it with something else, in order
	 * to catch possible errors.
	 */
	mock_tcpc.last.polarity = POLARITY_COUNT;

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

/* Record the cc voltages */
static enum tcpc_cc_pull cc_pull[16];
static int cc_pull_count;
static int record_cc_pull(int port, int pull)
{
	if (cc_pull_count < ARRAY_SIZE(cc_pull))
		cc_pull[cc_pull_count++] = pull;

	return EC_SUCCESS;
};

__maybe_unused static int test_cc_open_on_normal_reset(void)
{
	uint32_t flags = system_get_reset_flags();

	cc_pull_count = 0;
	mock_tcpc.callbacks.set_cc = &record_cc_pull;

	system_clear_reset_flags(EC_RESET_FLAG_POWER_ON);

	task_set_event(TASK_ID_PD_C0, TASK_EVENT_RESET_DONE, 0);
	task_wait_event(SECOND * 10);

	/* Ensure that the first CC set call was to open (error recovery). */
	TEST_GT(cc_pull_count, 0, "%d");
	TEST_EQ(cc_pull[0], TYPEC_CC_OPEN, "%d");

	/* Ensure that the second CC set call was to Rd (sink) */
	TEST_GT(cc_pull_count, 1, "%d");
	TEST_EQ(cc_pull[1], TYPEC_CC_RD, "%d");

	/* Reset system flags after test */
	system_set_reset_flags(flags);

	return EC_SUCCESS;
}

__maybe_unused static int test_cc_rd_on_por_reset(void)
{
	uint32_t flags = system_get_reset_flags();

	cc_pull_count = 0;
	mock_tcpc.callbacks.set_cc = &record_cc_pull;

	system_set_reset_flags(EC_RESET_FLAG_POWER_ON);

	task_set_event(TASK_ID_PD_C0, TASK_EVENT_RESET_DONE, 0);
	task_wait_event(SECOND * 10);

	/* Ensure that the first CC set call was to Rd (sink) */
	TEST_GT(cc_pull_count, 0, "%d");
	TEST_EQ(cc_pull[0], TYPEC_CC_RD, "%d");

	/* Reset system flags after test */
	system_clear_reset_flags(~flags);

	return EC_SUCCESS;
}

__maybe_unused static int test_auto_toggle_delay(void)
{
	uint64_t time;

	/* Start with auto toggle disabled so we can time the transition */
	pd_set_dual_role(PORT0, PD_DRP_TOGGLE_OFF);
	task_wait_event(SECOND);

	/* Enabled auto toggle and start the timer for the transition */
	pd_set_dual_role(PORT0, PD_DRP_TOGGLE_ON);
	time = get_time().val;

	/*
	 * Ensure we do not transition to auto toggle from Rd or Rp in less time
	 * than tDRP minimum (50 ms) * dcSRC.DRP minimum (30%) = 15 ms.
	 * Otherwise we can confuse external partners with the first transition
	 * to auto toggle.
	 */
	task_wait_event(SECOND);
	TEST_GT(mock_tcpc.first_call_to_enable_auto_toggle - time,
		15ul * MSEC, "%lu");

	return EC_SUCCESS;
}

__maybe_unused static int test_auto_toggle_delay_early_connect(void)
{
	cc_pull_count = 0;
	mock_tcpc.callbacks.set_cc = &record_cc_pull;
	mock_tcpc.first_call_to_enable_auto_toggle = TIMER_DISABLED;

	/* Start with auto toggle disabled */
	pd_set_dual_role(PORT0, PD_DRP_TOGGLE_OFF);
	task_wait_event(SECOND);

	/* Enabled auto toggle */
	pd_set_dual_role(PORT0, PD_DRP_TOGGLE_ON);

	/* Wait less than tDRP_SNK(40ms) and tDRP_SRC(30ms) */
	task_wait_event(MIN(PD_T_DRP_SNK, PD_T_DRP_SRC) - (10 * MSEC));

	/* Have partner connect as SRC */
	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RP_3_0;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* Ensure the auto toggle enable was never called */
	task_wait_event(SECOND);
	TEST_EQ(mock_tcpc.first_call_to_enable_auto_toggle,
		TIMER_DISABLED, "%lu");

	/* Ensure that the first CC set call was to Rd. */
	TEST_GT(cc_pull_count, 0, "%d");
	TEST_EQ(cc_pull[0], TYPEC_CC_RD, "%d");

	return EC_SUCCESS;
}

/* TODO(b/153071799): test as SNK monitor for Vbus disconnect (not CC line) */
/* TODO(b/153071799): test as SRC monitor for CC line state change */

/* Reset the mocks before each test */
void before_test(void)
{
	mock_usb_mux_reset();
	mock_tcpc_reset();

	/* Restart the PD task and let it settle */
	task_set_event(TASK_ID_PD_C0, TASK_EVENT_RESET_DONE, 0);
	task_wait_event(SECOND);

	/* Print out TCPC calls for easier debugging */
	mock_tcpc.should_print_call = true;
}

void run_test(int argc, char **argv)
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

	RUN_TEST(test_cc_open_on_normal_reset);
	RUN_TEST(test_cc_rd_on_por_reset);
	RUN_TEST(test_auto_toggle_delay);
	RUN_TEST(test_auto_toggle_delay_early_connect);

	/* Do basic state machine validity checks last. */
	RUN_TEST(test_tc_no_parent_cycles);
	RUN_TEST(test_tc_all_states_named);

	test_print_result();
}

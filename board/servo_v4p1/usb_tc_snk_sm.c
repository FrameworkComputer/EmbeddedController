/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "fusb302b.h"
#include "ioexpanders.h"
#include "system.h"
#include "task.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "usb_sm.h"
#include "usb_tc_sm.h"

#define EVT_TIMEOUT_NEVER  (-1)
#define EVT_TIMEOUT_5MS    (5 * MSEC)

/*
 * USB Type-C Sink
 *   See Figure 4-13 in Release 1.4 of USB Type-C Spec.
 */
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Type-C Layer Flags */

/* List of all TypeC-level states */
enum usb_tc_state {
	TC_UNATTACHED_SNK,
	TC_ATTACH_WAIT_SNK,
	TC_ATTACHED_SNK,
};
/* Forward declare the full list of states. This is indexed by usb_tc_state */
static const struct usb_state tc_states[];

/* TypeC Power strings */
static const char * const pwr2_5_str = "5V/0.5A";
static const char * const pwr7_5_str = "5V/1.5A";
static const char * const pwr15_str  = "5V/3A";

static struct type_c {
	/* state machine context */
	struct sm_ctx ctx;
	/* Port polarity */
	enum tcpc_cc_polarity polarity;
	/* event timeout */
	uint64_t evt_timeout;
	/* Time a port shall wait before it can determine it is attached */
	uint64_t cc_debounce;
	/*
	 * Time a Sink port shall wait before it can determine it is detached
	 * due to the potential for USB PD signaling on CC as described in
	 * the state definitions.
	 */
	uint64_t pd_debounce;
	/* The cc state */
	enum pd_cc_states cc_state;
	/* Generic timer */
	uint64_t timeout;
	/* Voltage on CC pin */
	enum tcpc_cc_voltage_status cc_voltage;
	/* Current CC1 value */
	int cc1;
	/* Current CC2 value */
	int cc2;
} tc;

/* Forward declare common, private functions */
static void set_state_tc(const enum usb_tc_state new_state);

static void restart_tc_sm(enum usb_tc_state start_state)
{
	int res;

	res = init_fusb302b(1);
	CPRINTS("FUSB302b init %s", res ? "failed" : "ready");

	/* State machine is disabled if init_fusb302b fails */
	if (!res)
		set_state_tc(start_state);

	/* Disable timeout. Task will wake on interrupt */
	tc.evt_timeout = EVT_TIMEOUT_NEVER;
}

/*
 * Private Functions
 */

/* Set the TypeC state machine to a new state. */
static void set_state_tc(const enum usb_tc_state new_state)
{
	set_state(0, &tc.ctx, &tc_states[new_state]);
}

static void print_alt_power(void)
{
	enum tcpc_cc_voltage_status cc;
	char const *pwr;

	cc = tc.polarity ? tc.cc2 : tc.cc1;
	if (cc == TYPEC_CC_VOLT_OPEN ||
		cc == TYPEC_CC_VOLT_RA || cc == TYPEC_CC_VOLT_RD) {
		/* Supply removed or not detected */
		return;
	}

	if (cc == TYPEC_CC_VOLT_RP_1_5)
		pwr = pwr7_5_str;
	else if (cc == TYPEC_CC_VOLT_RP_3_0)
		pwr = pwr15_str;
	else
		pwr = pwr2_5_str;

	CPRINTS("ALT: Switching to alternate supply @ %s", pwr);
}

static void sink_power_sub_states(void)
{
	enum tcpc_cc_voltage_status cc;
	enum tcpc_cc_voltage_status new_cc_voltage;

	cc = tc.polarity ? tc.cc2 : tc.cc1;

	if (cc == TYPEC_CC_VOLT_RP_DEF)
		new_cc_voltage = TYPEC_CC_VOLT_RP_DEF;
	else if (cc == TYPEC_CC_VOLT_RP_1_5)
		new_cc_voltage = TYPEC_CC_VOLT_RP_1_5;
	else if (cc == TYPEC_CC_VOLT_RP_3_0)
		new_cc_voltage = TYPEC_CC_VOLT_RP_3_0;
	else
		new_cc_voltage = TYPEC_CC_VOLT_OPEN;

	/* Debounce the cc state */
	if (new_cc_voltage != tc.cc_voltage) {
		tc.cc_voltage = new_cc_voltage;
		tc.cc_debounce = get_time().val + PD_T_RP_VALUE_CHANGE;
		return;
	}

	if (tc.cc_debounce == 0 || get_time().val < tc.cc_debounce)
		return;

	tc.cc_debounce = 0;
	print_alt_power();
}

/*
 * TYPE-C State Implementations
 */

/**
 * Unattached.SNK
 */
static void tc_unattached_snk_entry(int port)
{
	tc.evt_timeout = EVT_TIMEOUT_NEVER;
}

static void tc_unattached_snk_run(int port)
{
	/*
	 * The port shall transition to AttachWait.SNK when a Source
	 * connection is detected, as indicated by the SNK.Rp state
	 * on at least one of its CC pins.
	 */
	if (cc_is_rp(tc.cc1) || cc_is_rp(tc.cc2))
		set_state_tc(TC_ATTACH_WAIT_SNK);
}

/**
 * AttachWait.SNK
 */
static void tc_attach_wait_snk_entry(int port)
{
	tc.evt_timeout = EVT_TIMEOUT_5MS;
	tc.cc_state = PD_CC_UNSET;
}

static void tc_attach_wait_snk_run(int port)
{
	enum pd_cc_states new_cc_state;

	if (cc_is_rp(tc.cc1) && cc_is_rp(tc.cc2))
		new_cc_state = PD_CC_DFP_DEBUG_ACC;
	else if (cc_is_rp(tc.cc1) || cc_is_rp(tc.cc2))
		new_cc_state = PD_CC_DFP_ATTACHED;
	else
		new_cc_state = PD_CC_NONE;

	/* Debounce the cc state */
	if (new_cc_state != tc.cc_state) {
		tc.cc_debounce = get_time().val + PD_T_CC_DEBOUNCE;
		tc.pd_debounce = get_time().val + PD_T_PD_DEBOUNCE;
		tc.cc_state = new_cc_state;
		return;
	}

	/* Wait for CC debounce */
	if (get_time().val < tc.cc_debounce)
		return;

	/*
	 * The port shall transition to Attached.SNK after the state of only
	 * one of the CC1 or CC2 pins is SNK.Rp for at least tCCDebounce and
	 * VBUS is detected.
	 */
	if (is_vbus_present() && (new_cc_state == PD_CC_DFP_ATTACHED))
		set_state_tc(TC_ATTACHED_SNK);
	else
		set_state_tc(TC_UNATTACHED_SNK);
}

/**
 * Attached.SNK
 */
static void tc_attached_snk_entry(int port)
{
	print_alt_power();

	tc.evt_timeout = EVT_TIMEOUT_NEVER;
	tc.cc_debounce = 0;

	/* Switch over to alternate supply */
	en_pp5000_alt_3p3(1);
}

static void tc_attached_snk_run(int port)
{
	/* Detach detection */
	if (!is_vbus_present()) {
		set_state_tc(TC_UNATTACHED_SNK);
		return;
	}

	/* Run Sink Power Sub-State */
	sink_power_sub_states();
}

static void tc_attached_snk_exit(int port)
{
	/* Alternate charger removed. Switch back to host power */
	en_pp5000_alt_3p3(0);
}

/*
 * Type-C State
 *
 * TC_UNATTACHED_SNK
 * TC_ATTACH_WAIT_SNK
 * TC_TRY_WAIT_SNK
 * TC_ATTACHED_SNK
 */
static const struct usb_state tc_states[] = {
	[TC_UNATTACHED_SNK] = {
		.entry	= tc_unattached_snk_entry,
		.run	= tc_unattached_snk_run,
	},
	[TC_ATTACH_WAIT_SNK] = {
		.entry	= tc_attach_wait_snk_entry,
		.run	= tc_attach_wait_snk_run,
	},
	[TC_ATTACHED_SNK] = {
		.entry	= tc_attached_snk_entry,
		.run	= tc_attached_snk_run,
		.exit	= tc_attached_snk_exit,
	},
};

void snk_task(void *u)
{
	/* Unattached.SNK is the default starting state. */
	restart_tc_sm(TC_UNATTACHED_SNK);

	while (1) {
		/* wait for next event or timeout expiration */
		task_wait_event(tc.evt_timeout);

		/* Sample CC lines */
		get_cc(&tc.cc1, &tc.cc2);

		/* Detect polarity */
		tc.polarity = (tc.cc1 > tc.cc2) ? POLARITY_CC1 : POLARITY_CC2;

		/* Run TypeC state machine */
		run_state(0, &tc.ctx);
	}
}


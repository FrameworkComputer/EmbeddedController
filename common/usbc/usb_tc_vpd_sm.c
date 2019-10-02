/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "system.h"
#include "task.h"
#include "tcpm.h"
#include "usb_pd.h"
#include "usb_tc_sm.h"
#include "usb_sm.h"
#include "vpd_api.h"

/* USB Type-C VCONN Powered Device module */

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)
#else /* CONFIG_COMMON_RUNTIME */
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/* Type-C Layer Flags */
#define TC_FLAGS_VCONN_ON           BIT(0)

/**
 * This is the Type-C Port object that contains information needed to
 * implement a VCONN Powered Device.
 */
static struct type_c {
	/* state machine context */
	struct sm_ctx ctx;
	/* Higher-level power deliver state machines are enabled if true. */
	uint8_t pd_enable;
	/* port flags, see TC_FLAGS_* */
	uint32_t flags;
	/* Time a port shall wait before it can determine it is attached */
	uint64_t cc_debounce;
	/* VPD host port cc state */
	enum pd_cc_states host_cc_state;
	uint8_t ct_cc;
} tc[CONFIG_USB_PD_PORT_MAX_COUNT];

/* List of all TypeC-level states */
enum usb_tc_state {
	/* Normal States */
	TC_DISABLED,
	TC_UNATTACHED_SNK,
	TC_ATTACH_WAIT_SNK,
	TC_ATTACHED_SNK,
	/* Super States */
	TC_VBUS_CC_ISO,
	TC_HOST_RARD,
	TC_HOST_OPEN,
};
/* Forward declare the full list of states. This is indexed by usb_tc_state */
static const struct usb_state tc_states[];

#ifdef CONFIG_COMMON_RUNTIME
/* List of human readable state names for console debugging */
static const char * const tc_state_names[] = {
	[TC_DISABLED] = "Disabled",
	[TC_UNATTACHED_SNK] = "Unattached.SNK",
	[TC_ATTACH_WAIT_SNK] = "AttachWait.SNK",
	[TC_ATTACHED_SNK] = "Attached.SNK",
};
#endif

/* Forward declare private, common functions */
static void set_state_tc(const int port, enum usb_tc_state new_state);

/* Public TypeC functions */

void tc_state_init(int port)
{
	int res = 0;

	res = tc_restart_tcpc(port);

	CPRINTS("TCPC p%d init %s", port, res ? "failed" : "ready");

	/* Disable TCPC RX until connection is established */
	tcpm_set_rx_enable(port, 0);

	set_state_tc(port, res ? TC_DISABLED : TC_UNATTACHED_SNK);

	/* Disable pd state machines */
	tc[port].pd_enable = 0;
	tc[port].flags = 0;
}

enum pd_power_role tc_get_power_role(int port)
{
	/* Vconn power device is always the sink */
	return PD_ROLE_SINK;
}

enum pd_cable_plug tc_get_cable_plug(int port)
{
	/* Vconn power device is always the cable */
	return PD_PLUG_FROM_CABLE;
}

enum pd_data_role tc_get_data_role(int port)
{
	/* Vconn power device doesn't have a data role, but UFP match SNK */
	return PD_ROLE_UFP;
}

/* Note tc_set_power_role and tc_set_data_role are unimplemented */

uint8_t tc_get_polarity(int port)
{
	/* Does not track polarity yet */
	return 0;
}

uint8_t tc_get_pd_enabled(int port)
{
	return tc[port].pd_enable;
}

void tc_event_check(int port, int evt)
{
	/* Do Nothing */
}

/*
 * Private Functions
 */

/* Set the TypeC state machine to a new state. */
static void set_state_tc(const int port, const enum usb_tc_state new_state)
{
	set_state(port, &tc[port].ctx, &tc_states[new_state]);
}

/* Get the current TypeC state. */
test_export_static enum usb_tc_state get_state_tc(const int port)
{
	return tc[port].ctx.current - &tc_states[0];
}

test_mockable_static void print_current_state(const int port)
{
	CPRINTS("C%d: %s", port, tc_state_names[get_state_tc(port)]);
}

/**
 * Disabled
 *
 * Super State Entries:
 *   Enable mcu communication
 *   Remove the terminations from Host CC
 */
static void tc_disabled_entry(const int port)
{
	print_current_state(port);
}

static void tc_disabled_run(const int port)
{
	task_wait_event(-1);
}

static void tc_disabled_exit(const int port)
{
	if (!IS_ENABLED(CONFIG_USB_PD_TCPC)) {
		if (tc_restart_tcpc(port) != 0) {
			CPRINTS("TCPC p%d restart failed!", port);
			return;
		}
	}

	CPRINTS("TCPC p%d resumed!", port);
}

/**
 * Unattached.SNK
 *
 * Super State Entry:
 *   Enable mcu communication
 *   Place Ra on VCONN and Rd on Host CC
 */
static void tc_unattached_snk_entry(const int port)
{
	print_current_state(port);
}

static void tc_unattached_snk_run(const int port)
{
	int host_cc;

	/* Check Host CC for connection */
	vpd_host_get_cc(&host_cc);

	/*
	 * Transition to AttachWait.SNK when a Source connection is
	 * detected, as indicated by the SNK.Rp state on its Host-side
	 * port’s CC pin.
	 */
	if (cc_is_rp(host_cc))
		set_state_tc(port, TC_ATTACH_WAIT_SNK);
}

/**
 * AttachedWait.SNK
 *
 * Super State Entry:
 *   Enable mcu communication
 *   Place Ra on VCONN and Rd on Host CC
 */
static void tc_attach_wait_snk_entry(const int port)
{
	print_current_state(port);

	/* Forces an initial debounce in run function */
	tc[port].host_cc_state = -1;
}

static void tc_attach_wait_snk_run(const int port)
{
	int host_new_cc_state;
	int host_cc;

	/* Check Host CC for connection */
	vpd_host_get_cc(&host_cc);

	if (cc_is_rp(host_cc))
		host_new_cc_state = PD_CC_DFP_ATTACHED;
	else
		host_new_cc_state = PD_CC_NONE;

	/* Debounce the Host CC state */
	if (tc[port].host_cc_state != host_new_cc_state) {
		tc[port].host_cc_state = host_new_cc_state;
		if (host_new_cc_state == PD_CC_DFP_ATTACHED)
			tc[port].cc_debounce = get_time().val +
							PD_T_CC_DEBOUNCE;
		else
			tc[port].cc_debounce = get_time().val +
							PD_T_PD_DEBOUNCE;

		return;
	}

	/* Wait for Host CC debounce */
	if (get_time().val < tc[port].cc_debounce)
		return;

	/*
	 * A VCONN-Powered USB Device shall transition to
	 * Attached.SNK after the state of the Host-side port’s CC pin is
	 * SNK.Rp for at least tCCDebounce and either host-side VCONN or
	 * VBUS is detected.
	 *
	 * Transition to Unattached.SNK when the state of both the CC1 and
	 * CC2 pins is SNK.Open for at least tPDDebounce.
	 */
	if (tc[port].host_cc_state == PD_CC_DFP_ATTACHED &&
			(vpd_is_vconn_present() || vpd_is_host_vbus_present()))
		set_state_tc(port, TC_ATTACHED_SNK);
	else if (tc[port].host_cc_state == PD_CC_NONE)
		set_state_tc(port, TC_UNATTACHED_SNK);
}

/**
 * Attached.SNK
 */
static void tc_attached_snk_entry(const int port)
{
	print_current_state(port);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);
}

static void tc_attached_snk_run(const int port)
{
	/* Has host vbus and vconn been removed */
	if (!vpd_is_host_vbus_present() && !vpd_is_vconn_present()) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	if (vpd_is_vconn_present()) {
		if (!(tc[port].flags & TC_FLAGS_VCONN_ON)) {
			/* VCONN detected. Remove RA */
			vpd_host_set_pull(TYPEC_CC_RD, 0);
			tc[port].flags |= TC_FLAGS_VCONN_ON;
		}
	}
}

static void tc_attached_snk_exit(const int port)
{
	/* Disable PD */
	tc[port].pd_enable = 0;
	tc[port].flags &= ~TC_FLAGS_VCONN_ON;
}

/**
 * Super State HOST_RARD
 */
static void tc_host_rard_entry(const int port)
{
	/* Place Ra on VCONN and Rd on Host CC */
	vpd_host_set_pull(TYPEC_CC_RA_RD, 0);
}

/**
 * Super State HOST_OPEN
 */
static void tc_host_open_entry(const int port)
{
	/* Remove the terminations from Host CC */
	vpd_host_set_pull(TYPEC_CC_OPEN, 0);
}

/**
 * Super State VBUS_CC_ISO
 */
static void tc_vbus_cc_iso_entry(const int port)
{
	/* Enable mcu communication and cc */
	vpd_mcu_cc_en(1);
}

void tc_run(const int port)
{
	run_state(port, &tc[port].ctx);
}

/*
 * Type-C State Hierarchy (Sub-States are listed inside the boxes)
 *
 * | TC_VBUS_CC_ISO ----------------------------------------|
 * |                                                        |
 * |  | TC_HOST_RARD -----------| | TC_HOST_OPEN ---------| |
 * |  |                         | |                       | |
 * |  | TC_UNATTACHED_SNK       | | TC_DISABLED           | |
 * |  | TC_ATTACH_WAIT_SNK      | |-----------------------| |
 * |  |-------------------------|                           |
 * |--------------------------------------------------------|
 *
 * TC_ATTACHED_SNK
 */
static const struct usb_state tc_states[] = {
	/* Super States */
	[TC_VBUS_CC_ISO] = {
		.entry  = tc_vbus_cc_iso_entry,
	},
	[TC_HOST_RARD] = {
		.entry  = tc_host_rard_entry,
		.parent = &tc_states[TC_VBUS_CC_ISO],
	},
	[TC_HOST_OPEN] = {
		.entry  = tc_host_open_entry,
		.parent = &tc_states[TC_VBUS_CC_ISO],
	},
	/* Normal States */
	[TC_DISABLED] = {
		.entry  = tc_disabled_entry,
		.run    = tc_disabled_run,
		.exit   = tc_disabled_exit,
		.parent = &tc_states[TC_HOST_OPEN],
	},
	[TC_UNATTACHED_SNK] = {
		.entry  = tc_unattached_snk_entry,
		.run    = tc_unattached_snk_run,
		.parent = &tc_states[TC_HOST_RARD],
	},
	[TC_ATTACH_WAIT_SNK] = {
		.entry  = tc_attach_wait_snk_entry,
		.run    = tc_attach_wait_snk_run,
		.parent = &tc_states[TC_HOST_RARD],
	},
	[TC_ATTACHED_SNK] = {
		.entry  = tc_attached_snk_entry,
		.run    = tc_attached_snk_run,
		.exit   = tc_attached_snk_exit,
	},
};

#ifdef TEST_BUILD
const struct test_sm_data test_tc_sm_data[] = {
	{
		.base = tc_states,
		.size = ARRAY_SIZE(tc_states),
		.names = tc_state_names,
		.names_size = ARRAY_SIZE(tc_state_names),
	},
};
const int test_tc_sm_data_size = ARRAY_SIZE(test_tc_sm_data);
#endif

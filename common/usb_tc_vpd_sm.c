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
#include "usb_tc_vpd_sm.h"
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

#undef PD_DEFAULT_STATE
/* Port default state at startup */
#define PD_DEFAULT_STATE(port) tc_unattached_snk

/**
 * This is the Type-C Port object that contains information needed to
 * implement a VCONN Powered Device.
 */
struct type_c tc[CONFIG_USB_PD_PORT_COUNT];

/* Type-C states */
DECLARE_STATE(tc, disabled, WITH_EXIT);
DECLARE_STATE(tc, unattached_snk, NOOP_EXIT);
DECLARE_STATE(tc, attach_wait_snk, NOOP_EXIT);
DECLARE_STATE(tc, attached_snk, WITH_EXIT);

/* Super States */
DECLARE_STATE(tc, host_rard, NOOP_EXIT);
DECLARE_STATE(tc, host_open, NOOP_EXIT);
DECLARE_STATE(tc, vbus_cc_iso, NOOP_EXIT);

void tc_state_init(int port)
{
	int res = 0;
	sm_state this_state;

	res = tc_restart_tcpc(port);

	CPRINTS("TCPC p%d init %s", port, res ? "failed" : "ready");
	this_state = res ? tc_disabled : PD_DEFAULT_STATE(port);

	/* Disable TCPC RX until connection is established */
	tcpm_set_rx_enable(port, 0);

	init_state(port, TC_OBJ(port), this_state);

	/* Disable pd state machines */
	tc[port].pd_enable = 0;
	tc[port].evt_timeout = 10*MSEC;
	tc[port].power_role = PD_PLUG_CABLE_VPD;
	tc[port].data_role = 0; /* Reserved for VPD */
	tc[port].flags = 0;
}

void tc_event_check(int port, int evt)
{
	/* Do Nothing */
}

/**
 * Disabled
 *
 * Super State Entries:
 *   Enable mcu communication
 *   Remove the terminations from Host CC
 */
static unsigned int tc_disabled(int port, enum signal sig)
{
	int ret = 0;

	ret = (*tc_disabled_sig[sig])(port);
	return SUPER(ret, sig, tc_host_open);
}

static unsigned int tc_disabled_entry(int port)
{
	tc[port].state_id = DISABLED;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	return 0;
}

static unsigned int tc_disabled_run(int port)
{
	task_wait_event(-1);

	return RUN_SUPER;
}

static unsigned int tc_disabled_exit(int port)
{
#ifndef CONFIG_USB_PD_TCPC
	if (tc_restart_tcpc(port) != 0) {
		CPRINTS("TCPC p%d restart failed!", port);
		return 0;
	}
#endif
	CPRINTS("TCPC p%d resumed!", port);
	set_state(port, TC_OBJ(port), tc_unattached_snk);

	return 0;
}

/**
 * Unattached.SNK
 *
 * Super State Entry:
 *   Enable mcu communication
 *   Place Ra on VCONN and Rd on Host CC
 */
static unsigned int tc_unattached_snk(int port, enum signal sig)
{
	int ret;

	ret = (*tc_unattached_snk_sig[sig])(port);
	return SUPER(ret, sig, tc_host_rard);
}

static unsigned int tc_unattached_snk_entry(int port)
{
	tc[port].state_id = UNATTACHED_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	return 0;
}

static unsigned int tc_unattached_snk_run(int port)
{
	int host_cc;

	/* Check Host CC for connection */
	vpd_host_get_cc(&host_cc);

	/*
	 * Transition to AttachWait.SNK when a Source connection is
	 * detected, as indicated by the SNK.Rp state on its Host-side
	 * port’s CC pin.
	 */
	if (cc_is_rp(host_cc)) {
		set_state(port, TC_OBJ(port), tc_attach_wait_snk);
		return 0;
	}

	return RUN_SUPER;
}

/**
 * AttachedWait.SNK
 *
 * Super State Entry:
 *   Enable mcu communication
 *   Place Ra on VCONN and Rd on Host CC
 */
static unsigned int tc_attach_wait_snk(int port, enum signal sig)
{
	int ret = 0;

	ret = (*tc_attach_wait_snk_sig[sig])(port);
	return SUPER(ret, sig, tc_host_rard);
}

static unsigned int tc_attach_wait_snk_entry(int port)
{
	tc[port].state_id = ATTACH_WAIT_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);
	tc[port].host_cc_state = PD_CC_UNSET;

	return 0;
}

static unsigned int tc_attach_wait_snk_run(int port)
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

		return 0;
	}

	/* Wait for Host CC debounce */
	if (get_time().val < tc[port].cc_debounce)
		return 0;

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
		set_state(port, TC_OBJ(port), tc_attached_snk);
	else if (tc[port].host_cc_state == PD_CC_NONE)
		set_state(port, TC_OBJ(port), tc_unattached_snk);

	return 0;
}

/**
 * Attached.SNK
 */
static unsigned int tc_attached_snk(int port, enum signal sig)
{
	int ret;

	ret = (*tc_attached_snk_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int tc_attached_snk_entry(int port)
{
	tc[port].state_id = ATTACHED_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	return 0;
}

static unsigned int tc_attached_snk_run(int port)
{
	/* Has host vbus and vconn been removed */
	if (!vpd_is_host_vbus_present() && !vpd_is_vconn_present()) {
		set_state(port, TC_OBJ(port), tc_unattached_snk);
		return 0;
	}

	if (vpd_is_vconn_present()) {
		if (!(tc[port].flags & TC_FLAGS_VCONN_ON)) {
			/* VCONN detected. Remove RA */
			vpd_host_set_pull(TYPEC_CC_RD, 0);
			tc[port].flags |= TC_FLAGS_VCONN_ON;
		}
	}

	return 0;
}

static unsigned int tc_attached_snk_exit(int port)
{
	/* Disable PD */
	tc[port].pd_enable = 0;
	tc[port].flags &= ~TC_FLAGS_VCONN_ON;

	return 0;
}

/**
 * Super State HOST_RARD
 */
static unsigned int tc_host_rard(int port, enum signal sig)
{
	int ret;

	ret = (*tc_host_rard_sig[sig])(port);
	return SUPER(ret, sig, tc_vbus_cc_iso);
}

static unsigned int tc_host_rard_entry(int port)
{
	/* Place Ra on VCONN and Rd on Host CC */
	vpd_host_set_pull(TYPEC_CC_RA_RD, 0);

	return 0;
}

static unsigned int tc_host_rard_run(int port)
{
	return RUN_SUPER;
}

/**
 * Super State HOST_OPEN
 */
static unsigned int tc_host_open(int port, enum signal sig)
{
	int ret;

	ret = (*tc_host_open_sig[sig])(port);
	return SUPER(ret, sig, tc_vbus_cc_iso);
}

static unsigned int tc_host_open_entry(int port)
{
	/* Remove the terminations from Host CC */
	vpd_host_set_pull(TYPEC_CC_OPEN, 0);

	return 0;
}

static unsigned int tc_host_open_run(int port)
{
	return RUN_SUPER;
}

/**
 * Super State VBUS_CC_ISO
 */
static unsigned int tc_vbus_cc_iso(int port, enum signal sig)
{
	int ret;

	ret = (*tc_vbus_cc_iso_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int tc_vbus_cc_iso_entry(int port)
{
	/* Enable mcu communication and cc */
	vpd_mcu_cc_en(1);

	return 0;
}

static unsigned int tc_vbus_cc_iso_run(int port)
{
	return 0;
}

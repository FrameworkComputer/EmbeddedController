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
#include "usb_tc_ctvpd_sm.h"
#include "usb_tc_sm.h"
#include "vpd_api.h"

/* USB Type-C CTVPD module */

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_HOOK, format, ## args)
#define CPRINTS(format, args...) cprints(CC_HOOK, format, ## args)
#else /* CONFIG_COMMON_RUNTIME */
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/* Type-C Layer Flags */
#define TC_FLAGS_VCONN_ON           (1 << 0)

#define SUPPORT_TIMER_RESET_INIT     0
#define SUPPORT_TIMER_RESET_REQUEST  1
#define SUPPORT_TIMER_RESET_COMPLETE 2

/**
 * This is the Type-C Port object that contains information needed to
 * implement a Charge Through VCONN Powered Device.
 */
struct type_c tc[CONFIG_USB_PD_PORT_COUNT];

/* Type-C states */
DECLARE_STATE(tc, disabled, WITH_RUN, WITH_EXIT);
DECLARE_STATE(tc, error_recovery, WITH_RUN, NOOP);
DECLARE_STATE(tc, unattached_snk, WITH_RUN, NOOP);
DECLARE_STATE(tc, attach_wait_snk, WITH_RUN, NOOP);
DECLARE_STATE(tc, attached_snk, WITH_RUN, WITH_EXIT);
DECLARE_STATE(tc, try_snk, WITH_RUN, NOOP);
DECLARE_STATE(tc, unattached_src, WITH_RUN, NOOP);
DECLARE_STATE(tc, attach_wait_src, WITH_RUN, NOOP);
DECLARE_STATE(tc, try_wait_src, WITH_RUN, NOOP);
DECLARE_STATE(tc, attached_src, WITH_RUN, NOOP);
DECLARE_STATE(tc, ct_try_snk, WITH_RUN, WITH_EXIT);
DECLARE_STATE(tc, ct_attach_wait_unsupported, WITH_RUN, WITH_EXIT);
DECLARE_STATE(tc, ct_attached_unsupported, WITH_RUN, WITH_EXIT);
DECLARE_STATE(tc, ct_unattached_unsupported, WITH_RUN, WITH_EXIT);
DECLARE_STATE(tc, ct_unattached_vpd, WITH_RUN, WITH_EXIT);
DECLARE_STATE(tc, ct_disabled_vpd, WITH_RUN, NOOP);
DECLARE_STATE(tc, ct_attached_vpd, WITH_RUN, NOOP);
DECLARE_STATE(tc, ct_attach_wait_vpd, WITH_RUN, WITH_EXIT);

/* Super States */
DECLARE_STATE(tc, host_rard_ct_rd, NOOP, NOOP);
DECLARE_STATE(tc, host_open_ct_open, NOOP, NOOP);
DECLARE_STATE(tc, vbus_cc_iso, NOOP, NOOP);
DECLARE_STATE(tc, host_rp3_ct_rd, NOOP, NOOP);
DECLARE_STATE(tc, host_rp3_ct_rpu, NOOP, NOOP);
DECLARE_STATE(tc, host_rpu_ct_rd, NOOP, NOOP);

void tc_reset_support_timer(int port)
{
	tc[port].support_timer_reset |= SUPPORT_TIMER_RESET_REQUEST;
}

void tc_state_init(int port, enum typec_state_id start_state)
{
	int res = 0;
	sm_state this_state;

	res = tc_restart_tcpc(port);
	if (res)
		this_state = tc_disabled;
	else
		this_state = (start_state == TC_UNATTACHED_SRC) ?
				tc_unattached_src : tc_unattached_snk;

	CPRINTS("TCPC p%d init %s", port, res ? "failed" : "ready");

	sm_init_state(port, TC_OBJ(port), this_state);

	/* Disable pd state machines */
	tc[port].pd_enable = 0;
	tc[port].evt_timeout = 10*MSEC;
	tc[port].power_role = PD_PLUG_CABLE_VPD;
	tc[port].data_role = 0; /* Reserved for VPD */
	tc[port].billboard_presented = 0;
	tc[port].flags = 0;
}

void tc_event_check(int port, int evt)
{
	/* Do Nothing */
}

/**
 * Disabled
 *
 * Super State Entry Actions:
 *   Isolate the Host-side port from the Charge-Through port
 *   Enable mcu communication
 *   Remove the terminations from Host
 *   Remove the terminations from Charge-Through
 */
static int tc_disabled(int port, enum sm_signal sig)
{
	int ret = 0;

	ret = (*tc_disabled_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_open_ct_open);
}

static int tc_disabled_entry(int port)
{
	tc[port].state_id = TC_DISABLED;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);
	return 0;
}

static int tc_disabled_run(int port)
{
	task_wait_event(-1);
	return SM_RUN_SUPER;
}

static int tc_disabled_exit(int port)
{
#ifndef CONFIG_USB_PD_TCPC
	if (tc_restart_tcpc(port) != 0) {
		CPRINTS("TCPC p%d restart failed!", port);
		return 0;
	}
#endif
	CPRINTS("TCPC p%d resumed!", port);
	sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	return 0;
}

/**
 * ErrorRecovery
 *
 * Super State Entry Actions:
 *   Isolate the Host-side port from the Charge-Through port
 *   Enable mcu communication
 *   Remove the terminations from Host
 *   Remove the terminations from Charge-Through
 */
static int tc_error_recovery(int port, enum sm_signal sig)
{
	int ret = 0;

	ret = (*tc_error_recovery_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_open_ct_open);
}

static int tc_error_recovery_entry(int port)
{
	tc[port].state_id = TC_ERROR_RECOVERY;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);
	/* Use cc_debounce state variable for error recovery timeout */
	tc[port].cc_debounce = get_time().val + PD_T_ERROR_RECOVERY;
	return 0;
}

static int tc_error_recovery_run(int port)
{
	if (get_time().val > tc[port].cc_debounce)
		return sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	return SM_RUN_SUPER;
}

/**
 * Unattached.SNK
 *
 * Super State Entry Actions:
 *   Isolate the Host-side port from the Charge-Through port
 *   Enable mcu communication
 *   Place Ra on VCONN and Rd on Host CC
 *   Place Rd on Charge-Through CCs
 */
static int tc_unattached_snk(int port, enum sm_signal sig)
{
	int ret = 0;

	ret = (*tc_unattached_snk_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_rard_ct_rd);
}

static int tc_unattached_snk_entry(int port)
{
	tc[port].state_id = TC_UNATTACHED_SNK;
	if (tc[port].obj.last_state != tc_unattached_src)
		CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	tc[port].flags &= ~TC_FLAGS_VCONN_ON;
	tc[port].cc_state = PD_CC_UNSET;

	return 0;
}

static int tc_unattached_snk_run(int port)
{
	int host_cc;
	int new_cc_state;
	int cc1;
	int cc2;

	/* Check Host CC for connection */
	vpd_host_get_cc(&host_cc);

	/*
	 * Transition to AttachWait.SNK when a Source connection is
	 * detected, as indicated by the SNK.Rp state on its Host-side
	 * port’s CC pin.
	 */
	if (cc_is_rp(host_cc))
		return sm_set_state(port, TC_OBJ(port), tc_attach_wait_snk);

	/* Check Charge-Through CCs for connection */
	vpd_ct_get_cc(&cc1, &cc2);

	if (cc_is_rp(cc1) != cc_is_rp(cc2))
		new_cc_state = PD_CC_DFP_ATTACHED;
	else
		new_cc_state = PD_CC_NONE;

	/* Debounce Charge-Through CC state */
	if (tc[port].cc_state != new_cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_CC_DEBOUNCE;
	}

	/* If we are here, Host CC must be open */

	/* Wait for Charge-Through CC debounce */
	if (get_time().val < tc[port].cc_debounce)
		return 0;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * Unattached.SRC when the state of the Host-side port’s CC pin is
	 * SNK.Open for tDRP − dcSRC.DRP ∙ tDRP and both of the following
	 * is detected on the Charge-Through port.
	 *   1) SNK.Rp state is detected on exactly one of the CC1 or CC2
	 *      pins for at least tCCDebounce
	 *   2) VBUS is detected
	 */
	if (vpd_is_ct_vbus_present() &&
				tc[port].cc_state == PD_CC_DFP_ATTACHED)
		return sm_set_state(port, TC_OBJ(port), tc_unattached_src);

	return SM_RUN_SUPER;
}

/**
 * AttachWait.SNK
 *
 * Super State Entry Actions:
 *   Isolate the Host-side port from the Charge-Through port
 *   Enable mcu communication
 *   Place Ra on VCONN and Rd on Host CC
 *   Place Rd on Charge-Through CCs
 */
static int tc_attach_wait_snk(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_attach_wait_snk_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_rard_ct_rd);
}

static int tc_attach_wait_snk_entry(int port)
{
	tc[port].state_id = TC_ATTACH_WAIT_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);
	tc[port].host_cc_state = PD_CC_UNSET;

	return 0;
}

static int tc_attach_wait_snk_run(int port)
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
			tc[port].host_cc_debounce = get_time().val +
							PD_T_CC_DEBOUNCE;
		else
			tc[port].host_cc_debounce = get_time().val +
							PD_T_PD_DEBOUNCE;
		return 0;
	}

	/* Wait for Host CC debounce */
	if (get_time().val < tc[port].host_cc_debounce)
		return 0;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * Attached.SNK after the state of the Host-side port’s CC pin is
	 * SNK.Rp for at least tCCDebounce and either host-side VCONN or
	 * VBUS is detected.
	 *
	 * Transition to Unattached.SNK when the state of both the CC1 and
	 * CC2 pins is SNK.Open for at least tPDDebounce.
	 */
	if (tc[port].host_cc_state == PD_CC_DFP_ATTACHED &&
			(vpd_is_vconn_present() || vpd_is_host_vbus_present()))
		sm_set_state(port, TC_OBJ(port), tc_attached_snk);
	else if (tc[port].host_cc_state == PD_CC_NONE)
		sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	return 0;
}

/**
 * Attached.SNK
 */
static int tc_attached_snk(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_attached_snk_sig[sig])(port);
	return SM_SUPER(ret, sig, 0);
}

static int tc_attached_snk_entry(int port)
{
	tc[port].state_id = TC_ATTACHED_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	/*
	 * This state can only be entered from states AttachWait.SNK
	 * and Try.SNK. So the Host port is isolated from the
	 * Charge-Through port. We only need to High-Z the
	 * Charge-Through ports CC1 and CC2 pins.
	 */
	vpd_ct_set_pull(TYPEC_CC_OPEN, 0);

	tc[port].host_cc_state = PD_CC_UNSET;

	/* Start Charge-Through support timer */
	tc[port].support_timer_reset = SUPPORT_TIMER_RESET_INIT;
	tc[port].support_timer = get_time().val + PD_T_AME;

	/* Sample host CC every 2ms */
	tc_set_timeout(port, 2*MSEC);

	return 0;
}

static int tc_attached_snk_run(int port)
{
	int host_new_cc_state;
	int host_cc;

	/* Has host vbus and vconn been removed */
	if (!vpd_is_host_vbus_present() && !vpd_is_vconn_present())
		return sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	/*
	 * Reset the Charge-Through Support Timer when it first
	 * receives any USB PD Structured VDM Command it supports,
	 * which is the Discover Identity command. And this is only
	 * done one time.
	 */
	if (tc[port].support_timer_reset == SUPPORT_TIMER_RESET_REQUEST) {
		tc[port].support_timer_reset |= SUPPORT_TIMER_RESET_COMPLETE;
		tc[port].support_timer = get_time().val + PD_T_AME;
	}

	/* Check Host CC for connection */
	vpd_host_get_cc(&host_cc);

	if (cc_is_rp(host_cc))
		host_new_cc_state = PD_CC_DFP_ATTACHED;
	else
		host_new_cc_state = PD_CC_NONE;

	/* Debounce the Host CC state */
	if (tc[port].host_cc_state != host_new_cc_state) {
		tc[port].host_cc_state = host_new_cc_state;
		tc[port].host_cc_debounce = get_time().val + PD_T_VPDCTDD;
		return 0;
	}

	/* Wait for Host CC debounce */
	if (get_time().val < tc[port].host_cc_debounce)
		return 0;

	if (vpd_is_vconn_present()) {
		if (!(tc[port].flags & TC_FLAGS_VCONN_ON)) {
			/* VCONN detected. Remove RA */
			vpd_host_set_pull(TYPEC_CC_RD, 0);
			tc[port].flags |= TC_FLAGS_VCONN_ON;
		}

		/*
		 * A Charge-Through VCONN-Powered USB Device shall transition
		 * to CTUnattached.VPD if VCONN is present and the state of
		 * its Host-side port’s CC pin is SNK.Open for tVPDCTDD.
		 */
		if (tc[port].host_cc_state == PD_CC_NONE)
			return sm_set_state(port, TC_OBJ(port),
							tc_ct_unattached_vpd);
	}

	/* Check the Support Timer */
	if (get_time().val > tc[port].support_timer &&
					!tc[port].billboard_presented) {
		/*
		 * Present USB Billboard Device Class interface
		 * indicating that Charge-Through is not supported
		 */
		tc[port].billboard_presented = 1;
		vpd_present_billboard(BB_SNK);
	}

	return 0;
}

static int tc_attached_snk_exit(int port)
{
	/* Reset timeout value to 10ms */
	tc_set_timeout(port, 10*MSEC);
	tc[port].billboard_presented = 0;
	vpd_present_billboard(BB_NONE);

	return 0;
}

/**
 * Super State HOST_RA_CT_RD
 */
static int tc_host_rard_ct_rd(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_host_rard_ct_rd_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_vbus_cc_iso);
}

static int tc_host_rard_ct_rd_entry(int port)
{
	/* Place Ra on VCONN and Rd on Host CC */
	vpd_host_set_pull(TYPEC_CC_RA_RD, 0);

	/* Place Rd on Charge-Through CCs */
	vpd_ct_set_pull(TYPEC_CC_RD, 0);

	return 0;
}

/**
 * Super State HOST_OPEN_CT_OPEN
 */
static int tc_host_open_ct_open(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_host_open_ct_open_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_vbus_cc_iso);
}

static int tc_host_open_ct_open_entry(int port)
{
	/* Remove the terminations from Host */
	vpd_host_set_pull(TYPEC_CC_OPEN, 0);

	/* Remove the terminations from Charge-Through */
	vpd_ct_set_pull(TYPEC_CC_OPEN, 0);

	return 0;
}

/**
 * Super State VBUS_CC_ISO
 */
static int tc_vbus_cc_iso(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_vbus_cc_iso_sig[sig])(port);
	return SM_SUPER(ret, sig, 0);
}

static int tc_vbus_cc_iso_entry(int port)
{
	/* Isolate the Host-side port from the Charge-Through port */
	vpd_vbus_pass_en(0);

	/* Remove Charge-Through side port CCs */
	vpd_ct_cc_sel(CT_OPEN);

	/* Enable mcu communication and cc */
	vpd_mcu_cc_en(1);

	return 0;
}

/**
 * Unattached.SRC
 *
 * Super State Entry Actions:
 *   Isolate the Host-side port from the Charge-Through port
 *   Enable mcu communication
 *   Place RpUSB on Host CC
 *   Place Rd on Charge-Through CCs
 */
static int tc_unattached_src(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_unattached_src_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_rpu_ct_rd);
}

static int tc_unattached_src_entry(int port)
{
	tc[port].state_id = TC_UNATTACHED_SRC;
	if (tc[port].obj.last_state != tc_unattached_snk)
		CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Get power from VBUS */
	vpd_vconn_pwr_sel_odl(PWR_VBUS);

	/* Make sure it's the Charge-Through Port's VBUS */
	if (!vpd_is_ct_vbus_present())
		return sm_set_state(port, TC_OBJ(port), tc_error_recovery);

	tc[port].next_role_swap = get_time().val + PD_T_DRP_SRC;

	return 0;
}

static int tc_unattached_src_run(int port)
{
	int host_cc;

	/* Check Host CC for connection */
	vpd_host_get_cc(&host_cc);

	/*
	 * Transition to AttachWait.SRC when host-side VBUS is
	 * vSafe0V and SRC.Rd state is detected on the Host-side
	 * port’s CC pin.
	 */
	if (!vpd_is_host_vbus_present() && host_cc == TYPEC_CC_VOLT_RD)
		return sm_set_state(port, TC_OBJ(port), tc_attach_wait_src);

	/*
	 * Transition to Unattached.SNK within tDRPTransition or
	 * if Charge-Through VBUS is removed.
	 */
	if (!vpd_is_ct_vbus_present() ||
				get_time().val > tc[port].next_role_swap)
		return sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	return SM_RUN_SUPER;
}

/**
 * AttachWait.SRC
 *
 * Super State Entry Actions:
 *   Isolate the Host-side port from the Charge-Through port
 *   Enable mcu communication
 *   Place RpUSB on Host CC
 *   Place Rd on Charge-Through CCs
 */
static int tc_attach_wait_src(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_attach_wait_src_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_rpu_ct_rd);
}

static int tc_attach_wait_src_entry(int port)
{
	tc[port].state_id = TC_ATTACH_WAIT_SRC;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	tc[port].host_cc_state = PD_CC_UNSET;

	return 0;
}

static int tc_attach_wait_src_run(int port)
{
	int host_new_cc_state;
	int host_cc;

	/* Check Host CC for connection */
	vpd_host_get_cc(&host_cc);

	if (host_cc == TYPEC_CC_VOLT_RD)
		host_new_cc_state = PD_CC_UFP_ATTACHED;
	else
		host_new_cc_state = PD_CC_NONE;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition
	 * to Unattached.SNK when the SRC.Open state is detected on the
	 * Host-side port’s CC or if Charge-Through VBUS falls below
	 * vSinkDisconnect. The Charge-Through VCONN-Powered USB Device
	 * shall detect the SRC.Open state within tSRCDisconnect, but
	 * should detect it as quickly as possible.
	 */
	if (host_new_cc_state == PD_CC_NONE || !vpd_is_ct_vbus_present())
		return sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	/* Debounce the Host CC state */
	if (tc[port].host_cc_state != host_new_cc_state) {
		tc[port].host_cc_state = host_new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_CC_DEBOUNCE;
		return 0;
	}

	/* Wait for Host CC debounce */
	if (get_time().val < tc[port].cc_debounce)
		return 0;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * Try.SNK when the host-side VBUS is at vSafe0V and the SRC.Rd
	 * state is on the Host-side port’s CC pin for at least tCCDebounce.
	 */
	if (tc[port].host_cc_state == PD_CC_UFP_ATTACHED &&
						!vpd_is_host_vbus_present())
		return sm_set_state(port, TC_OBJ(port), tc_try_snk);

	return SM_RUN_SUPER;
}

/**
 * Attached.SRC
 */
static int tc_attached_src(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_attached_src_sig[sig])(port);
	return SM_SUPER(ret, sig, 0);
}

static int tc_attached_src_entry(int port)
{
	tc[port].state_id = TC_ATTACHED_SRC;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	/* Connect Charge-Through VBUS to Host VBUS */
	vpd_vbus_pass_en(1);

	/*
	 * Get power from VBUS. No need to test because
	 * the Host VBUS is connected to the Charge-Through
	 * VBUS
	 */
	vpd_vconn_pwr_sel_odl(PWR_VBUS);

	return 0;
}

static int tc_attached_src_run(int port)
{
	int host_cc;

	/* Check Host CC for connection */
	vpd_host_get_cc(&host_cc);

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * Unattached.SNK when VBUS falls below vSinkDisconnect or the
	 * Host-side port’s CC pin is SRC.Open. The Charge-Through
	 * VCONNPowered USB Device shall detect the SRC.Open state within
	 * tSRCDisconnect, but should detect it as quickly as possible.
	 */
	if (!vpd_is_ct_vbus_present() || host_cc == TYPEC_CC_VOLT_OPEN)
		sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	return 0;
}

/**
 * Super State HOST_RPU_CT_RD
 */
static int tc_host_rpu_ct_rd(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_host_rpu_ct_rd_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_vbus_cc_iso);
}

static int tc_host_rpu_ct_rd_entry(int port)
{
	/* Place RpUSB on Host CC */
	vpd_host_set_pull(TYPEC_CC_RP, TYPEC_RP_USB);

	/* Place Rd on Charge-Through CCs */
	vpd_ct_set_pull(TYPEC_CC_RD, 0);

	return 0;
}

/**
 * Try.SNK
 *
 * Super State Entry Actions:
 *   Isolate the Host-side port from the Charge-Through port
 *   Enable mcu communication
 *   Place Ra on VCONN and Rd on Host CC
 *   Place Rd on Charge-Through CCs
 */
static int tc_try_snk(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_try_snk_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_rard_ct_rd);
}

static int tc_try_snk_entry(int port)
{
	tc[port].state_id = TC_TRY_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Get power from VBUS */
	vpd_vconn_pwr_sel_odl(PWR_VBUS);

	/* Make sure it's the Charge-Through Port's VBUS */
	if (!vpd_is_ct_vbus_present())
		return sm_set_state(port, TC_OBJ(port), tc_error_recovery);

	tc[port].host_cc_state = PD_CC_UNSET;

	/* Using next_role_swap timer as try_src timer */
	tc[port].next_role_swap = get_time().val + PD_T_DRP_TRY;

	return 0;
}

static int tc_try_snk_run(int port)
{
	int host_new_cc_state;
	int host_cc;

	/*
	 * Wait for tDRPTry before monitoring the Charge-Through
	 * port’s CC pins for the SNK.Rp
	 */
	if (get_time().val < tc[port].next_role_swap)
		return 0;

	/* Check Host CC for connection */
	vpd_host_get_cc(&host_cc);

	if (cc_is_rp(host_cc))
		host_new_cc_state = PD_CC_DFP_ATTACHED;
	else
		host_new_cc_state = PD_CC_NONE;

	/* Debounce the Host CC state */
	if (tc[port].host_cc_state != host_new_cc_state) {
		tc[port].host_cc_state = host_new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_DEBOUNCE;
		return 0;
	}

	/* Wait for Host CC debounce */
	if (get_time().val < tc[port].cc_debounce)
		return 0;

	/*
	 * The Charge-Through VCONN-Powered USB Device shall then transition to
	 * Attached.SNK when the SNK.Rp state is detected on the Host-side
	 * port’s CC pin for at least tTryCCDebounce and VBUS or VCONN is
	 * detected on Host-side port.
	 *
	 * Alternatively, the Charge-Through VCONN-Powered USB Device shall
	 * transition to TryWait.SRC if Host-side SNK.Rp state is not detected
	 * for tTryCCDebounce.
	 */
	if (tc[port].host_cc_state == PD_CC_DFP_ATTACHED &&
			(vpd_is_host_vbus_present() || vpd_is_vconn_present()))
		sm_set_state(port, TC_OBJ(port), tc_attached_snk);
	else if (tc[port].host_cc_state == PD_CC_NONE)
		sm_set_state(port, TC_OBJ(port), tc_try_wait_src);

	return 0;
}

/**
 * TryWait.SRC
 *
 * Super State Entry Actions:
 *   Isolate the Host-side port from the Charge-Through port
 *   Enable mcu communication
 *   Place RpUSB on Host CC
 *   Place Rd on Charge-Through CCs
 */
static int tc_try_wait_src(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_try_wait_src_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_rpu_ct_rd);
}

static int tc_try_wait_src_entry(int port)
{
	tc[port].state_id = TC_TRY_WAIT_SRC;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	tc[port].host_cc_state = PD_CC_UNSET;
	tc[port].next_role_swap = get_time().val + PD_T_DRP_TRY;

	return 0;
}

static int tc_try_wait_src_run(int port)
{
	int host_new_cc_state;
	int host_cc;

	/* Check Host CC for connection */
	vpd_host_get_cc(&host_cc);

	if (host_cc == TYPEC_CC_VOLT_RD)
		host_new_cc_state = PD_CC_UFP_ATTACHED;
	else
		host_new_cc_state = PD_CC_NONE;

	/* Debounce the Host CC state */
	if (tc[port].host_cc_state != host_new_cc_state) {
		tc[port].host_cc_state = host_new_cc_state;
		tc[port].host_cc_debounce =
					get_time().val + PD_T_TRY_CC_DEBOUNCE;
		return 0;
	}

	if (get_time().val > tc[port].host_cc_debounce) {
		/*
		 * A Charge-Through VCONN-Powered USB Device shall transition
		 * to Attached.SRC when host-side VBUS is at vSafe0V and the
		 * SRC.Rd state is detected on the Host-side port’s CC pin for
		 * at least tTryCCDebounce.
		 */
		if (tc[port].host_cc_state == PD_CC_UFP_ATTACHED &&
						!vpd_is_host_vbus_present())
			return sm_set_state(port, TC_OBJ(port),
							tc_attached_src);
	}

	if (get_time().val > tc[port].next_role_swap) {
		/*
		 * The Charge-Through VCONN-Powered USB Device shall transition
		 * to Unattached.SNK after tDRPTry if the Host-side port’s CC
		 * pin is not in the SRC.Rd state.
		 */
		if (tc[port].host_cc_state == PD_CC_NONE)
			return sm_set_state(port, TC_OBJ(port),
							tc_unattached_snk);
	}

	return SM_RUN_SUPER;
}

/**
 * CTTry.SNK
 *
 * Super State Entry Actions:
 *   Isolate the Host-side port from the Charge-Through port
 *   Enable mcu communication
 *   Place RP3A0 on Host CC
 *   Connect Charge-Through Rd
 *   Get power from VCONN
 */
static int tc_ct_try_snk(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_ct_try_snk_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_rp3_ct_rd);
}

static int tc_ct_try_snk_entry(int port)
{
	tc[port].state_id = TC_CTTRY_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	tc[port].cc_state = PD_CC_UNSET;
	tc[port].next_role_swap = get_time().val + PD_T_DRP_TRY;

	return 0;
}

static int tc_ct_try_snk_run(int port)
{
	int new_cc_state;
	int cc1;
	int cc2;

	/*
	 * Wait for tDRPTry before monitoring the Charge-Through
	 * port’s CC pins for the SNK.Rp
	 */
	if (get_time().val < tc[port].next_role_swap)
		return 0;

	/* Check CT CC for connection */
	vpd_ct_get_cc(&cc1, &cc2);

	if (cc_is_rp(cc1) || cc_is_rp(cc2))
		new_cc_state = PD_CC_DFP_ATTACHED;
	else
		new_cc_state = PD_CC_NONE;

	/*
	 * The Charge-Through VCONN-Powered USB Device shall transition
	 * to Unattached.SNK if VCONN falls below vVCONNDisconnect.
	 */
	if (!vpd_is_vconn_present())
		return sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	/* Debounce the CT CC state */
	if (tc[port].cc_state != new_cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_DEBOUNCE;
		tc[port].try_wait_debounce = get_time().val + PD_T_TRY_WAIT;

		return 0;
	}

	if (get_time().val > tc[port].cc_debounce) {
		/*
		 * The Charge-Through VCONN-Powered USB Device shall then
		 * transition to CTAttached.VPD when the SNK.Rp state is
		 * detected on the Charge-Through port’s CC pins for at
		 * least tTryCCDebounce and VBUS is detected on
		 * Charge-Through port.
		 */
		if (tc[port].cc_state == PD_CC_DFP_ATTACHED &&
				vpd_is_ct_vbus_present())
			return sm_set_state(port, TC_OBJ(port),
							tc_ct_attached_vpd);
	}

	if (get_time().val > tc[port].try_wait_debounce) {
		/*
		 * A Charge-Through VCONN-Powered USB Device shall transition
		 * to CTAttached.Unsupported if SNK.Rp state is not detected
		 * for tDRPTryWait.
		 */
		if (tc[port].cc_state == PD_CC_NONE)
			return sm_set_state(port, TC_OBJ(port),
					tc_ct_attached_unsupported);
	}

	return SM_RUN_SUPER;
}

static int tc_ct_try_snk_exit(int port)
{
	/* Disable PD */
	tc[port].pd_enable = 0;

	return 0;
}

/**
 * CTAttachWait.Unsupported
 *
 *  Super State Entry Actions:
 *    Isolate the Host-side port from the Charge-Through port
 *    Enable mcu communication
 *    Place RP3A0 on Host CC
 *    Place RPUSB on Charge-Through CC
 *    Get power from VCONN
 */
static int tc_ct_attach_wait_unsupported(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_ct_attach_wait_unsupported_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_rp3_ct_rpu);
}

static int tc_ct_attach_wait_unsupported_entry(int port)
{
	tc[port].state_id = TC_CTATTACH_WAIT_UNSUPPORTED;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	tc[port].cc_state = PD_CC_UNSET;

	return 0;
}

static int tc_ct_attach_wait_unsupported_run(int port)
{
	int new_cc_state;
	int cc1;
	int cc2;

	/* Check CT CC for connection */
	vpd_ct_get_cc(&cc1, &cc2);

	if (cc_is_at_least_one_rd(cc1, cc2))
		new_cc_state = PD_CC_DFP_ATTACHED;
	else if (cc_is_audio_acc(cc1, cc2))
		new_cc_state = PD_CC_AUDIO_ACC;
	else /* (cc1 == TYPEC_CC_VOLT_OPEN or cc2 == TYPEC_CC_VOLT_OPEN */
		new_cc_state = PD_CC_NONE;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * Unattached.SNK if VCONN falls below vVCONNDisconnect.
	 */
	if (!vpd_is_vconn_present())
		return sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	/* Debounce the cc state */
	if (tc[port].cc_state != new_cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_CC_DEBOUNCE;
		return 0;
	}

	/* Wait for CC debounce */
	if (get_time().val < tc[port].cc_debounce)
		return 0;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * CTUnattached.VPD when the state of either the Charge-Through
	 * Port’s CC1 or CC2 pin is SRC.Open for at least tCCDebounce.
	 *
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * CTTry.SNK if the state of at least one of the Charge-Through
	 * port’s CC pins is SRC.Rd, or if the state of both the CC1 and CC2
	 * pins is SRC.Ra. for at least tCCDebounce.
	 */
	if (new_cc_state == PD_CC_NONE)
		sm_set_state(port, TC_OBJ(port), tc_ct_unattached_vpd);
	else /* PD_CC_DFP_ATTACHED or PD_CC_AUDIO_ACC */
		sm_set_state(port, TC_OBJ(port), tc_ct_try_snk);

	return 0;
}

static int tc_ct_attach_wait_unsupported_exit(int port)
{
	/* Disable PD */
	tc[port].pd_enable = 0;

	return 0;
}

/**
 * CTAttached.Unsupported
 *
 *  Super State Entry Actions:
 *    Isolate the Host-side port from the Charge-Through port
 *    Enable mcu communication
 *    Place RP3A0 on Host CC
 *    Place RPUSB on Charge-Through CC
 *    Get power from VCONN
 */
static int tc_ct_attached_unsupported(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_ct_attached_unsupported_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_rp3_ct_rpu);
}

static int tc_ct_attached_unsupported_entry(int port)
{
	tc[port].state_id = TC_CTATTACHED_UNSUPPORTED;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Present Billboard device */
	vpd_present_billboard(BB_SNK);

	return 0;
}

static int tc_ct_attached_unsupported_run(int port)
{
	int cc1;
	int cc2;

	/* Check CT CC for connection */
	vpd_ct_get_cc(&cc1, &cc2);

	if (!vpd_is_vconn_present())
		return sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	/*
	 * The Charge-Through VCONN-Powered USB Device shall transition to
	 * CTUnattached.VPD when SRC.Open state is detected on both the
	 * Charge-Through port’s CC pins or the SRC.Open state is detected
	 * on one CC pin and SRC.Ra is detected on the other CC pin.
	 */
	if ((cc1 == TYPEC_CC_VOLT_OPEN && cc2 == TYPEC_CC_VOLT_OPEN) ||
	    (cc1 == TYPEC_CC_VOLT_OPEN && cc2 == TYPEC_CC_VOLT_RA) ||
	    (cc1 == TYPEC_CC_VOLT_RA && cc2 == TYPEC_CC_VOLT_OPEN))
		return sm_set_state(port, TC_OBJ(port), tc_ct_unattached_vpd);

	return SM_RUN_SUPER;
}

static int tc_ct_attached_unsupported_exit(int port)
{
	vpd_present_billboard(BB_NONE);

	return 0;
}

/**
 * CTUnattached.Unsupported
 *
 *  Super State Entry Actions:
 *    Isolate the Host-side port from the Charge-Through port
 *    Enable mcu communication
 *    Place RP3A0 on Host CC
 *    Place RPUSB on Charge-Through CC
 *    Get power from VCONN
 */
static int tc_ct_unattached_unsupported(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_ct_unattached_unsupported_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_rp3_ct_rpu);
}

static int tc_ct_unattached_unsupported_entry(int port)
{
	tc[port].state_id = TC_CTUNATTACHED_UNSUPPORTED;
	if (tc[port].obj.last_state != tc_ct_unattached_vpd)
		CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	tc[port].next_role_swap = get_time().val + PD_T_DRP_SRC;

	return 0;
}

static int tc_ct_unattached_unsupported_run(int port)
{
	int cc1;
	int cc2;

	/* Check CT CC for connection */
	vpd_ct_get_cc(&cc1, &cc2);

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * CTAttachWait.Unsupported when a Sink connection is detected on
	 * the Charge-Through port, as indicated by the SRC.Rd state on at
	 * least one of the Charge-Through port’s CC pins or SRC.Ra state
	 * on both the CC1 and CC2 pins.
	 */
	if (cc_is_at_least_one_rd(cc1, cc2) || cc_is_audio_acc(cc1, cc2))
		return sm_set_state(port, TC_OBJ(port),
				tc_ct_attach_wait_unsupported);

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * Unattached.SNK if VCONN falls below vVCONNDisconnect.
	 */
	if (!vpd_is_vconn_present())
		return sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * CTUnattached.VPD within tDRPTransition after dcSRC.DRP ∙ tDRP.
	 */
	if (get_time().val > tc[port].next_role_swap)
		return sm_set_state(port, TC_OBJ(port), tc_ct_unattached_vpd);

	return SM_RUN_SUPER;
}

static int tc_ct_unattached_unsupported_exit(int port)
{
	/* Disable PD */
	tc[port].pd_enable = 0;

	return 0;
}

/**
 * CTUnattached.VPD
 *
 *  Super State Entry Actions:
 *    Isolate the Host-side port from the Charge-Through port
 *    Enable mcu communication
 *    Place RP3A0 on Host CC
 *    Connect Charge-Through Rd
 *    Get power from VCONN
 */
static int tc_ct_unattached_vpd(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_ct_unattached_vpd_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_rp3_ct_rd);
}

static int tc_ct_unattached_vpd_entry(int port)
{
	tc[port].state_id = TC_CTUNATTACHED_VPD;
	if (tc[port].obj.last_state != tc_ct_unattached_unsupported)
		CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	tc[port].cc_state = PD_CC_UNSET;

	return 0;
}

static int tc_ct_unattached_vpd_run(int port)
{
	int new_cc_state;
	int cc1;
	int cc2;

	/* Check CT CC for connection */
	vpd_ct_get_cc(&cc1, &cc2);

	if (cc_is_rp(cc1) != cc_is_rp(cc2))
		new_cc_state = PD_CC_DFP_ATTACHED;
	else if (!cc_is_rp(cc1) && !cc_is_rp(cc2))
		new_cc_state = PD_CC_NONE;
	else
		new_cc_state = PD_CC_UNSET;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * CTAttachWait.VPD when a Source connection is detected on the
	 * Charge-Through port, as indicated by the SNK.Rp state on
	 * exactly one of the Charge-Through port’s CC pins.
	 */
	if (new_cc_state == PD_CC_DFP_ATTACHED)
		return sm_set_state(port, TC_OBJ(port),
					tc_ct_attach_wait_vpd);

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * Unattached.SNK if VCONN falls below vVCONNDisconnect.
	 */
	if (!vpd_is_vconn_present())
		return sm_set_state(port, TC_OBJ(port),
				tc_unattached_snk);

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_DRP_SRC;
		return 0;
	}

	if (get_time().val < tc[port].cc_debounce)
		return 0;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * CTUnattached.Unsupported within tDRPTransition after the state
	 * of both the Charge-Through port’s CC1 and CC2 pins is SNK.Open
	 * for tDRP-dcSRC.DRP ∙ tDRP, or if directed.
	 */
	if (tc[port].cc_state == PD_CC_NONE)
		return sm_set_state(port, TC_OBJ(port),
					tc_ct_unattached_unsupported);

	return SM_RUN_SUPER;
}

static int tc_ct_unattached_vpd_exit(int port)
{
	/* Disable PD */
	tc[port].pd_enable = 0;

	return 0;
}

/**
 * CTDisabled.VPD
 *
 * Super State Entry Actions:
 *   Isolate the Host-side port from the Charge-Through port
 *   Enable mcu communication
 *   Remove the terminations from Host
 *   Remove the terminations from Charge-Through
 */
static int tc_ct_disabled_vpd(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_ct_disabled_vpd_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_open_ct_open);
}

static int tc_ct_disabled_vpd_entry(int port)
{
	tc[port].state_id = TC_CTDISABLED_VPD;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Get power from VBUS */
	vpd_vconn_pwr_sel_odl(PWR_VBUS);

	tc[port].next_role_swap = get_time().val + PD_T_VPDDISABLE;

	return 0;
}

static int tc_ct_disabled_vpd_run(int port)
{
	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition
	 * to Unattached.SNK after tVPDDisable.
	 */
	if (get_time().val > tc[port].next_role_swap)
		sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	return 0;
}

/**
 * CTAttached.VPD
 */
static int tc_ct_attached_vpd(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_ct_attached_vpd_sig[sig])(port);
	return SM_SUPER(ret, sig, 0);
}

static int tc_ct_attached_vpd_entry(int port)
{
	int cc1;
	int cc2;

	tc[port].state_id = TC_CTATTACHED_VPD;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Get power from VCONN */
	vpd_vconn_pwr_sel_odl(PWR_VCONN);

	/*
	 * Detect which of the Charge-Through port’s CC1 or CC2
	 * pins is connected through the cable
	 */
	vpd_ct_get_cc(&cc1, &cc2);
	tc[port].ct_cc  = cc_is_rp(cc2) ? CT_CC2 : CT_CC1;

	/*
	 * 1. Remove or reduce any additional capacitance on the
	 *    Host-side CC port
	 */
	vpd_mcu_cc_en(0);

	/*
	 * 2. Disable the Rp termination advertising 3.0 A on the
	 *    host port’s CC pin
	 */
	vpd_host_set_pull(TYPEC_CC_OPEN, 0);

	/*
	 * 3. Passively multiplex the detected Charge-Through port’s
	 *    CC pin through to the host port’s CC
	 */
	vpd_ct_cc_sel(tc[port].ct_cc);

	/*
	 * 4. Disable the Rd on the Charge-Through port’s CC1 and CC2
	 *    pins
	 */
	vpd_ct_set_pull(TYPEC_CC_OPEN, 0);

	/*
	 * 5. Connect the Charge-Through port’s VBUS through to the
	 *    host port’s VBUS
	 */
	vpd_vbus_pass_en(1);

	tc[port].cc_state = PD_CC_UNSET;

	return 0;
}

static int tc_ct_attached_vpd_run(int port)
{
	int new_cc_state;
	int cc1;
	int cc2;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * CTDisabled.VPD if VCONN falls below vVCONNDisconnect.
	 */
	if (!vpd_is_vconn_present())
		return sm_set_state(port, TC_OBJ(port), tc_ct_disabled_vpd);

	/* Check CT CC for connection */
	vpd_ct_get_cc(&cc1, &cc2);
	if ((tc[port].ct_cc ? cc2 : cc1) == TYPEC_CC_VOLT_OPEN)
		new_cc_state = PD_CC_NONE;
	else
		new_cc_state = PD_CC_DFP_ATTACHED;

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_VPDCTDD;
		return 0;
	}

	if (get_time().val < tc[port].pd_debounce)
		return 0;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * CTUnattached.VPD when VBUS falls below vSinkDisconnect and the
	 * state of the passed-through CC pin is SNK.Open for tVPDCTDD.
	 */
	if (tc[port].cc_state == PD_CC_NONE && !vpd_is_ct_vbus_present())
		sm_set_state(port, TC_OBJ(port), tc_ct_unattached_vpd);

	return 0;
}

/**
 * CTAttachWait.VPD
 *
 *  Super State Entry Actions:
 *    Isolate the Host-side port from the Charge-Through port
 *    Enable mcu communication
 *    Place RP3A0 on Host CC
 *    Connect Charge-Through Rd
 *    Get power from VCONN
 */
static int tc_ct_attach_wait_vpd(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_ct_attach_wait_vpd_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_host_rp3_ct_rd);
}

static int tc_ct_attach_wait_vpd_entry(int port)
{
	tc[port].state_id = TC_CTATTACH_WAIT_VPD;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	tc[port].cc_state = PD_CC_UNSET;

	/* Sample CCs every 2ms */
	tc_set_timeout(port, 2 * MSEC);
	return 0;
}

static int tc_ct_attach_wait_vpd_run(int port)
{
	int new_cc_state;
	int cc1;
	int cc2;

	/* Check CT CC for connection */
	vpd_ct_get_cc(&cc1, &cc2);

	if (cc_is_rp(cc1) != cc_is_rp(cc2))
		new_cc_state = PD_CC_DFP_ATTACHED;
	else if (!cc_is_rp(cc1) && !cc_is_rp(cc2))
		new_cc_state = PD_CC_NONE;
	else
		new_cc_state = PD_CC_UNSET;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * CTDisabled.VPD if VCONN falls below vVCONNDisconnect.
	 */
	if (!vpd_is_vconn_present())
		return sm_set_state(port, TC_OBJ(port), tc_ct_disabled_vpd);

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val +
						PD_T_CC_DEBOUNCE;
		tc[port].pd_debounce = get_time().val +
						PD_T_PD_DEBOUNCE;
		return 0;
	}

	if (get_time().val > tc[port].pd_debounce) {
		/*
		 * A Charge-Through VCONN-Powered USB Device shall transition
		 * to CTUnattached.VPD when the state of both the Charge-Through
		 * port’s CC1 and CC2 pins are SNK.Open for at least
		 * tPDDebounce.
		 */
		if (tc[port].cc_state  == PD_CC_NONE)
			return sm_set_state(port, TC_OBJ(port),
						tc_ct_unattached_vpd);
	}

	if (get_time().val > tc[port].cc_debounce) {
		/*
		 * A Charge-Through VCONN-Powered USB Device shall transition to
		 * CTAttached.VPD after the state of only one of the
		 * Charge-Through port’s CC1 or CC2 pins is SNK.Rp for at
		 * least tCCDebounce and VBUS on the Charge-Through port is
		 * detected.
		 */
		if (tc[port].cc_state  == PD_CC_DFP_ATTACHED &&
						vpd_is_ct_vbus_present())
			return sm_set_state(port, TC_OBJ(port),
							tc_ct_attached_vpd);
	}

	return SM_RUN_SUPER;
}

static int tc_ct_attach_wait_vpd_exit(int port)
{
	/* Disable PD */
	tc[port].pd_enable = 0;

	/* Reset timeout value to 10ms */
	tc_set_timeout(port, 10 * MSEC);

	return 0;
}

/**
 * Super State HOST_RP3_CT_RD
 */
static int tc_host_rp3_ct_rd(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_host_rp3_ct_rd_sig[sig])(port);

	return SM_SUPER(ret, sig, tc_vbus_cc_iso);
}

static int tc_host_rp3_ct_rd_entry(int port)
{
	/* Place RP3A0 on Host CC */
	vpd_host_set_pull(TYPEC_CC_RP, TYPEC_RP_3A0);

	/* Connect Charge-Through Rd */
	vpd_ct_set_pull(TYPEC_CC_RD, 0);

	/*
	 * A Charge-Through VCONN-Powered USB Device shall
	 * ensure that it is powered by VCONN
	 */

	/* Make sure vconn is on */
	if (!vpd_is_vconn_present())
		sm_set_state(port, TC_OBJ(port), tc_error_recovery);

	/* Get power from VCONN */
	vpd_vconn_pwr_sel_odl(PWR_VCONN);

	return 0;
}

/**
 * Super State HOST_RP3_CT_RPU
 */
static int tc_host_rp3_ct_rpu(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_host_rp3_ct_rpu_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_vbus_cc_iso);
}

static int tc_host_rp3_ct_rpu_entry(int port)
{
	/* Place RP3A0 on Host CC */
	vpd_host_set_pull(TYPEC_CC_RP, TYPEC_RP_3A0);

	/* Place RPUSB on Charge-Through CC */
	vpd_ct_set_pull(TYPEC_CC_RP, TYPEC_RP_USB);

	/*
	 * A Charge-Through VCONN-Powered USB Device shall
	 * ensure that it is powered by VCONN
	 */

	/* Make sure vconn is on */
	if (!vpd_is_vconn_present())
		sm_set_state(port, TC_OBJ(port), tc_error_recovery);

	/* Get power from VCONN */
	vpd_vconn_pwr_sel_odl(PWR_VCONN);

	return 0;
}

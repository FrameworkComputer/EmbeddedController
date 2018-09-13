/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "vpd_api.h"

/* USB Type-C VCONN Powered Device module */

#ifndef __CROS_EC_USB_TC_VPD_H
#define __CROS_EC_USB_TC_VPD_H

/* Type-C Layer Flags */
#define TC_FLAGS_VCONN_ON           BIT(0)

#undef PD_DEFAULT_STATE
/* Port default state at startup */
#define PD_DEFAULT_STATE(port) tc_state_unattached_snk

/*
 * TC_OBJ is a convenience macro to access struct sm_obj, which
 * must be the first member of struct type_c.
 */
#define TC_OBJ(port)   (SM_OBJ(tc[port]))

/**
 * This is the Type-C Port object that contains information needed to
 * implement a VCONN Powered Device.
 */
static struct type_c {
	/*
	 * struct sm_obj must be first. This is the state machine
	 * object that keeps track of the current and last state
	 * of the state machine.
	 */
	struct sm_obj obj;
	/* state id */
	enum typec_state_id state_id;
	/* current port power role (VPD, SOURCE or SINK) */
	uint8_t power_role;
	/* current port data role (DFP or UFP) */
	uint8_t data_role;
	/* bool: enable power delivery state machines */
	uint8_t pd_enable;
	/* event timeout */
	uint64_t evt_timeout;
	/* state machine event */
	int evt;
	/* port flags, see TC_FLAGS_* */
	uint32_t flags;
	/* Time a port shall wait before it can determine it is attached */
	uint64_t cc_debounce;
	/* VPD host port cc state */
	enum pd_cc_states host_cc_state;
	uint8_t ct_cc;
} tc[CONFIG_USB_PD_PORT_COUNT];

/* Type-C states */
static unsigned int tc_state_disabled(int port, enum signal sig);
static unsigned int tc_state_disabled_entry(int port);
static unsigned int tc_state_disabled_run(int port);
static unsigned int tc_state_disabled_exit(int port);

static unsigned int tc_state_unattached_snk(int port, enum signal sig);
static unsigned int tc_state_unattached_snk_entry(int port);
static unsigned int tc_state_unattached_snk_run(int port);
static unsigned int tc_state_unattached_snk_exit(int port);

static unsigned int tc_state_attach_wait_snk(int port, enum signal sig);
static unsigned int tc_state_attach_wait_snk_entry(int port);
static unsigned int tc_state_attach_wait_snk_run(int port);
static unsigned int tc_state_attach_wait_snk_exit(int port);

static unsigned int tc_state_attached_snk(int port, enum signal sig);
static unsigned int tc_state_attached_snk_entry(int port);
static unsigned int tc_state_attached_snk_run(int port);
static unsigned int tc_state_attached_snk_exit(int port);

/* Super States */
static unsigned int tc_state_host_rard(int port, enum signal sig);
static unsigned int tc_state_host_rard_entry(int port);
static unsigned int tc_state_host_rard_run(int port);
static unsigned int tc_state_host_rard_exit(int port);

static unsigned int tc_state_host_open(int port, enum signal sig);
static unsigned int tc_state_host_open_entry(int port);
static unsigned int tc_state_host_open_run(int port);
static unsigned int tc_state_host_open_exit(int port);

static unsigned int tc_state_vbus_cc_iso(int port, enum signal sig);
static unsigned int tc_state_vbus_cc_iso_entry(int port);
static unsigned int tc_state_vbus_cc_iso_run(int port);
static unsigned int tc_state_vbus_cc_iso_exit(int port);

static unsigned int get_super_state(int port);

static const state_sig tc_state_disabled_sig[] = {
	tc_state_disabled_entry,
	tc_state_disabled_run,
	tc_state_disabled_exit,
	get_super_state
};

static const state_sig tc_state_unattached_snk_sig[] = {
	tc_state_unattached_snk_entry,
	tc_state_unattached_snk_run,
	tc_state_unattached_snk_exit,
	get_super_state
};

static const state_sig tc_state_attach_wait_snk_sig[] = {
	tc_state_attach_wait_snk_entry,
	tc_state_attach_wait_snk_run,
	tc_state_attach_wait_snk_exit,
	get_super_state
};

static const state_sig tc_state_attached_snk_sig[] = {
	tc_state_attached_snk_entry,
	tc_state_attached_snk_run,
	tc_state_attached_snk_exit,
	get_super_state
};

static const state_sig tc_state_host_rard_sig[] = {
	tc_state_host_rard_entry,
	tc_state_host_rard_run,
	tc_state_host_rard_exit,
	get_super_state
};

static const state_sig tc_state_host_open_sig[] = {
	tc_state_host_open_entry,
	tc_state_host_open_run,
	tc_state_host_open_exit,
	get_super_state
};

static const state_sig tc_state_vbus_cc_iso_sig[] = {
	tc_state_vbus_cc_iso_entry,
	tc_state_vbus_cc_iso_run,
	tc_state_vbus_cc_iso_exit,
	get_super_state
};

static void tc_state_init(int port)
{
	int res = 0;
	sm_state this_state;

	res = tc_restart_tcpc(port);

	CPRINTS("TCPC p%d init %s", port, res ? "failed" : "ready");
	this_state = res ? tc_state_disabled : PD_DEFAULT_STATE(port);

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

static void tc_event_check(int port, int evt)
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
static unsigned int tc_state_disabled(int port, enum signal sig)
{
	int ret = 0;

	ret = (*tc_state_disabled_sig[sig])(port);
	return SUPER(ret, sig, tc_state_host_open);
}

static unsigned int tc_state_disabled_entry(int port)
{
	tc[port].state_id = DISABLED;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	return 0;
}

static unsigned int tc_state_disabled_run(int port)
{
	task_wait_event(-1);

	return RUN_SUPER;
}

static unsigned int tc_state_disabled_exit(int port)
{
#ifndef CONFIG_USB_PD_TCPC
	if (tc_restart_tcpc(port) != 0) {
		CPRINTS("TCPC p%d restart failed!", port);
		return 0;
	}
#endif
	CPRINTS("TCPC p%d resumed!", port);
	set_state(port, TC_OBJ(port), tc_state_unattached_snk);

	return 0;
}

/**
 * Unattached.SNK
 *
 * Super State Entry:
 *   Enable mcu communication
 *   Place Ra on VCONN and Rd on Host CC
 */
static unsigned int tc_state_unattached_snk(int port, enum signal sig)
{
	int ret;

	ret = (*tc_state_unattached_snk_sig[sig])(port);
	return SUPER(ret, sig, tc_state_host_rard);
}

static unsigned int tc_state_unattached_snk_entry(int port)
{
	tc[port].state_id = UNATTACHED_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	return 0;
}

static unsigned int tc_state_unattached_snk_run(int port)
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
		set_state(port, TC_OBJ(port), tc_state_attach_wait_snk);
		return 0;
	}

	return RUN_SUPER;
}

static unsigned int tc_state_unattached_snk_exit(int port)
{
	return 0;
}

/**
 * AttachedWait.SNK
 *
 * Super State Entry:
 *   Enable mcu communication
 *   Place Ra on VCONN and Rd on Host CC
 */
static unsigned int tc_state_attach_wait_snk(int port, enum signal sig)
{
	int ret = 0;

	ret = (*tc_state_attach_wait_snk_sig[sig])(port);
	return SUPER(ret, sig, tc_state_host_rard);
}

static unsigned int tc_state_attach_wait_snk_entry(int port)
{
	tc[port].state_id = ATTACH_WAIT_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);
	tc[port].host_cc_state = PD_CC_UNSET;

	return 0;
}

static unsigned int tc_state_attach_wait_snk_run(int port)
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
		set_state(port, TC_OBJ(port), tc_state_attached_snk);
	else if (tc[port].host_cc_state == PD_CC_NONE)
		set_state(port, TC_OBJ(port), tc_state_unattached_snk);

	return 0;
}

static unsigned int tc_state_attach_wait_snk_exit(int port)
{
	return 0;
}

/**
 * Attached.SNK
 */
static unsigned int tc_state_attached_snk(int port, enum signal sig)
{
	int ret;

	ret = (*tc_state_attached_snk_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int tc_state_attached_snk_entry(int port)
{
	tc[port].state_id = ATTACHED_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	return 0;
}

static unsigned int tc_state_attached_snk_run(int port)
{
	/* Has host vbus and vconn been removed */
	if (!vpd_is_host_vbus_present() && !vpd_is_vconn_present()) {
		set_state(port, TC_OBJ(port), tc_state_unattached_snk);
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

static unsigned int tc_state_attached_snk_exit(int port)
{
	/* Disable PD */
	tc[port].pd_enable = 0;
	tc[port].flags &= ~TC_FLAGS_VCONN_ON;

	return 0;
}

/**
 * Super State HOST_RARD
 */
static unsigned int tc_state_host_rard(int port, enum signal sig)
{
	int ret;

	ret = (*tc_state_host_rard_sig[sig])(port);
	return SUPER(ret, sig, tc_state_vbus_cc_iso);
}

static unsigned int tc_state_host_rard_entry(int port)
{
	/* Place Ra on VCONN and Rd on Host CC */
	vpd_host_set_pull(TYPEC_CC_RA_RD, 0);

	return 0;
}

static unsigned int tc_state_host_rard_run(int port)
{
	return RUN_SUPER;
}

static unsigned int tc_state_host_rard_exit(int port)
{
	return 0;
}

/**
 * Super State HOST_OPEN
 */
static unsigned int tc_state_host_open(int port, enum signal sig)
{
	int ret;

	ret = (*tc_state_host_open_sig[sig])(port);
	return SUPER(ret, sig, tc_state_vbus_cc_iso);
}

static unsigned int tc_state_host_open_entry(int port)
{
	/* Remove the terminations from Host CC */
	vpd_host_set_pull(TYPEC_CC_OPEN, 0);

	return 0;
}

static unsigned int tc_state_host_open_run(int port)
{
	return RUN_SUPER;
}

static unsigned int tc_state_host_open_exit(int port)
{
	return 0;
}

/**
 * Super State VBUS_CC_ISO
 */
static unsigned int tc_state_vbus_cc_iso(int port, enum signal sig)
{
	int ret;

	ret = (*tc_state_vbus_cc_iso_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int tc_state_vbus_cc_iso_entry(int port)
{
	/* Enable mcu communication and cc */
	vpd_mcu_cc_en(1);

	return 0;
}

static unsigned int tc_state_vbus_cc_iso_run(int port)
{
	return 0;
}

static unsigned int tc_state_vbus_cc_iso_exit(int port)
{
	return 0;
}

static unsigned int get_super_state(int port)
{
	return RUN_SUPER;
}

#endif /*__CROS_EC_USB_TC_VPD_H */

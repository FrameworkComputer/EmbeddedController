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
#define TC_FLAGS_VCONN_ON           BIT(0)

#define SUPPORT_TIMER_RESET_INIT     0
#define SUPPORT_TIMER_RESET_REQUEST  1
#define SUPPORT_TIMER_RESET_COMPLETE 2

/* Constant used to force an initial debounce cycle */
#define PD_CC_UNSET -1

/**
 * This is the Type-C Port object that contains information needed to
 * implement a Charge Through VCONN Powered Device.
 */
static struct type_c {
	/* state machine context */
	struct sm_ctx ctx;
	/* Higher-level power deliver state machines are enabled if true. */
	uint8_t pd_enable;
	/* port flags, see TC_FLAGS_* */
	uint32_t flags;
	/*
	 * Time a charge-through port shall wait before it can determine it
	 * is attached
	 */
	uint64_t cc_debounce;
	/* Time a host port shall wait before it can determine it is attached */
	uint64_t host_cc_debounce;
	/* Time a Sink port shall wait before it can determine it is detached
	 * due to the potential for USB PD signaling on CC as described in
	 * the state definitions.
	 */
	uint64_t pd_debounce;
	/* Maintains state of billboard device */
	int billboard_presented;
	/*
	 * Time a port shall wait before it can determine it is
	 * re-attached during the try-wait process.
	 */
	uint64_t try_wait_debounce;
	/* charge-through support timer */
	uint64_t support_timer;
	/* reset the charge-through support timer */
	uint8_t support_timer_reset;
	/* VPD host port cc state */
	enum pd_cc_states host_cc_state;
	uint8_t ct_cc;
	/* The cc state */
	enum pd_cc_states cc_state;
	uint64_t next_role_swap;
} tc[CONFIG_USB_PD_PORT_MAX_COUNT];

/* List of all TypeC-level states */
enum usb_tc_state {
	/* Normal States */
	TC_DISABLED,
	TC_UNATTACHED_SNK,
	TC_ATTACH_WAIT_SNK,
	TC_ATTACHED_SNK,
	TC_ERROR_RECOVERY,
	TC_TRY_SNK,
	TC_UNATTACHED_SRC,
	TC_ATTACH_WAIT_SRC,
	TC_TRY_WAIT_SRC,
	TC_ATTACHED_SRC,
	TC_CT_TRY_SNK,
	TC_CT_ATTACH_WAIT_UNSUPPORTED,
	TC_CT_ATTACHED_UNSUPPORTED,
	TC_CT_UNATTACHED_UNSUPPORTED,
	TC_CT_UNATTACHED_VPD,
	TC_CT_DISABLED_VPD,
	TC_CT_ATTACHED_VPD,
	TC_CT_ATTACH_WAIT_VPD,
	/* Super States */
	TC_VBUS_CC_ISO,
	TC_HOST_RARD_CT_RD,
	TC_HOST_OPEN_CT_OPEN,
	TC_HOST_RP3_CT_RD,
	TC_HOST_RP3_CT_RPU,
	TC_HOST_RPU_CT_RD,
};

/* Forward declare the full list of states. This is indexed by usb_tc_state */
static const struct usb_state tc_states[];


#ifdef CONFIG_COMMON_RUNTIME
/* List of human readable state names for console debugging */
const char * const tc_state_names[] = {
	[TC_DISABLED] = "Disabled",
	[TC_UNATTACHED_SNK] = "Unattached.SNK",
	[TC_ATTACH_WAIT_SNK] = "AttachWait.SNK",
	[TC_ATTACHED_SNK] = "Attached.SNK",
	[TC_ERROR_RECOVERY] = "ErrorRecovery",
	[TC_TRY_SNK] = "Try.SNK",
	[TC_UNATTACHED_SRC] = "Unattached.SRC",
	[TC_ATTACH_WAIT_SRC] = "AttachWait.SRC",
	[TC_TRY_WAIT_SRC] = "TryWait.SRC",
	[TC_ATTACHED_SRC] = "Attached.SRC",
	[TC_CT_TRY_SNK] = "CTTry.SNK",
	[TC_CT_ATTACH_WAIT_UNSUPPORTED] = "CTAttachWait.Unsupported",
	[TC_CT_ATTACHED_UNSUPPORTED] = "CTAttached.Unsupported",
	[TC_CT_UNATTACHED_UNSUPPORTED] = "CTUnattached.Unsupported",
	[TC_CT_UNATTACHED_VPD] = "CTUnattached.VPD",
	[TC_CT_DISABLED_VPD] = "CTDisabled.VPD",
	[TC_CT_ATTACHED_VPD] = "CTAttached.VPD",
	[TC_CT_ATTACH_WAIT_VPD] = "CTAttachWait.VPD",
};
#endif

/* Forward declare private, common functions */
static void set_state_tc(const int port, enum usb_tc_state new_state);

/* Public TypeC functions */

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
	/* Vconn power device doesn't have a data role, but UFP matches SNK */
	return PD_ROLE_UFP;
}

/* Note tc_set_power_role and tc_set_data_role are unimplemented */

uint8_t tc_get_polarity(int port)
{
	/* Does not track polarity */
	return 0;
}

uint8_t tc_get_pd_enabled(int port)
{
	return tc[port].pd_enable;
}

void tc_reset_support_timer(int port)
{
	tc[port].support_timer_reset |= SUPPORT_TIMER_RESET_REQUEST;
}

void tc_state_init(int port)
{
	int res = 0;

	res = tc_restart_tcpc(port);

	CPRINTS("TCPC p%d init %s", port, res ? "failed" : "ready");

	/* Disable if restart failed, otherwise start in default state. */
	set_state_tc(port, res ? TC_DISABLED : TC_UNATTACHED_SNK);

	/* Disable pd state machines */
	tc[port].pd_enable = 0;
	tc[port].billboard_presented = 0;
	tc[port].flags = 0;
}

void tc_event_check(int port, int evt)
{
	/* Do Nothing */
}

void tc_run(const int port)
{
	run_state(port, &tc[port].ctx);
}

/* Internal Functions */

/* Set the TypeC state machine to a new state. */
static void set_state_tc(const int port, enum usb_tc_state new_state)
{
	set_state(port, &tc[port].ctx, &tc_states[new_state]);
}

/* Get the current TypeC state. */
test_export_static enum usb_tc_state get_state_tc(const int port)
{
	return tc[port].ctx.current - &tc_states[0];
}

/* Get the previous TypeC state. */
static enum usb_tc_state get_last_state_tc(const int port)
{
	return tc[port].ctx.previous - &tc_states[0];
}

test_mockable_static void print_current_state(const int port)
{
	CPRINTS("C%d: %s", port, tc_state_names[get_state_tc(port)]);
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
 * ErrorRecovery
 *
 * Super State Entry Actions:
 *   Isolate the Host-side port from the Charge-Through port
 *   Enable mcu communication
 *   Remove the terminations from Host
 *   Remove the terminations from Charge-Through
 */
static void tc_error_recovery_entry(const int port)
{
	print_current_state(port);
	/* Use cc_debounce state variable for error recovery timeout */
	tc[port].cc_debounce = get_time().val + PD_T_ERROR_RECOVERY;
}

static void tc_error_recovery_run(const int port)
{
	if (get_time().val > tc[port].cc_debounce)
		set_state_tc(port, TC_UNATTACHED_SNK);
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
static void tc_unattached_snk_entry(const int port)
{
	if (get_last_state_tc(port) != TC_UNATTACHED_SRC)
		print_current_state(port);

	tc[port].flags &= ~TC_FLAGS_VCONN_ON;
	tc[port].cc_state = PD_CC_UNSET;
}

static void tc_unattached_snk_run(const int port)
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
	if (cc_is_rp(host_cc)) {
		set_state_tc(port, TC_ATTACH_WAIT_SNK);
		return;
	}

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
		return;

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
				tc[port].cc_state == PD_CC_DFP_ATTACHED) {
		set_state_tc(port, TC_UNATTACHED_SRC);
		return;
	}
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
static void tc_attach_wait_snk_entry(const int port)
{
	print_current_state(port);
	tc[port].host_cc_state = PD_CC_UNSET;
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
			tc[port].host_cc_debounce = get_time().val +
							PD_T_CC_DEBOUNCE;
		else
			tc[port].host_cc_debounce = get_time().val +
							PD_T_PD_DEBOUNCE;
		return;
	}

	/* Wait for Host CC debounce */
	if (get_time().val < tc[port].host_cc_debounce)
		return;

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
}

static void tc_attached_snk_run(const int port)
{
	int host_new_cc_state;
	int host_cc;

	/* Has host vbus and vconn been removed */
	if (!vpd_is_host_vbus_present() && !vpd_is_vconn_present()) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

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
		return;
	}

	/* Wait for Host CC debounce */
	if (get_time().val < tc[port].host_cc_debounce)
		return;

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
		if (tc[port].host_cc_state == PD_CC_NONE) {
			set_state_tc(port, TC_CT_UNATTACHED_VPD);
			return;
		}
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
}

static void tc_attached_snk_exit(const int port)
{
	tc[port].billboard_presented = 0;
	vpd_present_billboard(BB_NONE);
}

/**
 * Super State HOST_RA_CT_RD
 */
static void tc_host_rard_ct_rd_entry(const int port)
{
	/* Place Ra on VCONN and Rd on Host CC */
	vpd_host_set_pull(TYPEC_CC_RA_RD, 0);

	/* Place Rd on Charge-Through CCs */
	vpd_ct_set_pull(TYPEC_CC_RD, 0);
}

/**
 * Super State HOST_OPEN_CT_OPEN
 */
static void tc_host_open_ct_open_entry(const int port)
{
	/* Remove the terminations from Host */
	vpd_host_set_pull(TYPEC_CC_OPEN, 0);

	/* Remove the terminations from Charge-Through */
	vpd_ct_set_pull(TYPEC_CC_OPEN, 0);
}

/**
 * Super State VBUS_CC_ISO
 */
static void tc_vbus_cc_iso_entry(const int port)
{
	/* Isolate the Host-side port from the Charge-Through port */
	vpd_vbus_pass_en(0);

	/* Remove Charge-Through side port CCs */
	vpd_ct_cc_sel(CT_OPEN);

	/* Enable mcu communication and cc */
	vpd_mcu_cc_en(1);
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
static void tc_unattached_src_entry(const int port)
{
	if (get_last_state_tc(port) != TC_UNATTACHED_SNK)
		print_current_state(port);

	/* Get power from VBUS */
	vpd_vconn_pwr_sel_odl(PWR_VBUS);

	/* Make sure it's the Charge-Through Port's VBUS */
	if (!vpd_is_ct_vbus_present()) {
		set_state_tc(port, TC_ERROR_RECOVERY);
		return;
	}

	tc[port].next_role_swap = get_time().val + PD_T_DRP_SRC;
}

static void tc_unattached_src_run(const int port)
{
	int host_cc;

	/* Check Host CC for connection */
	vpd_host_get_cc(&host_cc);

	/*
	 * Transition to AttachWait.SRC when host-side VBUS is
	 * vSafe0V and SRC.Rd state is detected on the Host-side
	 * port’s CC pin.
	 */
	if (!vpd_is_host_vbus_present() && host_cc == TYPEC_CC_VOLT_RD) {
		set_state_tc(port, TC_ATTACH_WAIT_SRC);
		return;
	}

	/*
	 * Transition to Unattached.SNK within tDRPTransition or
	 * if Charge-Through VBUS is removed.
	 */
	if (!vpd_is_ct_vbus_present() ||
				get_time().val > tc[port].next_role_swap) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}
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
static void tc_attach_wait_src_entry(const int port)
{
	print_current_state(port);

	tc[port].host_cc_state = PD_CC_UNSET;
}

static void tc_attach_wait_src_run(const int port)
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
	if (host_new_cc_state == PD_CC_NONE || !vpd_is_ct_vbus_present()) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/* Debounce the Host CC state */
	if (tc[port].host_cc_state != host_new_cc_state) {
		tc[port].host_cc_state = host_new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_CC_DEBOUNCE;
		return;
	}

	/* Wait for Host CC debounce */
	if (get_time().val < tc[port].cc_debounce)
		return;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * Try.SNK when the host-side VBUS is at vSafe0V and the SRC.Rd
	 * state is on the Host-side port’s CC pin for at least tCCDebounce.
	 */
	if (tc[port].host_cc_state == PD_CC_UFP_ATTACHED &&
						!vpd_is_host_vbus_present()) {
		set_state_tc(port, TC_TRY_SNK);
		return;
	}
}

/**
 * Attached.SRC
 */
static void tc_attached_src_entry(const int port)
{
	print_current_state(port);

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
}

static void tc_attached_src_run(const int port)
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
		set_state_tc(port, TC_UNATTACHED_SNK);
}

/**
 * Super State HOST_RPU_CT_RD
 */
static void tc_host_rpu_ct_rd_entry(const int port)
{
	/* Place RpUSB on Host CC */
	vpd_host_set_pull(TYPEC_CC_RP, TYPEC_RP_USB);

	/* Place Rd on Charge-Through CCs */
	vpd_ct_set_pull(TYPEC_CC_RD, 0);
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
static void tc_try_snk_entry(const int port)
{
	print_current_state(port);

	/* Get power from VBUS */
	vpd_vconn_pwr_sel_odl(PWR_VBUS);

	/* Make sure it's the Charge-Through Port's VBUS */
	if (!vpd_is_ct_vbus_present()) {
		set_state_tc(port, TC_ERROR_RECOVERY);
		return;
	}

	tc[port].host_cc_state = PD_CC_UNSET;

	/* Using next_role_swap timer as try_src timer */
	tc[port].next_role_swap = get_time().val + PD_T_DRP_TRY;
}

static void tc_try_snk_run(const int port)
{
	int host_new_cc_state;
	int host_cc;

	/*
	 * Wait for tDRPTry before monitoring the Charge-Through
	 * port’s CC pins for the SNK.Rp
	 */
	if (get_time().val < tc[port].next_role_swap)
		return;

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
		return;
	}

	/* Wait for Host CC debounce */
	if (get_time().val < tc[port].cc_debounce)
		return;

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
		set_state_tc(port, TC_ATTACHED_SNK);
	else if (tc[port].host_cc_state == PD_CC_NONE)
		set_state_tc(port, TC_TRY_WAIT_SRC);
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
static void tc_try_wait_src_entry(const int port)
{
	print_current_state(port);

	tc[port].host_cc_state = PD_CC_UNSET;
	tc[port].next_role_swap = get_time().val + PD_T_DRP_TRY;
}

static void tc_try_wait_src_run(const int port)
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
		return;
	}

	if (get_time().val > tc[port].host_cc_debounce) {
		/*
		 * A Charge-Through VCONN-Powered USB Device shall transition
		 * to Attached.SRC when host-side VBUS is at vSafe0V and the
		 * SRC.Rd state is detected on the Host-side port’s CC pin for
		 * at least tTryCCDebounce.
		 */
		if (tc[port].host_cc_state == PD_CC_UFP_ATTACHED &&
						!vpd_is_host_vbus_present()) {
			set_state_tc(port, TC_ATTACHED_SRC);
			return;
		}
	}

	if (get_time().val > tc[port].next_role_swap) {
		/*
		 * The Charge-Through VCONN-Powered USB Device shall transition
		 * to Unattached.SNK after tDRPTry if the Host-side port’s CC
		 * pin is not in the SRC.Rd state.
		 */
		if (tc[port].host_cc_state == PD_CC_NONE) {
			set_state_tc(port, TC_UNATTACHED_SNK);
			return;
		}
	}
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
static void tc_ct_try_snk_entry(const int port)
{
	print_current_state(port);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	tc[port].cc_state = PD_CC_UNSET;
	tc[port].next_role_swap = get_time().val + PD_T_DRP_TRY;
}

static void tc_ct_try_snk_run(const int port)
{
	int new_cc_state;
	int cc1;
	int cc2;

	/*
	 * Wait for tDRPTry before monitoring the Charge-Through
	 * port’s CC pins for the SNK.Rp
	 */
	if (get_time().val < tc[port].next_role_swap)
		return;

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
	if (!vpd_is_vconn_present()) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/* Debounce the CT CC state */
	if (tc[port].cc_state != new_cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_DEBOUNCE;
		tc[port].try_wait_debounce = get_time().val + PD_T_TRY_WAIT;

		return;
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
				vpd_is_ct_vbus_present()) {
			set_state_tc(port, TC_CT_ATTACHED_VPD);
			return;
		}
	}

	if (get_time().val > tc[port].try_wait_debounce) {
		/*
		 * A Charge-Through VCONN-Powered USB Device shall transition
		 * to CTAttached.Unsupported if SNK.Rp state is not detected
		 * for tDRPTryWait.
		 */
		if (tc[port].cc_state == PD_CC_NONE) {
			set_state_tc(port,
					TC_CT_ATTACHED_UNSUPPORTED);
			return;
		}
	}
}

static void tc_ct_try_snk_exit(const int port)
{
	/* Disable PD */
	tc[port].pd_enable = 0;
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
static void tc_ct_attach_wait_unsupported_entry(const int port)
{
	print_current_state(port);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	tc[port].cc_state = PD_CC_UNSET;
}

static void tc_ct_attach_wait_unsupported_run(const int port)
{
	int new_cc_state;
	int cc1;
	int cc2;

	/* Check CT CC for connection */
	vpd_ct_get_cc(&cc1, &cc2);

	if (cc_is_at_least_one_rd(cc1, cc2))
		new_cc_state = PD_CC_UFP_ATTACHED;
	else if (cc_is_audio_acc(cc1, cc2))
		new_cc_state = PD_CC_UFP_AUDIO_ACC;
	else /* (cc1 == TYPEC_CC_VOLT_OPEN or cc2 == TYPEC_CC_VOLT_OPEN */
		new_cc_state = PD_CC_NONE;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * Unattached.SNK if VCONN falls below vVCONNDisconnect.
	 */
	if (!vpd_is_vconn_present()) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/* Debounce the cc state */
	if (tc[port].cc_state != new_cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_CC_DEBOUNCE;
		return;
	}

	/* Wait for CC debounce */
	if (get_time().val < tc[port].cc_debounce)
		return;

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
		set_state_tc(port, TC_CT_UNATTACHED_VPD);
	else /* PD_CC_UFP_ATTACHED or PD_CC_UFP_AUDIO_ACC */
		set_state_tc(port, TC_CT_TRY_SNK);
}

static void tc_ct_attach_wait_unsupported_exit(const int port)
{
	/* Disable PD */
	tc[port].pd_enable = 0;
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
static void tc_ct_attached_unsupported_entry(const int port)
{
	print_current_state(port);

	/* Present Billboard device */
	vpd_present_billboard(BB_SNK);
}

static void tc_ct_attached_unsupported_run(const int port)
{
	int cc1;
	int cc2;

	/* Check CT CC for connection */
	vpd_ct_get_cc(&cc1, &cc2);

	if (!vpd_is_vconn_present()) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/*
	 * The Charge-Through VCONN-Powered USB Device shall transition to
	 * CTUnattached.VPD when SRC.Open state is detected on both the
	 * Charge-Through port’s CC pins or the SRC.Open state is detected
	 * on one CC pin and SRC.Ra is detected on the other CC pin.
	 */
	if ((cc1 == TYPEC_CC_VOLT_OPEN && cc2 == TYPEC_CC_VOLT_OPEN) ||
	    (cc1 == TYPEC_CC_VOLT_OPEN && cc2 == TYPEC_CC_VOLT_RA) ||
	    (cc1 == TYPEC_CC_VOLT_RA && cc2 == TYPEC_CC_VOLT_OPEN)) {
		set_state_tc(port, TC_CT_UNATTACHED_VPD);
		return;
	}
}

static void tc_ct_attached_unsupported_exit(const int port)
{
	vpd_present_billboard(BB_NONE);
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
static void tc_ct_unattached_unsupported_entry(const int port)
{
	if (get_last_state_tc(port) != TC_CT_UNATTACHED_VPD)
		print_current_state(port);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	tc[port].next_role_swap = get_time().val + PD_T_DRP_SRC;
}

static void tc_ct_unattached_unsupported_run(const int port)
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
	if (cc_is_at_least_one_rd(cc1, cc2) || cc_is_audio_acc(cc1, cc2)) {
		set_state_tc(port,
				TC_CT_ATTACH_WAIT_UNSUPPORTED);
		return;
	}

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * Unattached.SNK if VCONN falls below vVCONNDisconnect.
	 */
	if (!vpd_is_vconn_present()) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * CTUnattached.VPD within tDRPTransition after dcSRC.DRP ∙ tDRP.
	 */
	if (get_time().val > tc[port].next_role_swap) {
		set_state_tc(port, TC_CT_UNATTACHED_VPD);
		return;
	}
}

static void tc_ct_unattached_unsupported_exit(const int port)
{
	/* Disable PD */
	tc[port].pd_enable = 0;
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
static void tc_ct_unattached_vpd_entry(const int port)
{
	if (get_last_state_tc(port) != TC_CT_UNATTACHED_UNSUPPORTED)
		print_current_state(port);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	tc[port].cc_state = PD_CC_UNSET;
}

static void tc_ct_unattached_vpd_run(const int port)
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
	if (new_cc_state == PD_CC_DFP_ATTACHED) {
		set_state_tc(port, TC_CT_ATTACH_WAIT_VPD);
		return;
	}

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * Unattached.SNK if VCONN falls below vVCONNDisconnect.
	 */
	if (!vpd_is_vconn_present()) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_DRP_SRC;
		return;
	}

	if (get_time().val < tc[port].cc_debounce)
		return;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * CTUnattached.Unsupported within tDRPTransition after the state
	 * of both the Charge-Through port’s CC1 and CC2 pins is SNK.Open
	 * for tDRP-dcSRC.DRP ∙ tDRP, or if directed.
	 */
	if (tc[port].cc_state == PD_CC_NONE) {
		set_state_tc(port, TC_CT_UNATTACHED_UNSUPPORTED);
		return;
	}
}

static void tc_ct_unattached_vpd_exit(const int port)
{
	/* Disable PD */
	tc[port].pd_enable = 0;
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
static void tc_ct_disabled_vpd_entry(const int port)
{
	print_current_state(port);

	/* Get power from VBUS */
	vpd_vconn_pwr_sel_odl(PWR_VBUS);

	tc[port].next_role_swap = get_time().val + PD_T_VPDDISABLE;
}

static void tc_ct_disabled_vpd_run(const int port)
{
	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition
	 * to Unattached.SNK after tVPDDisable.
	 */
	if (get_time().val > tc[port].next_role_swap)
		set_state_tc(port, TC_UNATTACHED_SNK);
}

/**
 * CTAttached.VPD
 */
static void tc_ct_attached_vpd_entry(const int port)
{
	int cc1;
	int cc2;
	print_current_state(port);

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
}

static void tc_ct_attached_vpd_run(const int port)
{
	int new_cc_state;
	int cc1;
	int cc2;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * CTDisabled.VPD if VCONN falls below vVCONNDisconnect.
	 */
	if (!vpd_is_vconn_present()) {
		set_state_tc(port, TC_CT_DISABLED_VPD);
		return;
	}

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
		return;
	}

	if (get_time().val < tc[port].pd_debounce)
		return;

	/*
	 * A Charge-Through VCONN-Powered USB Device shall transition to
	 * CTUnattached.VPD when VBUS falls below vSinkDisconnect and the
	 * state of the passed-through CC pin is SNK.Open for tVPDCTDD.
	 */
	if (tc[port].cc_state == PD_CC_NONE && !vpd_is_ct_vbus_present())
		set_state_tc(port, TC_CT_UNATTACHED_VPD);
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
static void tc_ct_attach_wait_vpd_entry(const int port)
{
	print_current_state(port);

	/* Enable PD */
	tc[port].pd_enable = 1;
	set_polarity(port, 0);

	tc[port].cc_state = PD_CC_UNSET;
}

static void tc_ct_attach_wait_vpd_run(const int port)
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
	if (!vpd_is_vconn_present()) {
		set_state_tc(port, TC_CT_DISABLED_VPD);
		return;
	}

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val +
						PD_T_CC_DEBOUNCE;
		tc[port].pd_debounce = get_time().val +
						PD_T_PD_DEBOUNCE;
		return;
	}

	if (get_time().val > tc[port].pd_debounce) {
		/*
		 * A Charge-Through VCONN-Powered USB Device shall transition
		 * to CTUnattached.VPD when the state of both the Charge-Through
		 * port’s CC1 and CC2 pins are SNK.Open for at least
		 * tPDDebounce.
		 */
		if (tc[port].cc_state  == PD_CC_NONE) {
			set_state_tc(port, TC_CT_UNATTACHED_VPD);
			return;
		}
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
						vpd_is_ct_vbus_present()) {
			set_state_tc(port, TC_CT_ATTACHED_VPD);
			return;
		}
	}
}

static void tc_ct_attach_wait_vpd_exit(const int port)
{
	/* Disable PD */
	tc[port].pd_enable = 0;
}

/**
 * Super State HOST_RP3_CT_RD
 */
static void tc_host_rp3_ct_rd_entry(const int port)
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
		set_state_tc(port, TC_ERROR_RECOVERY);

	/* Get power from VCONN */
	vpd_vconn_pwr_sel_odl(PWR_VCONN);
}

/**
 * Super State HOST_RP3_CT_RPU
 */
static void tc_host_rp3_ct_rpu_entry(const int port)
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
		set_state_tc(port, TC_ERROR_RECOVERY);

	/* Get power from VCONN */
	vpd_vconn_pwr_sel_odl(PWR_VCONN);
}

/* All necessary Type-C states */

/*
 * Type-C State Hierarchy (Sub-States are listed inside the boxes)
 *
 * | TC_VBUS_CC_ISO ------------------------------------------------------|
 * |                                                                      |
 * |  | TC_HOST_RARD_CT_RD -----------| | TC_HOST_OPEN_CT_OPEN ---------| |
 * |  |                               | |                               | |
 * |  | TC_UNATTACHED_SNK             | | TC_DISABLED                   | |
 * |  | TC_ATTACH_WAIT_SNK            | | TC_ERROR_RECOVERY             | |
 * |  | TC_TRY_SNK                    | |-------------------------------| |
 * |  |-------------------------------|                                   |
 * |                                                                      |
 * |  | TC_HOST_RP3_CT_RD ------------| | TC_HOST_RPU_CT_RD ------------| |
 * |  |                               | |                               | |
 * |  | TC_CT_TRY_SNK                 | | TC_UNATTACHED_SRC             | |
 * |  | TC_CT_UNATTACHED_VPD          | | TC_ATTACH_WAIT_SRC            | |
 * |  | TC_CT_ATTACH_WAIT_VPD         | | TC_TRY_WAIT_SR                | |
 * |  |-------------------------------| |-------------------------------| |
 * |                                                                      |
 * |  | TC_HOST_RP3_CT_RPU -----------|                                   |
 * |  |                               |                                   |
 * |  | TC_CT_ATTACH_WAIT_UNSUPPORTED |                                   |
 * |  | TC_CT_ATTACHED_UNSUPPORTED    |                                   |
 * |  | TC_CT_UNATTACHED_UNSUPPORTED  |                                   |
 * |  |-------------------------------|                                   |
 * |----------------------------------------------------------------------|
 *
 * TC_ATTACHED_SNK
 * TC_ATTACHED_SRC
 * TC_CT_ATTACHED_VPD
 *
 */
static const struct usb_state tc_states[] = {
	/* Super States */
	[TC_VBUS_CC_ISO] = {
		.entry  = tc_vbus_cc_iso_entry,
	},
	[TC_HOST_RARD_CT_RD] = {
		.entry  = tc_host_rard_ct_rd_entry,
		.parent = &tc_states[TC_VBUS_CC_ISO],
	},
	[TC_HOST_OPEN_CT_OPEN] = {
		.entry  = tc_host_open_ct_open_entry,
		.parent = &tc_states[TC_VBUS_CC_ISO],
	},
	[TC_HOST_RP3_CT_RD] = {
		.entry  = tc_host_rp3_ct_rd_entry,
		.parent = &tc_states[TC_VBUS_CC_ISO],
	},
	[TC_HOST_RP3_CT_RPU] = {
		.entry  = tc_host_rp3_ct_rpu_entry,
		.parent = &tc_states[TC_VBUS_CC_ISO],
	},
	[TC_HOST_RPU_CT_RD] = {
		.entry  = tc_host_rpu_ct_rd_entry,
		.parent = &tc_states[TC_VBUS_CC_ISO],
	},
	/* Normal States */
	[TC_DISABLED] = {
		.entry  = tc_disabled_entry,
		.run    = tc_disabled_run,
		.exit   = tc_disabled_exit,
		.parent = &tc_states[TC_HOST_OPEN_CT_OPEN],
	},
	[TC_UNATTACHED_SNK] = {
		.entry  = tc_unattached_snk_entry,
		.run    = tc_unattached_snk_run,
		.parent = &tc_states[TC_HOST_RARD_CT_RD],
	},
	[TC_ATTACH_WAIT_SNK] = {
		.entry  = tc_attach_wait_snk_entry,
		.run    = tc_attach_wait_snk_run,
		.parent = &tc_states[TC_HOST_RARD_CT_RD],
	},
	[TC_ATTACHED_SNK] = {
		.entry  = tc_attached_snk_entry,
		.run    = tc_attached_snk_run,
		.exit   = tc_attached_snk_exit,
	},
	[TC_ERROR_RECOVERY] = {
		.entry  = tc_error_recovery_entry,
		.run    = tc_error_recovery_run,
		.parent = &tc_states[TC_HOST_OPEN_CT_OPEN],
	},
	[TC_TRY_SNK] = {
		.entry  = tc_try_snk_entry,
		.run    = tc_try_snk_run,
		.parent = &tc_states[TC_HOST_RARD_CT_RD],
	},
	[TC_UNATTACHED_SRC] = {
		.entry  = tc_unattached_src_entry,
		.run    = tc_unattached_src_run,
		.parent = &tc_states[TC_HOST_RPU_CT_RD],
	},
	[TC_ATTACH_WAIT_SRC] = {
		.entry  = tc_attach_wait_src_entry,
		.run    = tc_attach_wait_src_run,
		.parent = &tc_states[TC_HOST_RPU_CT_RD],
	},
	[TC_TRY_WAIT_SRC] = {
		.entry  = tc_try_wait_src_entry,
		.run    = tc_try_wait_src_run,
		.parent = &tc_states[TC_HOST_RPU_CT_RD],
	},
	[TC_ATTACHED_SRC] = {
		.entry  = tc_attached_src_entry,
		.run    = tc_attached_src_run,
	},
	[TC_CT_TRY_SNK] = {
		.entry  = tc_ct_try_snk_entry,
		.run    = tc_ct_try_snk_run,
		.exit   = tc_ct_try_snk_exit,
		.parent = &tc_states[TC_HOST_RP3_CT_RD],
	},
	[TC_CT_ATTACH_WAIT_UNSUPPORTED] = {
		.entry  = tc_ct_attach_wait_unsupported_entry,
		.run    = tc_ct_attach_wait_unsupported_run,
		.exit   = tc_ct_attach_wait_unsupported_exit,
		.parent = &tc_states[TC_HOST_RP3_CT_RPU],
	},
	[TC_CT_ATTACHED_UNSUPPORTED] = {
		.entry  = tc_ct_attached_unsupported_entry,
		.run    = tc_ct_attached_unsupported_run,
		.exit   = tc_ct_attached_unsupported_exit,
		.parent = &tc_states[TC_HOST_RP3_CT_RPU],
	},
	[TC_CT_UNATTACHED_UNSUPPORTED] = {
		.entry  = tc_ct_unattached_unsupported_entry,
		.run    = tc_ct_unattached_unsupported_run,
		.exit   = tc_ct_unattached_unsupported_exit,
		.parent = &tc_states[TC_HOST_RP3_CT_RPU],
	},
	[TC_CT_UNATTACHED_VPD] = {
		.entry  = tc_ct_unattached_vpd_entry,
		.run    = tc_ct_unattached_vpd_run,
		.exit   = tc_ct_unattached_vpd_exit,
		.parent = &tc_states[TC_HOST_RP3_CT_RD],
	},
	[TC_CT_DISABLED_VPD] = {
		.entry  = tc_ct_disabled_vpd_entry,
		.run    = tc_ct_disabled_vpd_run,
		.parent = &tc_states[TC_HOST_OPEN_CT_OPEN],
	},
	[TC_CT_ATTACHED_VPD] = {
		.entry  = tc_ct_attached_vpd_entry,
		.run    = tc_ct_attached_vpd_run,
	},
	[TC_CT_ATTACH_WAIT_VPD] = {
		.entry  = tc_ct_attach_wait_vpd_entry,
		.run    = tc_ct_attach_wait_vpd_run,
		.exit   = tc_ct_attach_wait_vpd_exit,
		.parent = &tc_states[TC_HOST_RP3_CT_RD],
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

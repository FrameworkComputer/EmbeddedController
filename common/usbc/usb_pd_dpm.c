/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Device Policy Manager implementation
 * Refer to USB PD 3.0 spec, version 2.0, sections 8.2 and 8.3
 */

#include "charge_state.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "usb_dp_alt_mode.h"
#include "usb_mode.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dpm.h"
#include "usb_pd_tcpm.h"
#include "usb_pd_pdo.h"
#include "usb_tbt_alt_mode.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/* Max Attention length is header + 1 VDO */
#define DPM_ATTENION_MAX_VDO 2

static struct {
	atomic_t flags;
	uint32_t vdm_attention[DPM_ATTENION_MAX_VDO];
	int vdm_cnt;
	mutex_t vdm_attention_mutex;
} dpm[CONFIG_USB_PD_PORT_MAX_COUNT];

#define DPM_SET_FLAG(port, flag) atomic_or(&dpm[(port)].flags, (flag))
#define DPM_CLR_FLAG(port, flag) atomic_clear_bits(&dpm[(port)].flags, (flag))
#define DPM_CHK_FLAG(port, flag) (dpm[(port)].flags & (flag))

/* Flags for internal DPM state */
#define DPM_FLAG_MODE_ENTRY_DONE      BIT(0)
#define DPM_FLAG_EXIT_REQUEST         BIT(1)
#define DPM_FLAG_ENTER_DP             BIT(2)
#define DPM_FLAG_ENTER_TBT            BIT(3)
#define DPM_FLAG_ENTER_USB4           BIT(4)
#define DPM_FLAG_ENTER_ANY            (DPM_FLAG_ENTER_DP | DPM_FLAG_ENTER_TBT \
					| DPM_FLAG_ENTER_USB4)
#define DPM_FLAG_SEND_ATTENTION       BIT(5)
#define DPM_FLAG_DATA_RESET_REQUESTED BIT(6)
#define DPM_FLAG_DATA_RESET_DONE      BIT(7)

#ifdef CONFIG_ZEPHYR
static int init_vdm_attention_mutex(const struct device *dev)
{
	int port;

	ARG_UNUSED(dev);

	for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++)
		k_mutex_init(&dpm[port].vdm_attention_mutex);

	return 0;
}
SYS_INIT(init_vdm_attention_mutex, POST_KERNEL, 50);
#endif /* CONFIG_ZEPHYR */

enum ec_status pd_request_vdm_attention(int port, const uint32_t *data,
					int vdo_count)
{
	mutex_lock(&dpm[port].vdm_attention_mutex);

	/* Only one Attention message may be pending */
	if (DPM_CHK_FLAG(port, DPM_FLAG_SEND_ATTENTION)) {
		mutex_unlock(&dpm[port].vdm_attention_mutex);
		return EC_RES_UNAVAILABLE;
	}

	/* SVDM Attention message must be 1 or 2 VDOs in length */
	if (!vdo_count || (vdo_count > DPM_ATTENION_MAX_VDO)) {
		mutex_unlock(&dpm[port].vdm_attention_mutex);
		return EC_RES_INVALID_PARAM;
	}

	/* Save contents of Attention message */
	memcpy(dpm[port].vdm_attention, data, vdo_count * sizeof(uint32_t));
	dpm[port].vdm_cnt = vdo_count;

	/*
	 * Indicate to DPM that an Attention message needs to be sent. This flag
	 * will be cleared when the Attention message is sent to the policy
	 * engine.
	 */
	DPM_SET_FLAG(port, DPM_FLAG_SEND_ATTENTION);

	mutex_unlock(&dpm[port].vdm_attention_mutex);

	return EC_RES_SUCCESS;
}

enum ec_status pd_request_enter_mode(int port, enum typec_mode mode)
{
	if (port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	/* Only one enter request may be active at a time. */
	if (DPM_CHK_FLAG(port, DPM_FLAG_ENTER_DP |
				DPM_FLAG_ENTER_TBT |
				DPM_FLAG_ENTER_USB4))
		return EC_RES_BUSY;

	switch (mode) {
	case TYPEC_MODE_DP:
		DPM_SET_FLAG(port, DPM_FLAG_ENTER_DP);
		break;
#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
	case TYPEC_MODE_TBT:
		DPM_SET_FLAG(port, DPM_FLAG_ENTER_TBT);
		break;
#endif /* CONFIG_USB_PD_TBT_COMPAT_MODE */
#ifdef CONFIG_USB_PD_USB4
	case TYPEC_MODE_USB4:
		DPM_SET_FLAG(port, DPM_FLAG_ENTER_USB4);
		break;
#endif
	default:
		return EC_RES_INVALID_PARAM;
	}

	DPM_CLR_FLAG(port, DPM_FLAG_MODE_ENTRY_DONE);
	DPM_CLR_FLAG(port, DPM_FLAG_EXIT_REQUEST);
	DPM_CLR_FLAG(port, DPM_FLAG_DATA_RESET_DONE);

	return EC_RES_SUCCESS;
}

void dpm_init(int port)
{
	dpm[port].flags = 0;
}

void dpm_mode_exit_complete(int port)
{
	DPM_CLR_FLAG(port, DPM_FLAG_MODE_ENTRY_DONE | DPM_FLAG_EXIT_REQUEST |
			DPM_FLAG_SEND_ATTENTION);
}

static void dpm_set_mode_entry_done(int port)
{
	DPM_SET_FLAG(port, DPM_FLAG_MODE_ENTRY_DONE);
	DPM_CLR_FLAG(port, DPM_FLAG_ENTER_DP | DPM_FLAG_ENTER_TBT |
			DPM_FLAG_ENTER_USB4);
}

void dpm_set_mode_exit_request(int port)
{
	DPM_SET_FLAG(port, DPM_FLAG_EXIT_REQUEST);
}

void dpm_data_reset_complete(int port)
{
	DPM_CLR_FLAG(port, DPM_FLAG_DATA_RESET_REQUESTED);
	DPM_SET_FLAG(port, DPM_FLAG_DATA_RESET_DONE);
	DPM_CLR_FLAG(port, DPM_FLAG_MODE_ENTRY_DONE);
}

static void dpm_clear_mode_exit_request(int port)
{
	DPM_CLR_FLAG(port, DPM_FLAG_EXIT_REQUEST);
}

/*
 * Returns true if the current policy requests that the EC try to enter this
 * mode on this port. If the EC is in charge of policy, the answer is always
 * yes.
 */
static bool dpm_mode_entry_requested(int port, enum typec_mode mode)
{
	/* If the AP isn't controlling policy, the EC is. */
	if (!IS_ENABLED(CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY))
		return true;

	switch (mode) {
	case TYPEC_MODE_DP:
		return !!DPM_CHK_FLAG(port, DPM_FLAG_ENTER_DP);
	case TYPEC_MODE_TBT:
		return !!DPM_CHK_FLAG(port, DPM_FLAG_ENTER_TBT);
	case TYPEC_MODE_USB4:
		return !!DPM_CHK_FLAG(port, DPM_FLAG_ENTER_USB4);
	default:
		return false;
	}
}

void dpm_vdm_acked(int port, enum tcpci_msg_type type, int vdo_count,
		uint32_t *vdm)
{
	const uint16_t svid = PD_VDO_VID(vdm[0]);

	assert(vdo_count >= 1);

	switch (svid) {
	case USB_SID_DISPLAYPORT:
		dp_vdm_acked(port, type, vdo_count, vdm);
		break;
	case USB_VID_INTEL:
		if (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE)) {
			intel_vdm_acked(port, type, vdo_count, vdm);
			break;
		}
	default:
		CPRINTS("C%d: Received unexpected VDM ACK for SVID %d", port,
				svid);
	}
}

void dpm_vdm_naked(int port, enum tcpci_msg_type type, uint16_t svid,
		uint8_t vdm_cmd)
{
	switch (svid) {
	case USB_SID_DISPLAYPORT:
		dp_vdm_naked(port, type, vdm_cmd);
		break;
	case USB_VID_INTEL:
		if (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE)) {
			intel_vdm_naked(port, type, vdm_cmd);
			break;
		}
	default:
		CPRINTS("C%d: Received unexpected VDM NAK for SVID %d", port,
				svid);
	}
}

/*
 * Requests that the PE send one VDM, whichever is next in the mode entry
 * sequence. This only happens if preconditions for mode entry are met. If
 * CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY is enabled, this function waits for the
 * AP to direct mode entry.
 */
static void dpm_attempt_mode_entry(int port)
{
	int vdo_count = 0;
	uint32_t vdm[VDO_MAX_SIZE];
	enum tcpci_msg_type tx_type = TCPCI_MSG_SOP;
	bool enter_mode_requested =
		IS_ENABLED(CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY) ?  false : true;
	enum dpm_msg_setup_status status = MSG_SETUP_UNSUPPORTED;

	if (pd_get_data_role(port) != PD_ROLE_DFP) {
		if (DPM_CHK_FLAG(port, DPM_FLAG_ENTER_DP |
					DPM_FLAG_ENTER_TBT |
					DPM_FLAG_ENTER_USB4))
			DPM_CLR_FLAG(port, DPM_FLAG_ENTER_DP |
					DPM_FLAG_ENTER_TBT |
					DPM_FLAG_ENTER_USB4);
		/*
		 * TODO(b/168030639): Notify the AP that the enter mode request
		 * failed.
		 */
		return;
	}

#ifdef HAS_TASK_CHIPSET
	/*
	 * Do not try to enter mode while CPU is off.
	 * CPU transitions (e.g b/158634281) can occur during the discovery
	 * phase or during enter/exit negotiations, and the state
	 * of the modes can get out of sync, causing the attempt to
	 * enter the mode to fail prematurely.
	 */
	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF))
		return;
#endif
	/*
	 * If discovery has not occurred for modes, do not attempt to switch
	 * to alt mode.
	 */
	if (pd_get_svids_discovery(port, TCPCI_MSG_SOP) != PD_DISC_COMPLETE ||
	    pd_get_modes_discovery(port, TCPCI_MSG_SOP) != PD_DISC_COMPLETE)
		return;

	if (dp_entry_is_done(port) ||
	   (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE) &&
				tbt_entry_is_done(port)) ||
	   (IS_ENABLED(CONFIG_USB_PD_USB4) && enter_usb_entry_is_done(port))) {
		dpm_set_mode_entry_done(port);
		return;
	}

	/*
	 * If muxes are still settling, then wait on our next VDM.  We must
	 * ensure we correctly sequence actions such as USB safe state with TBT
	 * entry or DP configuration.
	 */
	if (IS_ENABLED(CONFIG_USBC_SS_MUX) && !usb_mux_set_completed(port))
		return;

	if (IS_ENABLED(CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY) &&
			IS_ENABLED(CONFIG_USB_PD_DATA_RESET_MSG) &&
			DPM_CHK_FLAG(port, DPM_FLAG_ENTER_ANY) &&
			!DPM_CHK_FLAG(port, DPM_FLAG_DATA_RESET_REQUESTED) &&
			!DPM_CHK_FLAG(port, DPM_FLAG_DATA_RESET_DONE)) {
		pd_dpm_request(port, DPM_REQUEST_DATA_RESET);
		DPM_SET_FLAG(port, DPM_FLAG_DATA_RESET_REQUESTED);
		return;
	}

	if (IS_ENABLED(CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY) &&
			IS_ENABLED(CONFIG_USB_PD_DATA_RESET_MSG) &&
			!DPM_CHK_FLAG(port, DPM_FLAG_DATA_RESET_DONE)) {
		return;
	}

	/* Check if port, port partner and cable support USB4. */
	if (IS_ENABLED(CONFIG_USB_PD_USB4) &&
	    board_is_tbt_usb4_port(port) &&
	    enter_usb_port_partner_is_capable(port) &&
	    enter_usb_cable_is_capable(port) &&
	    dpm_mode_entry_requested(port, TYPEC_MODE_USB4)) {
		/*
		 * For certain cables, enter Thunderbolt alt mode with the
		 * cable and USB4 mode with the port partner.
		 */
		if (tbt_cable_entry_required_for_usb4(port)) {
			vdo_count = ARRAY_SIZE(vdm);
			status = tbt_setup_next_vdm(port, &vdo_count, vdm,
						    &tx_type);
		} else {
			pd_dpm_request(port, DPM_REQUEST_ENTER_USB);
			return;
		}
	}

	/* If not, check if they support Thunderbolt alt mode. */
	if (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE) &&
			board_is_tbt_usb4_port(port) &&
			pd_is_mode_discovered_for_svid(port, TCPCI_MSG_SOP,
				USB_VID_INTEL) &&
			dpm_mode_entry_requested(port, TYPEC_MODE_TBT)) {
		enter_mode_requested = true;
		vdo_count = ARRAY_SIZE(vdm);
		status = tbt_setup_next_vdm(port, &vdo_count, vdm,
					    &tx_type);
	}

	/* If not, check if they support DisplayPort alt mode. */
	if (status == MSG_SETUP_UNSUPPORTED &&
	    !DPM_CHK_FLAG(port, DPM_FLAG_MODE_ENTRY_DONE) &&
	    pd_is_mode_discovered_for_svid(port, TCPCI_MSG_SOP,
					   USB_SID_DISPLAYPORT) &&
	    dpm_mode_entry_requested(port, TYPEC_MODE_DP)) {
		enter_mode_requested = true;
		vdo_count = ARRAY_SIZE(vdm);
		status = dp_setup_next_vdm(port, &vdo_count, vdm);
	}

	/* Not ready to send a VDM, check again next cycle */
	if (status == MSG_SETUP_MUX_WAIT)
		return;

	/*
	 * If the PE didn't discover any supported (requested) alternate mode,
	 * just mark setup done and get out of here.
	 */
	if (status != MSG_SETUP_SUCCESS &&
				!DPM_CHK_FLAG(port, DPM_FLAG_MODE_ENTRY_DONE)) {
		if (enter_mode_requested) {
			/*
			 * TODO(b/168030639): Notify the AP that mode entry
			 * failed.
			 */
			CPRINTS("C%d: No supported alt mode discovered", port);
		}
		/*
		 * If the AP did not request mode entry, it may do so in the
		 * future, but the DPM is done trying for now.
		 */
		dpm_set_mode_entry_done(port);
		return;
	}

	if (status != MSG_SETUP_SUCCESS) {
		dpm_set_mode_entry_done(port);
		CPRINTS("C%d: Couldn't construct alt mode VDM", port);
		return;
	}

	/*
	 * TODO(b/155890173): Provide a host command to request that the PE send
	 * an arbitrary VDM via this mechanism.
	 */
	if (!pd_setup_vdm_request(port, tx_type, vdm, vdo_count)) {
		dpm_set_mode_entry_done(port);
		return;
	}

	pd_dpm_request(port, DPM_REQUEST_VDM);
}

static void dpm_attempt_mode_exit(int port)
{
	uint32_t vdm[VDO_MAX_SIZE];
	int vdo_count = ARRAY_SIZE(vdm);
	enum dpm_msg_setup_status status = MSG_SETUP_ERROR;
	enum tcpci_msg_type tx_type = TCPCI_MSG_SOP;

	if (IS_ENABLED(CONFIG_USB_PD_USB4) &&
	    enter_usb_entry_is_done(port)) {
		CPRINTS("C%d: USB4 teardown", port);
		usb4_exit_mode_request(port);
	}

	/*
	 * If muxes are still settling, then wait on our next VDM.  We must
	 * ensure we correctly sequence actions such as USB safe state with TBT
	 * or DP mode exit.
	 */
	if (IS_ENABLED(CONFIG_USBC_SS_MUX) && !usb_mux_set_completed(port))
		return;

	if (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE) &&
	    tbt_is_active(port)) {
		/*
		 * When the port is in USB4 mode and receives an exit request,
		 * it leaves USB4 SOP in active state.
		 * TODO(b/156749387): Support Data Reset for exiting USB4 SOP.
		 */
		CPRINTS("C%d: TBT teardown", port);
		tbt_exit_mode_request(port);
		status = tbt_setup_next_vdm(port, &vdo_count, vdm, &tx_type);
	} else if (dp_is_active(port)) {
		CPRINTS("C%d: DP teardown", port);
		status = dp_setup_next_vdm(port, &vdo_count, vdm);
	} else {
		/* Clear exit mode request */
		dpm_clear_mode_exit_request(port);
		return;
	}

	/* This covers error, wait mux, and unsupported cases */
	if (status != MSG_SETUP_SUCCESS)
		return;

	if (!pd_setup_vdm_request(port, tx_type, vdm, vdo_count)) {
		dpm_clear_mode_exit_request(port);
		return;
	}

	pd_dpm_request(port, DPM_REQUEST_VDM);
}

static void dpm_send_attention_vdm(int port)
{
	/* Set up VDM ATTEN msg that was passed in previously */
	if (pd_setup_vdm_request(port, TCPCI_MSG_SOP, dpm[port].vdm_attention,
				 dpm[port].vdm_cnt) == true)
		/* Trigger PE to start a VDM command run */
		pd_dpm_request(port, DPM_REQUEST_VDM);

	/* Clear flag after message is sent to PE layer */
	DPM_CLR_FLAG(port, DPM_FLAG_SEND_ATTENTION);
}

void dpm_run(int port)
{
	if (pd_get_data_role(port) == PD_ROLE_DFP) {
		/* Run DFP related DPM requests */
		if (DPM_CHK_FLAG(port, DPM_FLAG_EXIT_REQUEST))
			dpm_attempt_mode_exit(port);
		else if (!DPM_CHK_FLAG(port, DPM_FLAG_MODE_ENTRY_DONE))
			dpm_attempt_mode_entry(port);
	} else {
		/* Run UFP related DPM requests */
		if (DPM_CHK_FLAG(port, DPM_FLAG_SEND_ATTENTION))
			dpm_send_attention_vdm(port);
	}
}

/*
 * Source-out policy variables and APIs
 *
 * Priority for the available 3.0 A ports is given in the following order:
 * - sink partners which report requiring > 1.5 A in their Sink_Capabilities
 */

/*
 * Bitmasks of port numbers in each following category
 *
 * Note: request bitmasks should be accessed atomically as other ports may alter
 * them
 */
static uint32_t		max_current_claimed;
K_MUTEX_DEFINE(max_current_claimed_lock);

/* Ports with PD sink needing > 1.5 A */
static atomic_t sink_max_pdo_requested;
/* Ports with FRS source needing > 1.5 A */
static atomic_t source_frs_max_requested;
/* Ports with non-PD sinks, so current requirements are unknown */
static atomic_t non_pd_sink_max_requested;

#define LOWEST_PORT(p) __builtin_ctz(p)  /* Undefined behavior if p == 0 */

static int count_port_bits(uint32_t bitmask)
{
	int i, total = 0;

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (bitmask & BIT(i))
			total++;
	}

	return total;
}

/*
 * Centralized, mutex-controlled updates to the claimed 3.0 A ports
 */
static void balance_source_ports(void);
DECLARE_DEFERRED(balance_source_ports);

static void balance_source_ports(void)
{
	uint32_t removed_ports, new_ports;
	static bool deferred_waiting;

	if (in_deferred_context())
		deferred_waiting = false;

	/*
	 * Ignore balance attempts while we're waiting for a downgraded port to
	 * finish the downgrade.
	 */
	if (deferred_waiting)
		return;

	mutex_lock(&max_current_claimed_lock);

	/* Remove any ports which no longer require 3.0 A */
	removed_ports = max_current_claimed & ~(sink_max_pdo_requested |
						source_frs_max_requested |
						non_pd_sink_max_requested);
	max_current_claimed &= ~removed_ports;

	/* Allocate 3.0 A to new PD sink ports that need it */
	new_ports = sink_max_pdo_requested & ~max_current_claimed;
	while (new_ports) {
		int new_max_port = LOWEST_PORT(new_ports);

		if (count_port_bits(max_current_claimed) <
						CONFIG_USB_PD_3A_PORTS) {
			max_current_claimed |= BIT(new_max_port);
			typec_select_src_current_limit_rp(new_max_port,
							  TYPEC_RP_3A0);
		} else if (non_pd_sink_max_requested & max_current_claimed) {
			/* Always downgrade non-PD ports first */
			int rem_non_pd = LOWEST_PORT(non_pd_sink_max_requested &
						     max_current_claimed);
			typec_select_src_current_limit_rp(rem_non_pd,
				typec_get_default_current_limit_rp(rem_non_pd));
			max_current_claimed &= ~BIT(rem_non_pd);

			/* Wait tSinkAdj before using current */
			deferred_waiting = true;
			hook_call_deferred(&balance_source_ports_data,
					   PD_T_SINK_ADJ);
			goto unlock;
		} else if (source_frs_max_requested & max_current_claimed) {
			/* Downgrade lowest FRS port from 3.0 A slot */
			int rem_frs = LOWEST_PORT(source_frs_max_requested &
						 max_current_claimed);
			pd_dpm_request(rem_frs, DPM_REQUEST_FRS_DET_DISABLE);
			max_current_claimed &= ~BIT(rem_frs);

			/* Give 20 ms for the PD task to process DPM flag */
			deferred_waiting = true;
			hook_call_deferred(&balance_source_ports_data,
					   20 * MSEC);
			goto unlock;
		} else {
			/* No lower priority ports to downgrade */
			goto unlock;
		}
		new_ports &= ~BIT(new_max_port);
	}

	/* Allocate 3.0 A to any new FRS ports that need it */
	new_ports = source_frs_max_requested & ~max_current_claimed;
	while (new_ports) {
		int new_frs_port = LOWEST_PORT(new_ports);

		if (count_port_bits(max_current_claimed) <
						CONFIG_USB_PD_3A_PORTS) {
			max_current_claimed |= BIT(new_frs_port);
			pd_dpm_request(new_frs_port,
				       DPM_REQUEST_FRS_DET_ENABLE);
		} else if (non_pd_sink_max_requested & max_current_claimed) {
			int rem_non_pd = LOWEST_PORT(non_pd_sink_max_requested &
						     max_current_claimed);
			typec_select_src_current_limit_rp(rem_non_pd,
				typec_get_default_current_limit_rp(rem_non_pd));
			max_current_claimed &= ~BIT(rem_non_pd);

			/* Wait tSinkAdj before using current */
			deferred_waiting = true;
			hook_call_deferred(&balance_source_ports_data,
					   PD_T_SINK_ADJ);
			goto unlock;
		} else {
			/* No lower priority ports to downgrade */
			goto unlock;
		}
		new_ports &= ~BIT(new_frs_port);
	}

	/* Allocate 3.0 A to any non-PD ports which could need it */
	new_ports = non_pd_sink_max_requested & ~max_current_claimed;
	while (new_ports) {
		int new_max_port = LOWEST_PORT(new_ports);

		if (count_port_bits(max_current_claimed) <
						CONFIG_USB_PD_3A_PORTS) {
			max_current_claimed |= BIT(new_max_port);
			typec_select_src_current_limit_rp(new_max_port,
							  TYPEC_RP_3A0);
		} else {
			/* No lower priority ports to downgrade */
			goto unlock;
		}
		new_ports &= ~BIT(new_max_port);
	}
unlock:
	mutex_unlock(&max_current_claimed_lock);
}

/* Process port's first Sink_Capabilities PDO for port current consideration */
void dpm_evaluate_sink_fixed_pdo(int port, uint32_t vsafe5v_pdo)
{
	/* Verify partner supplied valid vSafe5V fixed object first */
	if ((vsafe5v_pdo & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
		return;

	if (PDO_FIXED_VOLTAGE(vsafe5v_pdo) != 5000)
		return;

	if (pd_get_power_role(port) == PD_ROLE_SOURCE) {
		if (CONFIG_USB_PD_3A_PORTS == 0)
			return;

		/* Valid PDO to process, so evaluate whether >1.5A is needed */
		if (PDO_FIXED_CURRENT(vsafe5v_pdo) <= 1500)
			return;

		atomic_or(&sink_max_pdo_requested, BIT(port));
	} else {
		int frs_current = vsafe5v_pdo & PDO_FIXED_FRS_CURR_MASK;

		if (!IS_ENABLED(CONFIG_USB_PD_FRS))
			return;

		/* FRS is only supported in PD 3.0 and higher */
		if (pd_get_rev(port, TCPCI_MSG_SOP) == PD_REV20)
			return;

		if ((vsafe5v_pdo & PDO_FIXED_DUAL_ROLE) && frs_current) {
			/* Always enable FRS when 3.0 A is not needed */
			if (frs_current == PDO_FIXED_FRS_CURR_DFLT_USB_POWER ||
			    frs_current == PDO_FIXED_FRS_CURR_1A5_AT_5V) {
				pd_dpm_request(port,
					       DPM_REQUEST_FRS_DET_ENABLE);
				return;
			}

			if (CONFIG_USB_PD_3A_PORTS == 0)
				return;

			atomic_or(&source_frs_max_requested, BIT(port));
		} else {
			return;
		}
	}

	balance_source_ports();
}

void dpm_add_non_pd_sink(int port)
{
	if (CONFIG_USB_PD_3A_PORTS == 0)
		return;

	atomic_or(&non_pd_sink_max_requested, BIT(port));

	balance_source_ports();
}

void dpm_remove_sink(int port)
{
	if (CONFIG_USB_PD_3A_PORTS == 0)
		return;

	if (!(BIT(port) & sink_max_pdo_requested) &&
	    !(BIT(port) & non_pd_sink_max_requested))
		return;

	atomic_clear_bits(&sink_max_pdo_requested, BIT(port));
	atomic_clear_bits(&non_pd_sink_max_requested, BIT(port));

	/* Restore selected default Rp on the port */
	typec_select_src_current_limit_rp(port,
		typec_get_default_current_limit_rp(port));

	balance_source_ports();
}

void dpm_remove_source(int port)
{
	if (CONFIG_USB_PD_3A_PORTS == 0)
		return;

	if (!IS_ENABLED(CONFIG_USB_PD_FRS))
		return;

	if (!(BIT(port) & source_frs_max_requested))
		return;

	atomic_clear_bits(&source_frs_max_requested, BIT(port));

	balance_source_ports();
}

/*
 * Note: all ports receive the 1.5 A source offering until they are found to
 * match a criteria on the 3.0 A priority list (ex. through sink capability
 * probing), at which point they will be offered a new 3.0 A source capability.
 */
__overridable int dpm_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	/* Max PDO may not exist on boards which don't offer 3 A */
#if CONFIG_USB_PD_3A_PORTS > 0
	if (max_current_claimed & BIT(port)) {
		*src_pdo = pd_src_pdo_max;
		return pd_src_pdo_max_cnt;
	}
#endif

	*src_pdo = pd_src_pdo;
	return pd_src_pdo_cnt;
}

int dpm_get_source_current(const int port)
{
	if (pd_get_power_role(port) == PD_ROLE_SINK)
		return 0;

	if (max_current_claimed & BIT(port))
		return 3000;
	else if (typec_get_default_current_limit_rp(port) == TYPEC_RP_1A5)
		return 1500;
	else
		return 500;
}

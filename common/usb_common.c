/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Contains common USB functions shared between the old (i.e. usb_pd_protocol)
 * and the new (i.e. usb_sm_*) USB-C PD stacks.
 */

#include "atomic.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "task.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "util.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

int usb_get_battery_soc(void)
{
#if defined(CONFIG_CHARGER)
	return charge_get_percent();
#elif defined(CONFIG_BATTERY)
	return board_get_battery_soc();
#else
	return 0;
#endif
}

#if defined(CONFIG_USB_PD_PREFER_MV) && defined(PD_PREFER_LOW_VOLTAGE) + \
	defined(PD_PREFER_HIGH_VOLTAGE) > 1
#error "PD preferred voltage strategy should be mutually exclusive."
#endif

STATIC_IF_NOT(CONFIG_USB_PD_PREFER_MV)
struct pd_pref_config_t __maybe_unused pd_pref_config;

/*
 * CC values for regular sources and Debug sources (aka DTS)
 *
 * Source type  Mode of Operation   CC1    CC2
 * ---------------------------------------------
 * Regular      Default USB Power   RpUSB  Open
 * Regular      USB-C @ 1.5 A       Rp1A5  Open
 * Regular      USB-C @ 3 A	    Rp3A0  Open
 * DTS		Default USB Power   Rp3A0  Rp1A5
 * DTS		USB-C @ 1.5 A       Rp1A5  RpUSB
 * DTS		USB-C @ 3 A	    Rp3A0  RpUSB
 */

typec_current_t usb_get_typec_current_limit(enum tcpc_cc_polarity polarity,
	enum tcpc_cc_voltage_status cc1, enum tcpc_cc_voltage_status cc2)
{
	typec_current_t charge = 0;
	enum tcpc_cc_voltage_status cc = polarity ? cc2 : cc1;
	enum tcpc_cc_voltage_status cc_alt = polarity ? cc1 : cc2;

	switch (cc) {
	case TYPEC_CC_VOLT_RP_3_0:
		if (!cc_is_rp(cc_alt) || cc_alt == TYPEC_CC_VOLT_RP_DEF)
			charge = 3000;
		else if (cc_alt == TYPEC_CC_VOLT_RP_1_5)
			charge = 500;
		break;
	case TYPEC_CC_VOLT_RP_1_5:
		charge = 1500;
		break;
	case TYPEC_CC_VOLT_RP_DEF:
		charge = 500;
		break;
	default:
		break;
	}

	if (IS_ENABLED(CONFIG_USBC_DISABLE_CHARGE_FROM_RP_DEF) && charge == 500)
		charge = 0;

	if (cc_is_rp(cc_alt))
		charge |= TYPEC_CURRENT_DTS_MASK;

	return charge;
}

enum tcpc_cc_polarity get_snk_polarity(enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2)
{
	if (cc_is_open(cc1, cc2))
		return POLARITY_NONE;

	/* The following assumes:
	 *
	 * TYPEC_CC_VOLT_RP_3_0 > TYPEC_CC_VOLT_RP_1_5
	 * TYPEC_CC_VOLT_RP_1_5 > TYPEC_CC_VOLT_RP_DEF
	 * TYPEC_CC_VOLT_RP_DEF > TYPEC_CC_VOLT_OPEN
	 */
	return cc2 > cc1;
}

enum tcpc_cc_polarity get_src_polarity(enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2)
{
	if (cc_is_open(cc1, cc2))
		return POLARITY_NONE;

	return cc1 != TYPEC_CC_VOLT_RD;
}

enum pd_cc_states pd_get_cc_state(
	enum tcpc_cc_voltage_status cc1, enum tcpc_cc_voltage_status cc2)
{
	/* Port partner is a SNK */
	if (cc_is_snk_dbg_acc(cc1, cc2))
		return PD_CC_UFP_DEBUG_ACC;
	if (cc_is_at_least_one_rd(cc1, cc2))
		return PD_CC_UFP_ATTACHED;
	if (cc_is_audio_acc(cc1, cc2))
		return PD_CC_UFP_AUDIO_ACC;

	/* Port partner is a SRC */
	if (cc_is_rp(cc1) && cc_is_rp(cc2))
		return PD_CC_DFP_DEBUG_ACC;
	if (cc_is_rp(cc1) || cc_is_rp(cc2))
		return PD_CC_DFP_ATTACHED;

	/*
	 * 1) Both lines are Vopen or
	 * 2) Only an e-marked cabled without a partner on the other side
	 */
	return PD_CC_NONE;
}

/*
 * Zinger implements a board specific usb policy that does not define
 * PD_MAX_VOLTAGE_MV and PD_OPERATING_POWER_MW. And in turn, does not
 * use the following functions.
 */
#if defined(PD_MAX_VOLTAGE_MV) && defined(PD_OPERATING_POWER_MW)
int pd_find_pdo_index(uint32_t src_cap_cnt, const uint32_t * const src_caps,
					int max_mv, uint32_t *selected_pdo)
{
	int i, uw, mv;
	int ret = 0;
	int cur_uw = 0;
	int has_preferred_pdo;
	int prefer_cur;
	int desired_uw = 0;
	const int prefer_mv = pd_pref_config.mv;
	const int type = pd_pref_config.type;

	int __attribute__((unused)) cur_mv = 0;

	if (IS_ENABLED(CONFIG_USB_PD_PREFER_MV))
		desired_uw = charge_get_plt_plus_bat_desired_mw() * 1000;

	/* max voltage is always limited by this boards max request */
	max_mv = MIN(max_mv, PD_MAX_VOLTAGE_MV);

	/* Get max power that is under our max voltage input */
	for (i = 0; i < src_cap_cnt; i++) {
		/* its an unsupported Augmented PDO (PD3.0) */
		if ((src_caps[i] & PDO_TYPE_MASK) == PDO_TYPE_AUGMENTED)
			continue;

		mv = ((src_caps[i] >> 10) & 0x3FF) * 50;
		/* Skip invalid voltage */
		if (!mv)
			continue;
		/* Skip any voltage not supported by this board */
		if (!pd_is_valid_input_voltage(mv))
			continue;

		if ((src_caps[i] & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
			uw = 250000 * (src_caps[i] & 0x3FF);
		} else {
			int ma = (src_caps[i] & 0x3FF) * 10;

			ma = MIN(ma, PD_MAX_CURRENT_MA);
			uw = ma * mv;
		}

		if (mv > max_mv)
			continue;
		uw = MIN(uw, PD_MAX_POWER_MW * 1000);
		prefer_cur = 0;

		/* Apply special rules in favor of voltage  */
		if (IS_ENABLED(PD_PREFER_LOW_VOLTAGE)) {
			if (uw == cur_uw && mv < cur_mv)
				prefer_cur = 1;
		} else if (IS_ENABLED(PD_PREFER_HIGH_VOLTAGE)) {
			if (uw == cur_uw && mv > cur_mv)
				prefer_cur = 1;
		} else if (IS_ENABLED(CONFIG_USB_PD_PREFER_MV)) {
			/* Pick if the PDO provides more than desired. */
			if (uw >= desired_uw) {
				/* pick if cur_uw is less than desired watt */
				if (cur_uw < desired_uw)
					prefer_cur = 1;
				else if (type == PD_PREFER_BUCK) {
					/*
					 * pick the smallest mV above prefer_mv
					 */
					if (mv >= prefer_mv && mv < cur_mv)
						prefer_cur = 1;
					/*
					 * pick if cur_mv is less than
					 * prefer_mv, and we have higher mV
					 */
					else if (cur_mv < prefer_mv &&
						 mv > cur_mv)
						prefer_cur = 1;
				} else if (type == PD_PREFER_BOOST) {
					/*
					 * pick the largest mV below prefer_mv
					 */
					if (mv <= prefer_mv && mv > cur_mv)
						prefer_cur = 1;
					/*
					 * pick if cur_mv is larger than
					 * prefer_mv, and we have lower mV
					 */
					else if (cur_mv > prefer_mv &&
						 mv < cur_mv)
						prefer_cur = 1;
				}
			/*
			 * pick the largest power if we don't see one staisfy
			 * desired power
			 */
			} else if (cur_uw == 0 || uw > cur_uw) {
				prefer_cur = 1;
			}
		}

		/* Prefer higher power, except for tiebreaker */
		has_preferred_pdo =
			prefer_cur ||
			(!IS_ENABLED(CONFIG_USB_PD_PREFER_MV) && uw > cur_uw);

		if (has_preferred_pdo) {
			ret = i;
			cur_uw = uw;
			cur_mv = mv;
		}
	}

	if (selected_pdo)
		*selected_pdo = src_caps[ret];

	return ret;
}

void pd_extract_pdo_power(uint32_t pdo, uint32_t *ma, uint32_t *mv)
{
	int max_ma, uw;

	*mv = ((pdo >> 10) & 0x3FF) * 50;

	if (*mv == 0) {
		*ma = 0;
		return;
	}

	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		uw = 250000 * (pdo & 0x3FF);
		max_ma = 1000 * MIN(1000 * uw, PD_MAX_POWER_MW) / *mv;
	} else {
		max_ma = 10 * (pdo & 0x3FF);
		max_ma = MIN(max_ma, PD_MAX_POWER_MW * 1000 / *mv);
	}

	*ma = MIN(max_ma, PD_MAX_CURRENT_MA);
}

void pd_build_request(uint32_t src_cap_cnt, const uint32_t * const src_caps,
			int32_t vpd_vdo, uint32_t *rdo, uint32_t *ma,
			uint32_t *mv, enum pd_request_type req_type,
			uint32_t max_request_mv, int port)
{
	uint32_t pdo;
	int pdo_index, flags = 0;
	int uw;
	int max_or_min_ma;
	int max_or_min_mw;
	int max_vbus;
	int vpd_vbus_dcr;
	int vpd_gnd_dcr;

	if (req_type == PD_REQUEST_VSAFE5V) {
		/* src cap 0 should be vSafe5V */
		pdo_index = 0;
		pdo = src_caps[0];
	} else {
		/* find pdo index for max voltage we can request */
		pdo_index = pd_find_pdo_index(src_cap_cnt, src_caps,
						max_request_mv, &pdo);
	}

	pd_extract_pdo_power(pdo, ma, mv);

	/*
	 * Adjust VBUS current if CTVPD device was detected.
	 */
	if (vpd_vdo > 0) {
		max_vbus = VPD_VDO_MAX_VBUS(vpd_vdo);
		vpd_vbus_dcr = VPD_VDO_VBUS_IMP(vpd_vdo) << 1;
		vpd_gnd_dcr = VPD_VDO_GND_IMP(vpd_vdo);

		/*
		 * Valid max_vbus values:
		 *   00b - 20000 mV
		 *   01b - 30000 mV
		 *   10b - 40000 mV
		 *   11b - 50000 mV
		 */
		max_vbus = 20000 + max_vbus * 10000;
		if (*mv > max_vbus)
			*mv = max_vbus;

		/*
		 * 5000 mA cable: 150 = 750000 / 50000
		 * 3000 mA cable: 250 = 750000 / 30000
		 */
		if (*ma > 3000)
			*ma = 750000 / (150 + vpd_vbus_dcr + vpd_gnd_dcr);
		else
			*ma = 750000 / (250 + vpd_vbus_dcr + vpd_gnd_dcr);
	}

	uw = *ma * *mv;
	/* Mismatch bit set if less power offered than the operating power */
	if (uw < (1000 * PD_OPERATING_POWER_MW))
		flags |= RDO_CAP_MISMATCH;

#ifdef CONFIG_USB_PD_GIVE_BACK
		/* Tell source we are give back capable. */
		flags |= RDO_GIVE_BACK;

		/*
		 * BATTERY PDO: Inform the source that the sink will reduce
		 * power to this minimum level on receipt of a GotoMin Request.
		 */
		max_or_min_mw = PD_MIN_POWER_MW;

		/*
		 * FIXED or VARIABLE PDO: Inform the source that the sink will
		 * reduce current to this minimum level on receipt of a GotoMin
		 * Request.
		 */
		max_or_min_ma = PD_MIN_CURRENT_MA;
#else
		/*
		 * Can't give back, so set maximum current and power to
		 * operating level.
		 */
		max_or_min_ma = *ma;
		max_or_min_mw = uw / 1000;
#endif

	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		int mw = uw / 1000;
		*rdo = RDO_BATT(pdo_index + 1, mw, max_or_min_mw, flags);
	} else {
		*rdo = RDO_FIXED(pdo_index + 1, *ma, max_or_min_ma, flags);
	}

	/*
	 * Ref: USB Power Delivery Specification
	 * (Revision 3.0, Version 2.0 / Revision 2.0, Version 1.3)
	 * 6.4.2.4 USB Communications Capable
	 * 6.4.2.5 No USB Suspend
	 *
	 * If the port partner is capable of USB communication set the
	 * USB Communications Capable flag.
	 * If the port partner is sink device do not suspend USB as the
	 * power can be used for charging.
	 */
	if (pd_get_partner_usb_comm_capable(port)) {
		*rdo |= RDO_COMM_CAP;
		if (pd_get_power_role(port) == PD_ROLE_SINK)
			*rdo |= RDO_NO_SUSPEND;
	}
}
#endif

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
void notify_sysjump_ready(volatile const task_id_t * const sysjump_task_waiting)
{
	/*
	 * If event was set from pd_prepare_sysjump, wake the
	 * task waiting on us to complete.
	 */
	if (*sysjump_task_waiting != TASK_ID_INVALID)
		task_set_event(*sysjump_task_waiting,
						TASK_EVENT_SYSJUMP_READY, 0);
}
#endif

__attribute__((weak)) uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

enum pd_drp_next_states drp_auto_toggle_next_state(
	uint64_t *drp_sink_time,
	enum pd_power_role power_role,
	enum pd_dual_role_states drp_state,
	enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2)
{
	/* Set to appropriate port state */
	if (cc_is_open(cc1, cc2)) {
		/*
		 * If nothing is attached then use drp_state to determine next
		 * state. If DRP auto toggle is still on, then remain in the
		 * DRP_AUTO_TOGGLE state. Otherwise, stop dual role toggling
		 * and go to a disconnected state.
		 */
		switch (drp_state) {
		case PD_DRP_TOGGLE_OFF:
			return DRP_TC_DEFAULT;
		case PD_DRP_FREEZE:
			if (power_role == PD_ROLE_SINK)
				return DRP_TC_UNATTACHED_SNK;
			else
				return DRP_TC_UNATTACHED_SRC;
		case PD_DRP_FORCE_SINK:
			return DRP_TC_UNATTACHED_SNK;
		case PD_DRP_FORCE_SOURCE:
			return DRP_TC_UNATTACHED_SRC;
		case PD_DRP_TOGGLE_ON:
		default:
			return DRP_TC_DRP_AUTO_TOGGLE;
		}
	} else if ((cc_is_rp(cc1) || cc_is_rp(cc2)) &&
		drp_state != PD_DRP_FORCE_SOURCE) {
		/* SNK allowed unless ForceSRC */
		return DRP_TC_UNATTACHED_SNK;
	} else if (cc_is_at_least_one_rd(cc1, cc2) ||
					cc_is_audio_acc(cc1, cc2)) {
		/*
		 * SRC allowed unless ForceSNK or Toggle Off
		 *
		 * Ideally we wouldn't use auto-toggle when drp_state is
		 * TOGGLE_OFF/FORCE_SINK, but for some TCPCs, auto-toggle can't
		 * be prevented in low power mode. Try being a sink in case the
		 * connected device is dual-role (this ensures reliable charging
		 * from a hub, b/72007056). 100 ms is enough time for a
		 * dual-role partner to switch from sink to source. If the
		 * connected device is sink-only, then we will attempt
		 * TC_UNATTACHED_SNK twice (due to debounce time), then return
		 * to low power mode (and stay there). After 200 ms, reset
		 * ready for a new connection.
		 */
		if (drp_state == PD_DRP_TOGGLE_OFF ||
			drp_state == PD_DRP_FORCE_SINK) {
			if (get_time().val > *drp_sink_time + 200*MSEC)
				*drp_sink_time = get_time().val;
			if (get_time().val < *drp_sink_time + 100*MSEC)
				return DRP_TC_UNATTACHED_SNK;
			else
				return DRP_TC_DRP_AUTO_TOGGLE;
		} else {
			return DRP_TC_UNATTACHED_SRC;
		}
	} else {
		/* Anything else, keep toggling */
		return DRP_TC_DRP_AUTO_TOGGLE;
	}
}

mux_state_t get_mux_mode_to_set(int port)
{
	/*
	 * If the SoC is down, then we disconnect the MUX to save power since
	 * no one cares about the data lines.
	 */
	if (IS_ENABLED(CONFIG_POWER_COMMON) &&
	    chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF))
		return USB_PD_MUX_NONE;

	/*
	 * When PD stack is disconnected, then mux should be disconnected, which
	 * is also what happens in the set_state disconnection code. Once the
	 * PD state machine progresses out of disconnect, the MUX state will
	 * be set correctly again.
	 */
	if (pd_is_disconnected(port))
		return USB_PD_MUX_NONE;

	/* If new data role isn't DFP & we only support DFP, also disconnect. */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    IS_ENABLED(CONFIG_USBC_SS_MUX_DFP_ONLY) &&
	    pd_get_data_role(port) != PD_ROLE_DFP)
		return USB_PD_MUX_NONE;

	/*
	 * If the power role is sink and the partner device is not capable
	 * of USB communication then disconnect.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    pd_get_power_role(port) == PD_ROLE_SINK &&
	    !pd_get_partner_usb_comm_capable(port))
		return USB_PD_MUX_NONE;

	/* Otherwise connect mux since we are in S3+ */
	return USB_PD_MUX_USB_ENABLED;
}

void set_usb_mux_with_current_data_role(int port)
{
	if (IS_ENABLED(CONFIG_USBC_SS_MUX)) {
		mux_state_t mux_mode = get_mux_mode_to_set(port);
		enum usb_switch usb_switch_mode =
				(mux_mode == USB_PD_MUX_NONE) ?
				USB_SWITCH_DISCONNECT : USB_SWITCH_CONNECT;

		usb_mux_set(port, mux_mode, usb_switch_mode,
				pd_get_polarity(port));
	}
}

#ifdef CONFIG_USBC_PPC

static void pd_send_hard_reset(int port)
{
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SEND_HARD_RESET, 0);
}

static uint32_t port_oc_reset_req;

static void re_enable_ports(void)
{
	uint32_t ports = atomic_read_clear(&port_oc_reset_req);

	while (ports) {
		int port = __fls(ports);

		ports &= ~BIT(port);

		/*
		 * Let the board know that the overcurrent is
		 * over since we're going to attempt re-enabling
		 * the port.
		 */
		board_overcurrent_event(port, 0);

		pd_send_hard_reset(port);
		/*
		 * TODO(b/117854867): PD3.0 to send an alert message
		 * indicating OCP after explicit contract.
		 */
	}
}
DECLARE_DEFERRED(re_enable_ports);

void pd_handle_overcurrent(int port)
{
	/* Keep track of the overcurrent events. */
	CPRINTS("C%d: overcurrent!", port);

	if (IS_ENABLED(CONFIG_USB_PD_LOGGING))
		pd_log_event(PD_EVENT_PS_FAULT, PD_LOG_PORT_SIZE(port, 0),
			PS_FAULT_OCP, NULL);

	ppc_add_oc_event(port);
	/* Let the board specific code know about the OC event. */
	board_overcurrent_event(port, 1);

	/* Wait 1s before trying to re-enable the port. */
	atomic_or(&port_oc_reset_req, BIT(port));
	hook_call_deferred(&re_enable_ports_data, SECOND);
}

void pd_handle_cc_overvoltage(int port)
{
	pd_send_hard_reset(port);
}

#endif /* CONFIG_USBC_PPC */

__overridable int pd_board_checks(void)
{
	return EC_SUCCESS;
}

__overridable int pd_check_data_swap(int port,
	enum pd_data_role data_role)
{
	/* Allow data swap if we are a UFP, otherwise don't allow. */
	return (data_role == PD_ROLE_UFP) ? 1 : 0;
}

__overridable void pd_check_dr_role(int port,
	enum pd_data_role dr_role, int flags)
{
	/* If UFP, try to switch to DFP */
	if ((flags & PD_FLAGS_PARTNER_DR_DATA) && dr_role == PD_ROLE_UFP)
		pd_request_data_swap(port);
}

__overridable int pd_check_power_swap(int port)
{
	/*
	 * Allow power swap if we are acting as a dual role device.  If we are
	 * not acting as dual role (ex. suspended), then only allow power swap
	 * if we are sourcing when we could be sinking.
	 */
	if (pd_get_dual_role(port) == PD_DRP_TOGGLE_ON)
		return 1;
	else if (pd_get_power_role(port) == PD_ROLE_SOURCE)
		return 1;

	return 0;
}

__overridable void pd_check_pr_role(int port,
	enum pd_power_role pr_role, int flags)
{
	/*
	 * If partner is dual-role power and dualrole toggling is on, consider
	 * if a power swap is necessary.
	 */
	if ((flags & PD_FLAGS_PARTNER_DR_POWER) &&
	    pd_get_dual_role(port) == PD_DRP_TOGGLE_ON) {
		/*
		 * If we are a sink and partner is not unconstrained, then
		 * swap to become a source. If we are source and partner is
		 * unconstrained, swap to become a sink.
		 */
		int partner_unconstrained = flags & PD_FLAGS_PARTNER_UNCONSTR;

		if ((!partner_unconstrained && pr_role == PD_ROLE_SINK) ||
		     (partner_unconstrained && pr_role == PD_ROLE_SOURCE))
			pd_request_power_swap(port);
	}
}

__overridable void pd_execute_data_swap(int port,
	enum pd_data_role data_role)
{
}

__overridable void pd_try_execute_vconn_swap(int port, int flags)
{
	/*
	 * If partner is dual-role power and vconn swap is enabled, consider
	 * if vconn swapping is necessary.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    IS_ENABLED(CONFIG_USBC_VCONN_SWAP))
		pd_try_vconn_src(port);
}

__overridable int pd_is_valid_input_voltage(int mv)
{
	return 1;
}

__overridable void pd_transition_voltage(int idx)
{
	/* Most devices are fixed 5V output. */
}

__overridable void typec_set_source_current_limit(int p, enum tcpc_rp_value rp)
{
	if (IS_ENABLED(CONFIG_USBC_PPC))
		ppc_set_vbus_source_current_limit(p, rp);
}

/* ---------------- Power Data Objects (PDOs) ----------------- */
#ifndef CONFIG_USB_PD_CUSTOM_PDO
#define PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP |\
			 PDO_FIXED_COMM_CAP)

const uint32_t pd_src_pdo[] = {
	PDO_FIXED(5000, 1500, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);
const uint32_t pd_src_pdo_max[] = {
	PDO_FIXED(5000, 3000, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_max_cnt = ARRAY_SIZE(pd_src_pdo_max);

const uint32_t pd_snk_pdo[] = {
	PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
	PDO_BATT(4750, PD_MAX_VOLTAGE_MV, PD_OPERATING_POWER_MW),
	PDO_VAR(4750, PD_MAX_VOLTAGE_MV, PD_MAX_CURRENT_MA),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);
#endif /* CONFIG_USB_PD_CUSTOM_PDO */

/* ----------------- Vendor Defined Messages ------------------ */
__overridable int pd_custom_vdm(int port, int cnt, uint32_t *payload,
				uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);
	uint16_t dev_id = 0;
	int is_rw, is_latest;

	/* make sure we have some payload */
	if (cnt == 0)
		return 0;

	switch (cmd) {
	case VDO_CMD_VERSION:
		/* guarantee last byte of payload is null character */
		*(payload + cnt - 1) = 0;
		CPRINTF("version: %s\n", (char *)(payload+1));
		break;
	case VDO_CMD_READ_INFO:
	case VDO_CMD_SEND_INFO:
		/* copy hash */
		if (cnt == 7) {
			dev_id = VDO_INFO_HW_DEV_ID(payload[6]);
			is_rw = VDO_INFO_IS_RW(payload[6]);

			is_latest = pd_dev_store_rw_hash(port,
							 dev_id,
							 payload + 1,
							 is_rw ?
							 SYSTEM_IMAGE_RW :
							 SYSTEM_IMAGE_RO);

			/*
			 * Send update host event unless our RW hash is
			 * already known to be the latest update RW.
			 */
			if (!is_rw || !is_latest)
				pd_send_host_event(PD_EVENT_UPDATE_DEVICE);

			CPRINTF("DevId:%d.%d SW:%d RW:%d\n",
				HW_DEV_ID_MAJ(dev_id),
				HW_DEV_ID_MIN(dev_id),
				VDO_INFO_SW_DBG_VER(payload[6]),
				is_rw);
		} else if (cnt == 6) {
			/* really old devices don't have last byte */
			pd_dev_store_rw_hash(port, dev_id, payload + 1,
					     SYSTEM_IMAGE_UNKNOWN);
		}
		break;
	case VDO_CMD_CURRENT:
		CPRINTF("Current: %dmA\n", payload[1]);
		break;
	case VDO_CMD_FLIP:
		if (IS_ENABLED(CONFIG_USBC_SS_MUX))
			usb_mux_flip(port);
		break;
#ifdef CONFIG_USB_PD_LOGGING
	case VDO_CMD_GET_LOG:
		pd_log_recv_vdm(port, cnt, payload);
		break;
#endif /* CONFIG_USB_PD_LOGGING */
	}

	return 0;
}

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
/*
 * Before entering into alternate mode, state of the USB-C MUX
 * needs to be in safe mode.
 * Ref: USB Type-C Cable and Connector Specification
 * Section E.2.2 Alternate Mode Electrical Requirements
 */
void usb_mux_set_safe_mode(int port)
{
	usb_mux_set(port, IS_ENABLED(CONFIG_USB_MUX_VIRTUAL) ?
		USB_PD_MUX_SAFE_MODE : USB_PD_MUX_NONE,
		USB_SWITCH_CONNECT, pd_get_polarity(port));

	/* Isolate the SBU lines. */
	if (IS_ENABLED(CONFIG_USBC_PPC_SBU))
		ppc_set_sbu(port, 0);
}

__overridable const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

int dp_flags[CONFIG_USB_PD_PORT_MAX_COUNT];
uint32_t dp_status[CONFIG_USB_PD_PORT_MAX_COUNT];

__overridable void svdm_safe_dp_mode(int port)
{
	/* make DP interface safe until configure */
	dp_flags[port] = 0;
	dp_status[port] = 0;

	usb_mux_set_safe_mode(port);
}

__overridable int svdm_enter_dp_mode(int port, uint32_t mode_caps)
{
	/*
	 * Don't enter the mode if the SoC is off.
	 *
	 * There's no need to enter the mode while the SoC is off; we'll
	 * actually enter the mode on the chipset resume hook.  Entering DP Alt
	 * Mode twice will confuse some monitors and require and unplug/replug
	 * to get them to work again.  The DP Alt Mode on USB-C spec says that
	 * if we don't need to maintain HPD connectivity info in a low power
	 * mode, then we shall exit DP Alt Mode.  (This is why we don't enter
	 * when the SoC is off as opposed to suspend where adding a display
	 * could cause a wake up.)
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return -1;

	/* Only enter mode if device is DFP_D capable */
	if (mode_caps & MODE_DP_SNK) {
		svdm_safe_dp_mode(port);

		if (IS_ENABLED(CONFIG_MKBP_EVENT) &&
		    chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
			/*
			 * Wake the system up since we're entering DP AltMode.
			 */
			pd_notify_dp_alt_mode_entry();

		return 0;
	}

	return -1;
}

__overridable int svdm_dp_status(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);

	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			 CMD_DP_STATUS | VDO_OPOS(opos));
	payload[1] = VDO_DP_STATUS(0, /* HPD IRQ  ... not applicable */
				   0, /* HPD level ... not applicable */
				   0, /* exit DP? ... no */
				   0, /* usb mode? ... no */
				   0, /* multi-function ... no */
				   (!!(dp_flags[port] & DP_FLAGS_DP_ON)),
				   0, /* power low? ... no */
				   (!!DP_FLAGS_DP_ON));
	return 2;
};

__overridable uint8_t get_dp_pin_mode(int port)
{
	return pd_dfp_dp_get_pin_mode(port, dp_status[port]);
}

__overridable int svdm_dp_config(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);
	int mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]);
	uint8_t pin_mode = get_dp_pin_mode(port);
	mux_state_t mux_mode;

	if (!pin_mode)
		return 0;

	/*
	 * Multi-function operation is only allowed if that pin config is
	 * supported.
	 */
	mux_mode = ((pin_mode & MODE_DP_PIN_MF_MASK) && mf_pref) ?
		USB_PD_MUX_DOCK : USB_PD_MUX_DP_ENABLED;
	CPRINTS("pin_mode: %x, mf: %d, mux: %d", pin_mode, mf_pref, mux_mode);

	/* Connect the SBU and USB lines to the connector. */
	if (IS_ENABLED(CONFIG_USBC_PPC_SBU))
		ppc_set_sbu(port, 1);
	usb_mux_set(port, mux_mode, USB_SWITCH_CONNECT, pd_get_polarity(port));

	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			 CMD_DP_CONFIG | VDO_OPOS(opos));
	payload[1] = VDO_DP_CFG(pin_mode,      /* pin mode */
				1,	       /* DPv1.3 signaling */
				2);	       /* UFP connected */
	return 2;
};

/*
 * timestamp of the next possible toggle to ensure the 2-ms spacing
 * between IRQ_HPD.  Since this is used in overridable functions, this
 * has to be global.
 */
uint64_t svdm_hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];

#ifndef PORT_TO_HPD
#define PORT_TO_HPD(port) ((port) ? GPIO_USB_C1_DP_HPD : GPIO_USB_C0_DP_HPD)
#endif /* PORT_TO_HPD */

__overridable void svdm_dp_post_config(int port)
{
	dp_flags[port] |= DP_FLAGS_DP_ON;
	if (!(dp_flags[port] & DP_FLAGS_HPD_HI_PENDING))
		return;

#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	gpio_set_level(PORT_TO_HPD(port), 1);

	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	usb_mux_hpd_update(port, 1, 0);

#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, 1);
#endif
}

__overridable int svdm_dp_attention(int port, uint32_t *payload)
{
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);
#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	enum gpio_signal hpd = PORT_TO_HPD(port);
	int cur_lvl = gpio_get_level(hpd);
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	dp_status[port] = payload[1];

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
	    (irq || lvl))
		/*
		 * Wake up the AP.  IRQ or level high indicates a DP sink is now
		 * present.
		 */
		if (IS_ENABLED(CONFIG_MKBP_EVENT))
			pd_notify_dp_alt_mode_entry();

	/* Its initial DP status message prior to config */
	if (!(dp_flags[port] & DP_FLAGS_DP_ON)) {
		if (lvl)
			dp_flags[port] |= DP_FLAGS_HPD_HI_PENDING;
		return 1;
	}

#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	if (irq & cur_lvl) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < svdm_hpd_deadline[port])
			usleep(svdm_hpd_deadline[port] - now);

		/* generate IRQ_HPD pulse */
		gpio_set_level(hpd, 0);
		usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
		gpio_set_level(hpd, 1);

		/* set the minimum time delay (2ms) for the next HPD IRQ */
		svdm_hpd_deadline[port] = get_time().val +
			HPD_USTREAM_DEBOUNCE_LVL;
	} else if (irq & !lvl) {
		/*
		 * IRQ can only be generated when the level is high, because
		 * the IRQ is signaled by a short low pulse from the high level.
		 */
		CPRINTF("ERR:HPD:IRQ&LOW\n");
		return 0; /* nak */
	} else {
		gpio_set_level(hpd, lvl);
		/* set the minimum time delay (2ms) for the next HPD IRQ */
		svdm_hpd_deadline[port] = get_time().val +
			HPD_USTREAM_DEBOUNCE_LVL;
	}
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	usb_mux_hpd_update(port, lvl, irq);

#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, lvl);
#endif

	/* ack */
	return 1;
}

__overridable void svdm_exit_dp_mode(int port)
{
	svdm_safe_dp_mode(port);
#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	gpio_set_level(PORT_TO_HPD(port), 0);
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */
	usb_mux_hpd_update(port, 0, 0);
#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, 0);
#endif
}

__overridable int svdm_enter_gfu_mode(int port, uint32_t mode_caps)
{
	/* Always enter GFU mode */
	return 0;
}

__overridable void svdm_exit_gfu_mode(int port)
{
}

__overridable int svdm_gfu_status(int port, uint32_t *payload)
{
	/*
	 * This is called after enter mode is successful, send unstructured
	 * VDM to read info.
	 */
	pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_READ_INFO, NULL, 0);
	return 0;
}

__overridable int svdm_gfu_config(int port, uint32_t *payload)
{
	return 0;
}

__overridable int svdm_gfu_attention(int port, uint32_t *payload)
{
	return 0;
}

#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
__overridable int svdm_tbt_compat_enter_mode(int port, uint32_t mode_caps)
{
	return 0;
}

__overridable void svdm_tbt_compat_exit_mode(int port)
{
}

__overridable int svdm_tbt_compat_status(int port, uint32_t *payload)
{
	return 0;
}

__overridable int svdm_tbt_compat_config(int port, uint32_t *payload)
{
	return 0;
}

__overridable int svdm_tbt_compat_attention(int port, uint32_t *payload)
{
	return 0;
}
#endif /* CONFIG_USB_PD_TBT_COMPAT_MODE */

const struct svdm_amode_fx supported_modes[] = {
	{
		.svid = USB_SID_DISPLAYPORT,
		.enter = &svdm_enter_dp_mode,
		.status = &svdm_dp_status,
		.config = &svdm_dp_config,
		.post_config = &svdm_dp_post_config,
		.attention = &svdm_dp_attention,
		.exit = &svdm_exit_dp_mode,
	},

	{
		.svid = USB_VID_GOOGLE,
		.enter = &svdm_enter_gfu_mode,
		.status = &svdm_gfu_status,
		.config = &svdm_gfu_config,
		.attention = &svdm_gfu_attention,
		.exit = &svdm_exit_gfu_mode,
	},
#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
	{
		.svid = USB_VID_INTEL,
		.enter = &svdm_tbt_compat_enter_mode,
		.status = &svdm_tbt_compat_status,
		.config = &svdm_tbt_compat_config,
		.attention = &svdm_tbt_compat_attention,
		.exit = &svdm_tbt_compat_exit_mode,
	},
#endif /* CONFIG_USB_PD_TBT_COMPAT_MODE */
};
const int supported_modes_cnt = ARRAY_SIZE(supported_modes);
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

__overridable enum ec_pd_port_location board_get_pd_port_location(int port)
{
	(void)port;
	return EC_PD_PORT_LOCATION_UNKNOWN;
}

static enum ec_status hc_get_pd_port_caps(struct host_cmd_handler_args *args)
{
	const struct ec_params_get_pd_port_caps *p = args->params;
	struct ec_response_get_pd_port_caps *r = args->response;

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	/* Power Role */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE))
		r->pd_power_role_cap = EC_PD_POWER_ROLE_DUAL;
	else
		r->pd_power_role_cap = EC_PD_POWER_ROLE_SINK;

	/* Try-Power Role */
	if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		r->pd_try_power_role_cap = EC_PD_TRY_POWER_ROLE_SOURCE;
	else
		r->pd_try_power_role_cap = EC_PD_TRY_POWER_ROLE_NONE;

	if (IS_ENABLED(CONFIG_USB_TYPEC_VPD) ||
	    IS_ENABLED(CONFIG_USB_TYPEC_CTVPD))
		r->pd_data_role_cap = EC_PD_DATA_ROLE_UFP;
	else
		r->pd_data_role_cap = EC_PD_DATA_ROLE_DUAL;

	/* Allow boards to override the locations from UNKNOWN if desired */
	r->pd_port_location = board_get_pd_port_location(p->port);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PD_PORT_CAPS,
		     hc_get_pd_port_caps,
		     EC_VER_MASK(0));

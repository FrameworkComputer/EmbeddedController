/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Contains common USB functions shared between the old (i.e. usb_pd_protocol)
 * and the new (i.e. usb_sm_*) USB-C PD stacks.
 */

#include "atomic.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "stdbool.h"
#include "system.h"
#include "task.h"
#include "typec_control.h"
#include "usb_api.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"
#include "usb_pd_flags.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"
#include "usbc_ocp.h"
#include "usbc_ppc.h"
#include "util.h"

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 43

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

/*
 * If we are trying to upgrade PD firmwares (TCPC chips, retimer, etc), we
 * need to ensure the battery has enough charge for this process. Set the
 * threshold to 10%, and it should be enough charge to get us
 * through the EC jump to RW and PD upgrade.
 */
#define MIN_BATTERY_FOR_PD_UPGRADE_PERCENT 10 /* % */

#ifdef CONFIG_COMMON_RUNTIME
struct ec_params_usb_pd_rw_hash_entry rw_hash_table[RW_HASH_ENTRIES];
#endif /* CONFIG_COMMON_RUNTIME */

bool pd_firmware_upgrade_check_power_readiness(int port)
{
	if (IS_ENABLED(HAS_TASK_CHARGER)) {
		struct batt_params batt = { 0 };
		/*
		 * Cannot rely on the EC's active charger data as the
		 * EC may just rebooted into RW and has not necessarily
		 * picked the active charger yet. Charger task may not
		 * initialized, so check battery directly.
		 * Prevent the upgrade if the battery doesn't have enough
		 * charge to finish the upgrade.
		 */
		battery_get_params(&batt);
		if (batt.flags & BATT_FLAG_BAD_STATE_OF_CHARGE ||
		    batt.state_of_charge < MIN_BATTERY_FOR_PD_UPGRADE_PERCENT) {
			CPRINTS("C%d: Cannot suspend for upgrade, not "
				"enough battery (%d%%)!",
				port, batt.state_of_charge);
			return false;
		}
	} else {
		/* VBUS is present on the port (it is either a
		 * source or sink) to provide power, so don't allow
		 * PD firmware upgrade on the port.
		 */
		if (pd_is_vbus_present(port))
			return false;
	}

	return true;
}

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
					    enum tcpc_cc_voltage_status cc1,
					    enum tcpc_cc_voltage_status cc2)
{
	typec_current_t charge = 0;
	enum tcpc_cc_voltage_status cc;
	enum tcpc_cc_voltage_status cc_alt;

	cc = polarity_rm_dts(polarity) ? cc2 : cc1;
	cc_alt = polarity_rm_dts(polarity) ? cc1 : cc2;

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
	/* The following assumes:
	 *
	 * TYPEC_CC_VOLT_RP_3_0 > TYPEC_CC_VOLT_RP_1_5
	 * TYPEC_CC_VOLT_RP_1_5 > TYPEC_CC_VOLT_RP_DEF
	 * TYPEC_CC_VOLT_RP_DEF > TYPEC_CC_VOLT_OPEN
	 */
	if (cc_is_src_dbg_acc(cc1, cc2))
		return (cc1 > cc2) ? POLARITY_CC1_DTS : POLARITY_CC2_DTS;

	return (cc1 > cc2) ? POLARITY_CC1 : POLARITY_CC2;
}

enum tcpc_cc_polarity get_src_polarity(enum tcpc_cc_voltage_status cc1,
				       enum tcpc_cc_voltage_status cc2)
{
	return (cc1 == TYPEC_CC_VOLT_RD) ? POLARITY_CC1 : POLARITY_CC2;
}

enum pd_cc_states pd_get_cc_state(enum tcpc_cc_voltage_status cc1,
				  enum tcpc_cc_voltage_status cc2)
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

__overridable int pd_board_check_request(uint32_t rdo, int pdo_cnt)
{
	return EC_SUCCESS;
}

int pd_get_source_pdo(const uint32_t **src_pdo_p, const int port)
{
#if defined(CONFIG_USB_PD_TCPMV2) && defined(CONFIG_USB_PE_SM)
	const uint32_t *src_pdo;
	const int pdo_cnt = dpm_get_source_pdo(&src_pdo, port);
#elif defined(CONFIG_USB_PD_DYNAMIC_SRC_CAP) || \
	defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT)
	const uint32_t *src_pdo;
	const int pdo_cnt = charge_manager_get_source_pdo(&src_pdo, port);
#else
	const uint32_t *src_pdo = pd_src_pdo;
	const int pdo_cnt = pd_src_pdo_cnt;
#endif

	*src_pdo_p = src_pdo;
	return pdo_cnt;
}

int pd_check_requested_voltage(uint32_t rdo, const int port)
{
	int max_ma = rdo & 0x3FF;
	int op_ma = (rdo >> 10) & 0x3FF;
	int idx = RDO_POS(rdo);
	uint32_t pdo;
	uint32_t pdo_ma;
	const uint32_t *src_pdo;
	int pdo_cnt;

	pdo_cnt = pd_get_source_pdo(&src_pdo, port);

	/* Check for invalid index */
	if (!idx || idx > pdo_cnt)
		return EC_ERROR_INVAL;

	/* Board specific check for this request */
	if (pd_board_check_request(rdo, pdo_cnt))
		return EC_ERROR_INVAL;

	/* check current ... */
	pdo = src_pdo[idx - 1];
	pdo_ma = (pdo & 0x3ff);

	if (op_ma > pdo_ma)
		return EC_ERROR_INVAL; /* too much op current */

	if (max_ma > pdo_ma && !(rdo & RDO_CAP_MISMATCH))
		return EC_ERROR_INVAL; /* too much max current */

	CPRINTF("Requested %d mV %d mA (for %d/%d mA)\n",
		((pdo >> 10) & 0x3ff) * 50, (pdo & 0x3ff) * 10, op_ma * 10,
		max_ma * 10);

	/* Accept the requested voltage */
	return EC_SUCCESS;
}

__overridable uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

__overridable bool board_is_usb_pd_port_present(int port)
{
	/*
	 * Use board_get_usb_pd_port_count() instead of checking
	 * CONFIG_USB_PD_PORT_MAX_COUNT directly here for legacy boards
	 * that implement board_get_usb_pd_port_count() but do not
	 * implement board_is_usb_pd_port_present().
	 */

	return (port >= 0) && (port < board_get_usb_pd_port_count());
}

__overridable bool board_is_dts_port(int port)
{
	return true;
}

int pd_get_retry_count(int port, enum tcpci_msg_type type)
{
	/* PD 3.0 6.7.7: nRetryCount = 2; PD 2.0 6.6.9: nRetryCount = 3 */
	return pd_get_rev(port, type) == PD_REV30 ? 2 : 3;
}

enum pd_drp_next_states drp_auto_toggle_next_state(
	uint64_t *drp_sink_time, enum pd_power_role power_role,
	enum pd_dual_role_states drp_state, enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2, bool auto_toggle_supported)
{
	const bool hardware_debounced_unattached =
		((drp_state == PD_DRP_TOGGLE_ON) && auto_toggle_supported);

	/* Set to appropriate port state */
	if (cc_is_open(cc1, cc2) || cc_is_pwred_cbl_without_snk(cc1, cc2)) {
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
			if (!auto_toggle_supported) {
				if (power_role == PD_ROLE_SINK)
					return DRP_TC_UNATTACHED_SNK;
				else
					return DRP_TC_UNATTACHED_SRC;
			}

			return DRP_TC_DRP_AUTO_TOGGLE;
		}
	} else if ((cc_is_rp(cc1) || cc_is_rp(cc2)) &&
		   drp_state != PD_DRP_FORCE_SOURCE) {
		/* SNK allowed unless ForceSRC */
		if (hardware_debounced_unattached)
			return DRP_TC_ATTACHED_WAIT_SNK;
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
			if (get_time().val > *drp_sink_time + 200 * MSEC)
				*drp_sink_time = get_time().val;
			if (get_time().val < *drp_sink_time + 100 * MSEC)
				return DRP_TC_UNATTACHED_SNK;
			else
				return DRP_TC_DRP_AUTO_TOGGLE;
		} else {
			if (hardware_debounced_unattached)
				return DRP_TC_ATTACHED_WAIT_SRC;
			return DRP_TC_UNATTACHED_SRC;
		}
	} else {
		/* Anything else, keep toggling */
		if (!auto_toggle_supported) {
			if (power_role == PD_ROLE_SINK)
				return DRP_TC_UNATTACHED_SNK;
			else
				return DRP_TC_UNATTACHED_SRC;
		}

		return DRP_TC_DRP_AUTO_TOGGLE;
	}
}

__overridable bool usb_ufp_check_usb3_enable(int port)
{
	return false;
}

mux_state_t get_mux_mode_to_set(int port)
{
	/*
	 * If the SoC is down, then we disconnect the MUX to save power since
	 * no one cares about the data lines.
	 */
	if (IS_ENABLED(CONFIG_AP_POWER_CONTROL) &&
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

	/*
	 * For type-c only connections, there may be a need to enable USB3.1
	 * mode when the port is in a UFP data role, independent of any other
	 * conditions which are checked below. The default function returns
	 * false, so only boards that override this check will be affected.
	 */
	if (usb_ufp_check_usb3_enable(port) &&
	    pd_get_data_role(port) == PD_ROLE_UFP)
		return USB_PD_MUX_USB_ENABLED;

	/* If new data role isn't DFP & we only support DFP, also disconnect. */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    IS_ENABLED(CONFIG_USBC_SS_MUX_DFP_ONLY) &&
	    pd_get_data_role(port) != PD_ROLE_DFP)
		return USB_PD_MUX_NONE;

	/* If new data role isn't UFP & we only support UFP then disconnect. */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    IS_ENABLED(CONFIG_USBC_SS_MUX_UFP_ONLY) &&
	    pd_get_data_role(port) != PD_ROLE_UFP)
		return USB_PD_MUX_NONE;

	/*
	 * If the power role is sink and the PD partner device is not capable
	 * of USB communication then disconnect.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    pd_get_power_role(port) == PD_ROLE_SINK && pd_capable(port) &&
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
			(mux_mode == USB_PD_MUX_NONE) ? USB_SWITCH_DISCONNECT :
							USB_SWITCH_CONNECT;

		usb_mux_set(port, mux_mode, usb_switch_mode,
			    polarity_rm_dts(pd_get_polarity(port)));
	}
}

void usb_mux_set_safe_mode(int port)
{
	if (IS_ENABLED(CONFIG_USBC_SS_MUX)) {
		usb_mux_set(port, USB_PD_MUX_SAFE_MODE, USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));
	}

	/* Isolate the SBU lines. */
	typec_set_sbu(port, false);
}

void usb_mux_set_safe_mode_exit(int port)
{
	if (IS_ENABLED(CONFIG_USBC_SS_MUX))
		usb_mux_set(port, USB_PD_MUX_NONE, USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(port)));

	/* Isolate the SBU lines. */
	typec_set_sbu(port, false);
}

void pd_send_hard_reset(int port)
{
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SEND_HARD_RESET);
}

#ifdef CONFIG_USBC_OCP
void pd_handle_overcurrent(int port)
{
	if ((port < 0) || (port >= board_get_usb_pd_port_count())) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return;
	}

	CPRINTS("C%d: overcurrent!", port);

	if (IS_ENABLED(CONFIG_USB_PD_LOGGING))
		pd_log_event(PD_EVENT_PS_FAULT, PD_LOG_PORT_SIZE(port, 0),
			     PS_FAULT_OCP, NULL);

	/* No action to take if disconnected, just log. */
	if (pd_is_disconnected(port))
		return;

	/*
	 * Keep track of the overcurrent events and allow the module to perform
	 * the spec-dictated recovery actions.
	 */
	usbc_ocp_add_event(port);
}

#endif /* CONFIG_USBC_OCP */

__maybe_unused void pd_handle_cc_overvoltage(int port)
{
	pd_send_hard_reset(port);
}

__overridable int pd_board_checks(void)
{
	return EC_SUCCESS;
}

__overridable int pd_check_data_swap(int port, enum pd_data_role data_role)
{
	/* Allow data swap if we are a UFP, otherwise don't allow. */
	return (data_role == PD_ROLE_UFP) ? 1 : 0;
}

__overridable int pd_check_power_swap(int port)
{
#ifdef CONFIG_CHARGE_MANAGER
	/*
	 * If the Type-C port is our active charge port and we don't have a
	 * battery, don't allow power role swap (to source).
	 */
	if (!IS_ENABLED(CONFIG_BATTERY) &&
	    port == charge_manager_get_active_charge_port())
		return 0;
#endif

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

__overridable void pd_execute_data_swap(int port, enum pd_data_role data_role)
{
}

__overridable enum pd_dual_role_states pd_get_drp_state_in_suspend(void)
{
	/* Disable dual role when going to suspend */
	return PD_DRP_TOGGLE_OFF;
}

__overridable enum pd_dual_role_states pd_get_drp_state_in_s0(void)
{
	/* Enable dual role when chipset on */
	return PD_DRP_TOGGLE_ON;
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

__overridable bool vboot_allow_usb_pd(void)
{
	return false;
}

/* VDM utility functions */
static void pd_usb_billboard_deferred(void)
{
	if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE) &&
	    !IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP) &&
	    !IS_ENABLED(CONFIG_USB_PD_SIMPLE_DFP) &&
	    IS_ENABLED(CONFIG_USB_BOS)) {
		/*
		 * TODO(tbroch)
		 * 1. Will we have multiple type-C port UFPs
		 * 2. Will there be other modes applicable to DFPs besides DP
		 */
		if (!pd_alt_mode(0, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT))
			usb_connect();
	}
}
DECLARE_DEFERRED(pd_usb_billboard_deferred);

#ifdef CONFIG_USB_PD_DISCHARGE
static void gpio_discharge_vbus(int port, int enable)
{
#ifdef CONFIG_USB_PD_DISCHARGE_GPIO
	enum gpio_signal dischg_gpio[] = {
		GPIO_USB_C0_DISCHARGE,
#if CONFIG_USB_PD_PORT_MAX_COUNT > 1
		GPIO_USB_C1_DISCHARGE,
#endif
#if CONFIG_USB_PD_PORT_MAX_COUNT > 2
		GPIO_USB_C2_DISCHARGE,
#endif
	};
	BUILD_ASSERT(ARRAY_SIZE(dischg_gpio) == CONFIG_USB_PD_PORT_MAX_COUNT);

	gpio_set_level(dischg_gpio[port], enable);
#endif /* CONFIG_USB_PD_DISCHARGE_GPIO */
}

void pd_set_vbus_discharge(int port, int enable)
{
	static mutex_t discharge_lock[CONFIG_USB_PD_PORT_MAX_COUNT];
#ifdef CONFIG_ZEPHYR
	static bool inited[CONFIG_USB_PD_PORT_MAX_COUNT];
#endif

	if (port >= board_get_usb_pd_port_count())
		return;

#ifdef CONFIG_ZEPHYR
	if (!inited[port]) {
		(void)k_mutex_init(&discharge_lock[port]);
		inited[port] = true;
	}
#endif
	mutex_lock(&discharge_lock[port]);
	enable &= !board_vbus_source_enabled(port);

	if (get_usb_pd_discharge() == USB_PD_DISCHARGE_GPIO) {
		gpio_discharge_vbus(port, enable);
	} else if (get_usb_pd_discharge() == USB_PD_DISCHARGE_TCPC) {
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
		tcpc_discharge_vbus(port, enable);
#endif
	} else if (get_usb_pd_discharge() == USB_PD_DISCHARGE_PPC) {
#ifdef CONFIG_USB_PD_DISCHARGE_PPC
		ppc_discharge_vbus(port, enable);
#endif
	}

	mutex_unlock(&discharge_lock[port]);
}
#endif /* CONFIG_USB_PD_DISCHARGE */

#ifdef CONFIG_USB_PD_TCPM_TCPCI
static atomic_t pd_ports_to_resume;
static void resume_pd_port(void)
{
	uint32_t port;
	uint32_t suspended_ports = atomic_clear(&pd_ports_to_resume);

	while (suspended_ports) {
		port = __builtin_ctz(suspended_ports);
		suspended_ports &= ~BIT(port);
		pd_set_suspend(port, 0);
	}
}
DECLARE_DEFERRED(resume_pd_port);

void pd_deferred_resume(int port)
{
	atomic_or(&pd_ports_to_resume, 1 << port);
	hook_call_deferred(&resume_pd_port_data, 5 * SECOND);
}
#endif /* CONFIG_USB_PD_TCPM_TCPCI */

__overridable int pd_snk_is_vbus_provided(int port)
{
	return EC_SUCCESS;
}

/*
 * Check the specified Vbus level
 *
 * Note that boards may override this function if they have a method outside the
 * TCPCI driver to verify vSafe0V.
 */
__overridable bool pd_check_vbus_level(int port, enum vbus_level level)
{
	if (IS_ENABLED(CONFIG_USB_PD_VBUS_DETECT_TCPC) &&
	    (get_usb_pd_vbus_detect() == USB_PD_VBUS_DETECT_TCPC)) {
		return tcpm_check_vbus_level(port, level);
	} else if (level == VBUS_PRESENT)
		return pd_snk_is_vbus_provided(port);
	else
		return !pd_snk_is_vbus_provided(port);
}

int pd_is_vbus_present(int port)
{
	return pd_check_vbus_level(port, VBUS_PRESENT);
}

#ifdef CONFIG_USB_PD_FRS
__overridable int board_pd_set_frs_enable(int port, int enable)
{
	return EC_SUCCESS;
}

int pd_set_frs_enable(int port, int enable)
{
	int rv = EC_SUCCESS;

	if (IS_ENABLED(CONFIG_USB_PD_FRS_PPC))
		rv = ppc_set_frs_enable(port, enable);
	if ((rv == EC_SUCCESS || rv == EC_ERROR_UNIMPLEMENTED) &&
	    tcpm_tcpc_has_frs_control(port))
		rv = tcpm_set_frs_enable(port, enable);
	if (rv == EC_SUCCESS)
		rv = board_pd_set_frs_enable(port, enable);
	return rv;
}
#endif /* defined(CONFIG_USB_PD_FRS) */

#ifdef CONFIG_CMD_TCPC_DUMP
/*
 * Dump TCPC registers.
 */
void tcpc_dump_registers(int port, const struct tcpc_reg_dump_map *reg,
			 int count)
{
	for (int i = 0; i < count; i++, reg++) {
		int val;
		int rv;

		switch (reg->size) {
		case 1:
			rv = tcpc_read(port, reg->addr, &val);
			ccprintf("  %-30s(0x%02x) = ", reg->name, reg->addr);
			if (rv)
				ccprintf("ERR(%d)\n", rv);
			else
				ccprintf("  0x%02x\n", (uint8_t)val);
			break;
		case 2:
			rv = tcpc_read16(port, reg->addr, &val);
			ccprintf("  %-30s(0x%02x) = ", reg->name, reg->addr);
			if (rv)
				ccprintf("ERR(%d)\n", rv);
			else
				ccprintf("0x%04x\n", (uint16_t)val);
			break;
		}
		cflush();
	}
}

static int command_tcpc_dump(int argc, const char **argv)
{
	int port;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = atoi(argv[1]);
	if ((port < 0) || (port >= board_get_usb_pd_port_count())) {
		CPRINTS("%s(%d) Invalid port!", __func__, port);
		return EC_ERROR_INVAL;
	}
	/* Dump TCPC registers. */
	tcpm_dump_registers(port);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(tcpci_dump, command_tcpc_dump, "<Type-C port>",
			"dump the TCPC regs");
#endif /* defined(CONFIG_CMD_TCPC_DUMP) */

void pd_srccaps_dump(int port)
{
	int i;
	const uint32_t *const srccaps = pd_get_src_caps(port);

	for (i = 0; i < pd_get_src_cap_cnt(port); ++i) {
		uint32_t max_ma, max_mv, min_mv;

#ifdef CONFIG_CMD_PD_SRCCAPS_REDUCED_SIZE
		pd_extract_pdo_power(srccaps[i], &max_ma, &max_mv, &min_mv);

		if ((srccaps[i] & PDO_TYPE_MASK) == PDO_TYPE_AUGMENTED) {
			if (IS_ENABLED(CONFIG_USB_PD_REV30))
				ccprintf("%d: %dmV-%dmV/%dmA\n", i, min_mv,
					 max_mv, max_ma);
		} else {
			ccprintf("%d: %dmV/%dmA\n", i, max_mv, max_ma);
		}
#else
		const uint32_t pdo = srccaps[i];
		const uint32_t pdo_mask = pdo & PDO_TYPE_MASK;
		const char *pdo_type = "?";
		bool range_flag = true;

		pd_extract_pdo_power(pdo, &max_ma, &max_mv, &min_mv);

		switch (pdo_mask) {
		case PDO_TYPE_FIXED:
			pdo_type = "Fixed";
			range_flag = false;
			break;
		case PDO_TYPE_BATTERY:
			pdo_type = "Battery";
			break;
		case PDO_TYPE_VARIABLE:
			pdo_type = "Variable";
			break;
		case PDO_TYPE_AUGMENTED:
			pdo_type = "Augmnt";
			if (!IS_ENABLED(CONFIG_USB_PD_REV30)) {
				pdo_type = "Aug3.0";
				range_flag = false;
			}
			break;
		}

		ccprintf("Src %d: (%s) %dmV", i, pdo_type, max_mv);
		if (range_flag)
			ccprintf("-%dmV", min_mv);
		ccprintf("/%dm%c", max_ma,
			 pdo_mask == PDO_TYPE_BATTERY ? 'W' : 'A');

		if (pdo & PDO_FIXED_DUAL_ROLE)
			ccprintf(" DRP");
		if (pdo & PDO_FIXED_UNCONSTRAINED)
			ccprintf(" UP");
		if (pdo & PDO_FIXED_COMM_CAP)
			ccprintf(" USB");
		if (pdo & PDO_FIXED_DATA_SWAP)
			ccprintf(" DRD");
		/* Note from ectool.c: FRS bits are reserved in PD 2.0 spec */
		if (pdo & PDO_FIXED_FRS_CURR_MASK)
			ccprintf(" FRS");
		ccprintf("\n");
#endif
	}
}

int pd_broadcast_alert_msg(uint32_t ado)
{
#if defined(CONFIG_USB_PD_TCPMV2) && defined(CONFIG_USB_PE_SM) && \
	!defined(CONFIG_USB_VPD) && !defined(CONFIG_USB_CTVPD)
	int ret = EC_SUCCESS;

	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (pd_send_alert_msg(i, ado) != EC_SUCCESS)
			ret = EC_ERROR_BUSY;
	}

	return ret;
#else
	return EC_ERROR_INVALID_CONFIG;
#endif
}

int pd_send_alert_msg(int port, uint32_t ado)
{
#if defined(CONFIG_USB_PD_TCPMV2) && defined(CONFIG_USB_PE_SM) && \
	!defined(CONFIG_USB_VPD) && !defined(CONFIG_USB_CTVPD)
	struct rmdo partner_rmdo;

	/*
	 * The Alert Data Object (ADO) definition changed between USB PD
	 * Revision 3.0 and 3.1. Clear reserved bits from the USB PD 3.0
	 * ADO before sending to a USB PD 3.0 partner and block the
	 * message if the ADO is empty.
	 */
	partner_rmdo = pd_get_partner_rmdo(port);
	if (partner_rmdo.major_rev == 0) {
		ado &= ~(ADO_EXTENDED_ALERT_EVENT |
			 ADO_EXTENDED_ALERT_EVENT_TYPE);
	}

	if (!ado)
		return EC_ERROR_INVAL;

	if (pe_set_ado(port, ado) != EC_SUCCESS)
		return EC_ERROR_BUSY;

	pd_dpm_request(port, DPM_REQUEST_SEND_ALERT);
	return EC_SUCCESS;
#else
	return EC_ERROR_INVALID_CONFIG;
#endif
}

#ifdef CONFIG_MKBP_EVENT
static int dp_alt_mode_entry_get_next_event(uint8_t *data)
{
	return EC_SUCCESS;
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_DP_ALT_MODE_ENTERED,
		     dp_alt_mode_entry_get_next_event);
#endif /* CONFIG_MKBP_EVENT */

__overridable void pd_notify_dp_alt_mode_entry(int port)
{
	if (IS_ENABLED(CONFIG_MKBP_EVENT)) {
		(void)port;
		CPRINTS("Notifying AP of DP Alt Mode Entry...");
		mkbp_send_event(EC_MKBP_EVENT_DP_ALT_MODE_ENTERED);
	}
}

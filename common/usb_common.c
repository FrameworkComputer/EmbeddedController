/* Copyright 2019 The Chromium OS Authors. All rights reserved.
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
#include "hooks.h"
#include "stdbool.h"
#include "host_command.h"
#include "system.h"
#include "task.h"
#include "usb_api.h"
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

__overridable void board_vbus_present_change(void)
{
}

#if defined(CONFIG_CMD_PD) && defined(CONFIG_CMD_PD_FLASH)
int hex8tou32(char *str, uint32_t *val)
{
	char *ptr = str;
	uint32_t tmp = 0;

	while (*ptr) {
		char c = *ptr++;

		if (c >= '0' && c <= '9')
			tmp = (tmp << 4) + (c - '0');
		else if (c >= 'A' && c <= 'F')
			tmp = (tmp << 4) + (c - 'A' + 10);
		else if (c >= 'a' && c <= 'f')
			tmp = (tmp << 4) + (c - 'a' + 10);
		else
			return EC_ERROR_INVAL;
	}
	if (ptr != str + 8)
		return EC_ERROR_INVAL;
	*val = tmp;
	return EC_SUCCESS;
}

int remote_flashing(int argc, char **argv)
{
	int port, cnt, cmd;
	uint32_t data[VDO_MAX_SIZE-1];
	char *e;
	static int flash_offset[CONFIG_USB_PD_PORT_MAX_COUNT];

	if (argc < 4 || argc > (VDO_MAX_SIZE + 4 - 1))
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 10);
	if (*e || port >= board_get_usb_pd_port_count())
		return EC_ERROR_PARAM2;

	cnt = 0;
	if (!strcasecmp(argv[3], "erase")) {
		cmd = VDO_CMD_FLASH_ERASE;
		flash_offset[port] = 0;
		ccprintf("ERASE ...");
	} else if (!strcasecmp(argv[3], "reboot")) {
		cmd = VDO_CMD_REBOOT;
		ccprintf("REBOOT ...");
	} else if (!strcasecmp(argv[3], "signature")) {
		cmd = VDO_CMD_ERASE_SIG;
		ccprintf("ERASE SIG ...");
	} else if (!strcasecmp(argv[3], "info")) {
		cmd = VDO_CMD_READ_INFO;
		ccprintf("INFO...");
	} else if (!strcasecmp(argv[3], "version")) {
		cmd = VDO_CMD_VERSION;
		ccprintf("VERSION...");
	} else {
		int i;

		argc -= 3;
		for (i = 0; i < argc; i++)
			if (hex8tou32(argv[i+3], data + i))
				return EC_ERROR_INVAL;
		cmd = VDO_CMD_FLASH_WRITE;
		cnt = argc;
		ccprintf("WRITE %d @%04x ...", argc * 4,
			 flash_offset[port]);
		flash_offset[port] += argc * 4;
	}

	pd_send_vdm(port, USB_VID_GOOGLE, cmd, data, cnt);

	/* Wait until VDM is done */
	while (pd[port].vdm_state > 0)
		task_wait_event(100*MSEC);

	ccprintf("DONE %d\n", pd[port].vdm_state);
	return EC_SUCCESS;
}
#endif /* defined(CONFIG_CMD_PD) && defined(CONFIG_CMD_PD_FLASH) */

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

/**
 * This function checks the current CC status of the port partner
 * and returns true if the attached partner is debug accessory.
 */
bool pd_is_debug_acc(int port)
{
	enum pd_cc_states cc_state = pd_get_task_cc_state(port);

	return cc_state == PD_CC_UFP_DEBUG_ACC ||
		cc_state == PD_CC_DFP_DEBUG_ACC;
}

void pd_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	tcpm_set_polarity(port, polarity);

	if (IS_ENABLED(CONFIG_USBC_PPC_POLARITY))
		ppc_set_polarity(port, polarity);
}

__overridable int pd_board_check_request(uint32_t rdo, int pdo_cnt)
{
	return EC_SUCCESS;
}

int pd_check_requested_voltage(uint32_t rdo, const int port)
{
	int max_ma = rdo & 0x3FF;
	int op_ma = (rdo >> 10) & 0x3FF;
	int idx = RDO_POS(rdo);
	uint32_t pdo;
	uint32_t pdo_ma;
#if defined(CONFIG_USB_PD_DYNAMIC_SRC_CAP) || \
		defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT)
	const uint32_t *src_pdo;
	const int pdo_cnt = charge_manager_get_source_pdo(&src_pdo, port);
#else
	const uint32_t *src_pdo = pd_src_pdo;
	const int pdo_cnt = pd_src_pdo_cnt;
#endif

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
		 ((pdo >> 10) & 0x3ff) * 50, (pdo & 0x3ff) * 10,
		 op_ma * 10, max_ma * 10);

	/* Accept the requested voltage */
	return EC_SUCCESS;
}

__overridable uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

enum pd_drp_next_states drp_auto_toggle_next_state(
	uint64_t *drp_sink_time,
	enum pd_power_role power_role,
	enum pd_dual_role_states drp_state,
	enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2,
	bool auto_toggle_supported)
{
	const bool hardware_debounced_unattached =
				((drp_state == PD_DRP_TOGGLE_ON) &&
				 auto_toggle_supported);

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
			if (get_time().val > *drp_sink_time + 200*MSEC)
				*drp_sink_time = get_time().val;
			if (get_time().val < *drp_sink_time + 100*MSEC)
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

void usb_mux_set_safe_mode(int port)
{
	if (IS_ENABLED(CONFIG_USBC_SS_MUX)) {
		usb_mux_set(port, IS_ENABLED(CONFIG_USB_MUX_VIRTUAL) ?
			USB_PD_MUX_SAFE_MODE : USB_PD_MUX_NONE,
			USB_SWITCH_CONNECT, pd_get_polarity(port));
	}

	/* Isolate the SBU lines. */
	if (IS_ENABLED(CONFIG_USBC_PPC_SBU))
		ppc_set_sbu(port, 0);
}

static void pd_send_hard_reset(int port)
{
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SEND_HARD_RESET, 0);
}

#ifdef CONFIG_USBC_PPC

static uint32_t port_oc_reset_req;

static void re_enable_ports(void)
{
	uint32_t ports = deprecated_atomic_read_clear(&port_oc_reset_req);

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
	CPRINTS("C%d: overcurrent!", port);

	if (IS_ENABLED(CONFIG_USB_PD_LOGGING))
		pd_log_event(PD_EVENT_PS_FAULT, PD_LOG_PORT_SIZE(port, 0),
			PS_FAULT_OCP, NULL);

	/* No action to take if disconnected, just log. */
	if (pd_is_disconnected(port))
		return;

	/* Keep track of the overcurrent events. */
	ppc_add_oc_event(port);

	/* Let the board specific code know about the OC event. */
	board_overcurrent_event(port, 1);

	/* Wait 1s before trying to re-enable the port. */
	deprecated_atomic_or(&port_oc_reset_req, BIT(port));
	hook_call_deferred(&re_enable_ports_data, SECOND);
}

#endif /* CONFIG_USBC_PPC */

__maybe_unused void pd_handle_cc_overvoltage(int port)
{
	pd_send_hard_reset(port);
}

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

__overridable void pd_execute_data_swap(int port,
	enum pd_data_role data_role)
{
}

__overridable enum pd_dual_role_states pd_get_drp_state_in_suspend(void)
{
	/* Disable dual role when going to suspend */
	return PD_DRP_TOGGLE_OFF;
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
#if defined(CONFIG_USB_PE_SM) && !defined(CONFIG_USB_VPD) && \
	!defined(CONFIG_USB_CTVPD)
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

			is_latest = pd_dev_store_rw_hash(
				port, dev_id, payload + 1,
				is_rw ? EC_IMAGE_RW : EC_IMAGE_RO);

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
					     EC_IMAGE_UNKNOWN);
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
#endif /* CONFIG_USB_PE_SM && !CONFIG_USB_VPD && !CONFIG_USB_CTVPD */

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
		if (!pd_alt_mode(0, TCPC_TX_SOP, USB_SID_DISPLAYPORT))
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
	static struct mutex discharge_lock[CONFIG_USB_PD_PORT_MAX_COUNT];

	if (port >= board_get_usb_pd_port_count())
		return;

	mutex_lock(&discharge_lock[port]);
	enable &= !board_vbus_source_enabled(port);

	if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE_GPIO))
		gpio_discharge_vbus(port, enable);
	else if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE_TCPC))
		tcpc_discharge_vbus(port, enable);
	else if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE_PPC))
		ppc_discharge_vbus(port, enable);

	mutex_unlock(&discharge_lock[port]);
}
#endif /* CONFIG_USB_PD_DISCHARGE */

#ifdef CONFIG_USB_PD_TCPM_TCPCI
static uint32_t pd_ports_to_resume;
static void resume_pd_port(void)
{
	uint32_t port;
	uint32_t suspended_ports =
		deprecated_atomic_read_clear(&pd_ports_to_resume);

	while (suspended_ports) {
		port = __builtin_ctz(suspended_ports);
		suspended_ports &= ~BIT(port);
		pd_set_suspend(port, 0);
	}
}
DECLARE_DEFERRED(resume_pd_port);

void pd_deferred_resume(int port)
{
	deprecated_atomic_or(&pd_ports_to_resume, 1 << port);
	hook_call_deferred(&resume_pd_port_data, 5 * SECOND);
}
#endif /* CONFIG_USB_PD_TCPM_TCPCI */


/*
 * Check the specified Vbus level
 *
 * Note that boards may override this function if they have a method outside the
 * TCPCI driver to verify vSafe0V.
 */
__overridable bool pd_check_vbus_level(int port, enum vbus_level level)
{
	if (IS_ENABLED(CONFIG_USB_PD_VBUS_DETECT_TCPC))
		return tcpm_check_vbus_level(port, level);
	else if (level == VBUS_PRESENT)
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
	if (rv == EC_SUCCESS && IS_ENABLED(CONFIG_USB_PD_FRS_TCPC))
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
	int i, val;

	for (i = 0; i < count; i++, reg++) {
		switch (reg->size) {
		case 1:
			tcpc_read(port, reg->addr, &val);
			ccprintf("  %-30s(0x%02x) =   0x%02x\n",
				reg->name, reg->addr, (uint8_t)val);
			break;
		case 2:
			tcpc_read16(port, reg->addr, &val);
			ccprintf("  %-30s(0x%02x) = 0x%04x\n",
				reg->name, reg->addr, (uint16_t)val);
			break;
		}
		cflush();
	}

}

static int command_tcpc_dump(int argc, char **argv)
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

int pd_build_alert_msg(uint32_t *msg, uint32_t *len, enum pd_power_role pr)
{
	if (msg == NULL || len == NULL)
		return EC_ERROR_INVAL;

	/*
	 * SOURCE: currently only supports OCP
	 * SINK:   currently only supports OVP
	 */
	if (pr == PD_ROLE_SOURCE)
		*msg = ADO_OCP_EVENT;
	else
		*msg = ADO_OVP_EVENT;

	/* Alert data is 4 bytes */
	*len = 4;

	return EC_SUCCESS;
}

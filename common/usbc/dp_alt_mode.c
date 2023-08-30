/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * DisplayPort alternate mode support
 * Refer to VESA DisplayPort Alt Mode on USB Type-C Standard, version 2.0,
 * section 5.2
 */

#include "atomic.h"
#include "builtin/assert.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "timer.h"
#include "typec_control.h"
#include "usb_common.h"
#include "usb_dp_alt_mode.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dp_hpd_gpio.h"
#include "usb_pd_tbt.h"
#include "usb_pd_tcpm.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/*
 * Note: the following DP-related variables must be kept as-is since
 * some boards are using them in their board-specific code.
 * TODO(b/267545470): Fold board DP code into the DP module
 */

/*
 * timestamp of the next possible toggle to ensure the 2-ms spacing
 * between IRQ_HPD.  Since this is used in overridable functions, this
 * has to be global.
 *
 * Note: This variable is also defined in the AP VDM control module and it
 * is assumed that the two will never be compiled together, as the modules are
 * mutually exclusive.
 */
uint64_t svdm_hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];

int dp_flags[CONFIG_USB_PD_PORT_MAX_COUNT];

uint32_t dp_status[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Console command multi-function preference set for a PD port. */
__maybe_unused bool dp_port_mf_allow[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[0 ... CONFIG_USB_PD_PORT_MAX_COUNT - 1] = true
};

/* The state of the DP negotiation */
enum dp_states {
	DP_START = 0,
	DP_ENTER_ACKED,
	DP_ENTER_NAKED,
	DP_STATUS_ACKED,
	DP_PREPARE_CONFIG,
	DP_ACTIVE,
	DP_ENTER_RETRY,
	DP_PREPARE_EXIT,
	DP_INACTIVE,
	DP_STATE_COUNT
};
static enum dp_states dp_state[CONFIG_USB_PD_PORT_MAX_COUNT];

/*
 * Map of states to expected VDM commands in responses.
 * Default of 0 indicates no command expected.
 */
static const uint8_t state_vdm_cmd[DP_STATE_COUNT] = {
	[DP_START] = CMD_ENTER_MODE,	     [DP_ENTER_ACKED] = CMD_DP_STATUS,
	[DP_PREPARE_CONFIG] = CMD_DP_CONFIG, [DP_PREPARE_EXIT] = CMD_EXIT_MODE,
	[DP_ENTER_RETRY] = CMD_ENTER_MODE,
};

/*
 * Track if we're retrying due to an Enter Mode NAK
 */
#define DP_FLAG_RETRY BIT(0)

static atomic_t dpm_dp_flags[CONFIG_USB_PD_PORT_MAX_COUNT];

#define DP_SET_FLAG(port, flag) atomic_or(&dpm_dp_flags[port], (flag))
#define DP_CLR_FLAG(port, flag) atomic_clear_bits(&dpm_dp_flags[port], (flag))
#define DP_CHK_FLAG(port, flag) (dpm_dp_flags[port] & (flag))

/* Note: There is only one DP mode currently specified */
static const int dp_opos = 1;

bool dp_is_active(int port)
{
	return dp_state[port] == DP_ACTIVE || dp_state[port] == DP_PREPARE_EXIT;
}

bool dp_is_idle(int port)
{
	return dp_state[port] == DP_INACTIVE || dp_state[port] == DP_START;
}

void dp_init(int port)
{
	dp_state[port] = DP_START;
	dpm_dp_flags[port] = 0;
}

bool dp_entry_is_done(int port)
{
	return dp_state[port] == DP_ACTIVE || dp_state[port] == DP_INACTIVE;
}

static void dp_entry_failed(int port)
{
	CPRINTS("C%d: DP alt mode protocol failed!", port);
	dp_state[port] = DP_INACTIVE;
	dpm_dp_flags[port] = 0;
}

static bool dp_response_valid(int port, enum tcpci_msg_type type, char *cmdt,
			      int vdm_cmd)
{
	enum dp_states st = dp_state[port];

	/*
	 * Check for an unexpected response.
	 * If DP is inactive, ignore the command.
	 */
	if (type != TCPCI_MSG_SOP ||
	    (st != DP_INACTIVE && state_vdm_cmd[st] != vdm_cmd)) {
		CPRINTS("C%d: Received unexpected DP VDM %s (cmd %d) from"
			" %s in state %d",
			port, cmdt, vdm_cmd,
			type == TCPCI_MSG_SOP ? "port partner" : "cable plug",
			st);
		dp_entry_failed(port);
		return false;
	}
	return true;
}

static void dp_exit_to_usb_mode(int port)
{
	svdm_exit_dp_mode(port);
	pd_set_dfp_enter_mode_flag(port, false);

	set_usb_mux_with_current_data_role(port);

	CPRINTS("C%d: Exited DP mode", port);
	/*
	 * If the EC exits an alt mode autonomously, don't try to enter it
	 * again. If the AP commands the EC to exit DP mode, it might command
	 * the EC to enter again later, so leave the state machine ready for
	 * that possibility.
	 */
	dp_state[port] = DP_INACTIVE;
}

void dp_vdm_acked(int port, enum tcpci_msg_type type, int vdo_count,
		  uint32_t *vdm)
{
	const uint8_t vdm_cmd = PD_VDO_CMD(vdm[0]);

	if (!dp_response_valid(port, type, "ACK", vdm_cmd))
		return;

	/* TODO(b/155890173): Validate VDO count for specific commands */

	switch (dp_state[port]) {
	case DP_START:
	case DP_ENTER_RETRY:
		dp_state[port] = DP_ENTER_ACKED;
		/* Inform PE layer that alt mode is now active */
		pd_set_dfp_enter_mode_flag(port, true);
		break;
	case DP_ENTER_ACKED:
		/* DP status response & UFP's DP attention have same payload. */
		dfp_consume_attention(port, vdm);
		dp_state[port] = DP_STATUS_ACKED;
		break;
	case DP_PREPARE_CONFIG:
		svdm_dp_post_config(port);
		dp_state[port] = DP_ACTIVE;
		CPRINTS("C%d: Entered DP mode", port);
		break;
	case DP_PREPARE_EXIT:
		/*
		 * Request to exit mode successful, so put the module in an
		 * inactive state or give entry another shot.
		 */
		if (DP_CHK_FLAG(port, DP_FLAG_RETRY)) {
			dp_state[port] = DP_ENTER_RETRY;
			DP_CLR_FLAG(port, DP_FLAG_RETRY);
		} else {
			dp_exit_to_usb_mode(port);
		}
		break;
	case DP_INACTIVE:
		/*
		 * This can occur if the mode is shutdown because
		 * the CPU is being turned off, and an exit mode
		 * command has been sent.
		 */
		break;
	default:
		/* Invalid or unexpected negotiation state */
		CPRINTF("%s called with invalid state %d\n", __func__,
			dp_state[port]);
		dp_entry_failed(port);
		break;
	}
}

void dp_vdm_naked(int port, enum tcpci_msg_type type, uint8_t vdm_cmd)
{
	if (!dp_response_valid(port, type, "NAK", vdm_cmd))
		return;

	switch (dp_state[port]) {
	case DP_START:
		/*
		 * If a request to enter DP mode is NAK'ed, this likely
		 * means the partner is already in DP alt mode, so
		 * request to exit the mode first before retrying
		 * the enter command. This can happen if the EC
		 * is restarted (e.g to go into recovery mode) while
		 * DP alt mode is active.
		 */
		dp_state[port] = DP_ENTER_NAKED;
		break;
	case DP_ENTER_RETRY:
		/*
		 * Another NAK on the second attempt to enter DP mode.
		 * Give up.
		 */
		dp_entry_failed(port);
		break;
	case DP_PREPARE_EXIT:
		/* Treat an Exit Mode NAK the same as an Exit Mode ACK. */
		dp_exit_to_usb_mode(port);
		break;
	default:
		CPRINTS("C%d: NAK for cmd %d in state %d", port, vdm_cmd,
			dp_state[port]);
		dp_entry_failed(port);
		break;
	}
}

enum dpm_msg_setup_status dp_setup_next_vdm(int port, int *vdo_count,
					    uint32_t *vdm)
{
	uint32_t mode_vdos[VDO_MAX_OBJECTS];
	int vdo_count_ret;

	if (*vdo_count < VDO_MAX_SIZE)
		return MSG_SETUP_ERROR;

	switch (dp_state[port]) {
	case DP_START:
	case DP_ENTER_RETRY:
		/* Enter the first supported mode for DisplayPort. */
		if (pd_get_mode_vdo_for_svid(port, TCPCI_MSG_SOP,
					     USB_SID_DISPLAYPORT,
					     mode_vdos) == 0)
			return MSG_SETUP_ERROR;

		if (svdm_enter_dp_mode(port, mode_vdos[dp_opos - 1]) < 0)
			return MSG_SETUP_ERROR;
		vdm[0] = VDO(USB_SID_DISPLAYPORT, 1,
			     CMD_ENTER_MODE | VDO_OPOS(dp_opos));
		/* CMDT_INIT is 0, so this is a no-op */
		vdm[0] |= VDO_CMDT(CMDT_INIT);
		vdm[0] |= VDO_SVDM_VERS_MAJOR(
			pd_get_vdo_ver(port, TCPCI_MSG_SOP));
		vdm[0] |= VDM_VERS_MINOR;

		vdo_count_ret = 1;
		if (dp_state[port] == DP_START)
			CPRINTS("C%d: Attempting to enter DP mode", port);
		break;
	case DP_ENTER_ACKED:
		vdo_count_ret = svdm_dp_status(port, vdm);
		if (vdo_count_ret == 0)
			return MSG_SETUP_ERROR;
		vdm[0] |= PD_VDO_OPOS(dp_opos);
		vdm[0] |= VDO_CMDT(CMDT_INIT);
		vdm[0] |= VDO_SVDM_VERS_MAJOR(
			pd_get_vdo_ver(port, TCPCI_MSG_SOP));
		vdm[0] |= VDM_VERS_MINOR;
		break;
	case DP_STATUS_ACKED:
		if (!get_dp_pin_mode(port))
			return MSG_SETUP_ERROR;

		dp_state[port] = DP_PREPARE_CONFIG;

		/*
		 * Place the USB Type-C pins that are to be re-configured to
		 * DisplayPort Configuration into the Safe state. For
		 * USB_PD_MUX_DOCK, the superspeed signals can remain
		 * connected. For USB_PD_MUX_DP_ENABLED, disconnect the
		 * superspeed signals here, before the pins are re-configured
		 * to DisplayPort (in svdm_dp_post_config, when we receive
		 * the config ack).
		 */
		if (svdm_dp_get_mux_mode(port) == USB_PD_MUX_DP_ENABLED) {
			usb_mux_set_safe_mode(port);
			return MSG_SETUP_MUX_WAIT;
		}
		/* Fall through if no mux set is needed */
		__fallthrough;
	case DP_PREPARE_CONFIG:
		vdo_count_ret = svdm_dp_config(port, vdm);
		if (vdo_count_ret == 0)
			return MSG_SETUP_ERROR;
		vdm[0] |= VDO_CMDT(CMDT_INIT);
		vdm[0] |= VDO_SVDM_VERS_MAJOR(
			pd_get_vdo_ver(port, TCPCI_MSG_SOP));
		vdm[0] |= VDM_VERS_MINOR;
		break;
	case DP_ENTER_NAKED:
		DP_SET_FLAG(port, DP_FLAG_RETRY);
		/* Fall through to send exit mode */
		__fallthrough;
	case DP_ACTIVE:
		/*
		 * Called to exit DP alt mode, either when the mode
		 * is active and the system is shutting down, or
		 * when an initial request to enter the mode is NAK'ed.
		 * This can happen if the EC is restarted (e.g to go
		 * into recovery mode) while DP alt mode is active.
		 */
		usb_mux_set_safe_mode_exit(port);
		dp_state[port] = DP_PREPARE_EXIT;
		return MSG_SETUP_MUX_WAIT;
	case DP_PREPARE_EXIT:
		/* DPM should call setup only after safe state is set */
		vdm[0] = VDO(USB_SID_DISPLAYPORT, 1, /* structured */
			     CMD_EXIT_MODE);

		vdm[0] |= VDO_OPOS(dp_opos);
		vdm[0] |= VDO_CMDT(CMDT_INIT);
		vdm[0] |= VDO_SVDM_VERS_MAJOR(
			pd_get_vdo_ver(port, TCPCI_MSG_SOP));
		vdm[0] |= VDM_VERS_MINOR;
		vdo_count_ret = 1;
		break;
	case DP_INACTIVE:
		/*
		 * DP mode is inactive.
		 */
		return MSG_SETUP_ERROR;
	default:
		CPRINTF("%s called with invalid state %d\n", __func__,
			dp_state[port]);
		return MSG_SETUP_ERROR;
	}

	if (vdo_count_ret) {
		*vdo_count = vdo_count_ret;
		return MSG_SETUP_SUCCESS;
	}

	return MSG_SETUP_UNSUPPORTED;
}

int svdm_dp_status(int port, uint32_t *payload)
{
	payload[0] =
		VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_STATUS | VDO_OPOS(dp_opos));
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

/*
 * This algorithm defaults to choosing higher pin config over lower ones in
 * order to prefer multi-function if desired.
 *
 *  NAME | SIGNALING | OUTPUT TYPE | MULTI-FUNCTION | PIN CONFIG
 * -------------------------------------------------------------
 *  A    |  USB G2   |  ?          | no             | 00_0001
 *  B    |  USB G2   |  ?          | yes            | 00_0010
 *  C    |  DP       |  CONVERTED  | no             | 00_0100
 *  D    |  PD       |  CONVERTED  | yes            | 00_1000
 *  E    |  DP       |  DP         | no             | 01_0000
 *  F    |  PD       |  DP         | yes            | 10_0000
 *
 * if UFP has NOT asserted multi-function preferred code masks away B/D/F
 * leaving only A/C/E.  For single-output dongles that should leave only one
 * possible pin config depending on whether its a converter DP->(VGA|HDMI) or DP
 * output.  If UFP is a USB-C receptacle it may assert C/D/E/F.  The DFP USB-C
 * receptacle must always choose C/D in those cases.
 */
int pd_dfp_dp_get_pin_mode(int port, uint32_t status)
{
	uint32_t mode_vdos[VDO_MAX_OBJECTS];
	uint32_t mode_caps;
	uint32_t pin_caps;
	int mf_pref;

	/*
	 * Default dp_port_mf_allow is true, we allow mf operation
	 * if UFP_D supports it.
	 */

	if (IS_ENABLED(CONFIG_CMD_MFALLOW))
		mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]) &&
			  dp_port_mf_allow[port];
	else
		mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]);

	if (pd_get_mode_vdo_for_svid(port, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT,
				     mode_vdos) == 0)
		return 0;

	mode_caps = mode_vdos[dp_opos - 1];

	/* TODO(crosbug.com/p/39656) revisit with DFP that can be a sink */
	pin_caps = PD_DP_PIN_CAPS(mode_caps);

	/* if don't want multi-function then ignore those pin configs */
	if (!mf_pref)
		pin_caps &= ~MODE_DP_PIN_MF_MASK;

	/* TODO(crosbug.com/p/39656) revisit if DFP drives USB Gen 2 signals */
	pin_caps &= ~MODE_DP_PIN_BR2_MASK;

	/* if C/D present they have precedence over E/F for USB-C->USB-C */
	if (pin_caps & (MODE_DP_PIN_C | MODE_DP_PIN_D))
		pin_caps &= ~(MODE_DP_PIN_E | MODE_DP_PIN_F);

	/* get_next_bit returns undefined for zero */
	if (!pin_caps)
		return 0;

	return 1 << get_next_bit(&pin_caps);
}

mux_state_t svdm_dp_get_mux_mode(int port)
{
	int pin_mode = get_dp_pin_mode(port);
	/* Default dp_port_mf_allow is true */
	int mf_pref;

	if (IS_ENABLED(CONFIG_CMD_MFALLOW))
		mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]) &&
			  dp_port_mf_allow[port];
	else
		mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]);

	/*
	 * Multi-function operation is only allowed if that pin config is
	 * supported.
	 */
	if ((pin_mode & MODE_DP_PIN_MF_MASK) && mf_pref)
		return USB_PD_MUX_DOCK;
	else
		return USB_PD_MUX_DP_ENABLED;
}

/*
 * Note: the following DP-related overridables must be kept as-is since
 * some boards are using them in their board-specific code.
 * TODO(b/267545470): Fold board DP code into the DP module
 */
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
	 * could cause a wake up.)  When in S5->S3 transition state, we
	 * should treat it as a SoC off state.
	 */
#ifdef CONFIG_AP_POWER_CONTROL
	if (!chipset_in_state(CHIPSET_STATE_ANY_SUSPEND | CHIPSET_STATE_ON))
		return -1;
#endif

		/*
		 * TCPMv2: Enable logging of CCD line state CCD_MODE_ODL.
		 * DisplayPort Alternate mode requires that the SBU lines are
		 * used for AUX communication. However, in Chromebooks SBU
		 * signals are repurposed as USB2 signals for CCD. This
		 * functionality is accomplished by override fets whose state is
		 * controlled by CCD_MODE_ODL.
		 *
		 * This condition helps in debugging unexpected AUX timeout
		 * issues by indicating the state of the CCD override fets.
		 */
#ifdef GPIO_CCD_MODE_ODL
	if (!gpio_get_level(GPIO_CCD_MODE_ODL))
		CPRINTS("WARNING: Tried to EnterMode DP with [CCD on AUX/SBU]");
#endif

	/* Only enter mode if device is DFP_D capable */
	if (mode_caps & MODE_DP_SNK) {
		svdm_safe_dp_mode(port);

		if (IS_ENABLED(CONFIG_MKBP_EVENT) &&
		    chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
			/*
			 * Wake the system up since we're entering DP AltMode.
			 */
			pd_notify_dp_alt_mode_entry(port);

		return 0;
	}

	return -1;
}

__overridable uint8_t get_dp_pin_mode(int port)
{
	return pd_dfp_dp_get_pin_mode(port, dp_status[port]);
}

__overridable bool board_is_dp_uhbr13_5_allowed(int port)
{
	return true;
}

bool dp_is_uhbr13_5_supported(int port)
{
	if (!board_is_dp_uhbr13_5_allowed(port))
		return false;

	union dp_mode_resp_cable cable_dp_mode_resp;

	cable_dp_mode_resp.raw_value =
		dp_get_mode_vdo(port, TCPCI_MSG_SOP_PRIME);

	return cable_dp_mode_resp.uhbr13_5_support;
}

union dp_mode_cfg dp_create_vdo_cfg(int port, uint8_t pin_mode)
{
	union dp_mode_cfg cfg_vdo = { .raw_value = 0 };

	cfg_vdo.cfg = DP_SINK;
	cfg_vdo.dfp_d_pin = pin_mode;
	if (IS_ENABLED(CONFIG_USB_PD_DP21_MODE) &&
	    dp_resolve_dpam_version(port, TCPCI_MSG_SOP) == DPAM_VERSION_21) {
		struct dp_cable_type_flags cable_flags;
		enum dp21_cable_type cable_type = DP21_PASSIVE_CABLE;

		cable_flags = dp_get_pd_cable_type_flags(port);

		if (cable_flags.optical) {
			cable_type = DP21_OPTICAL_CABLE;
		} else if (cable_flags.active) {
			cable_type = (cable_flags.retimer) ?
					     DP21_ACTIVE_RETIMER_CABLE :
					     DP21_ACTIVE_REDRIVER_CABLE;
		}

		cfg_vdo.signaling = dp_get_cable_bit_rate(port);
		cfg_vdo.uhbr13_5_support = dp_is_uhbr13_5_supported(port);
		cfg_vdo.active_comp = cable_type;
		cfg_vdo.dpam_ver = DPAM_VERSION_21;
	} else {
		cfg_vdo.signaling = DP_HBR3;
	}

	return cfg_vdo;
}

/* Note: Assumes that pins have already been set in safe state if necessary */
__overridable int svdm_dp_config(int port, uint32_t *payload)
{
	uint8_t pin_mode = get_dp_pin_mode(port);
	mux_state_t mux_mode = svdm_dp_get_mux_mode(port);
	/* Default dp_port_mf_allow is true */
	int mf_pref;

	if (IS_ENABLED(CONFIG_CMD_MFALLOW))
		mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]) &&
			  dp_port_mf_allow[port];
	else
		mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]);

	if (!pin_mode)
		return 0;

	CPRINTS("pin_mode: %x, mf: %d, mux: %d", pin_mode, mf_pref, mux_mode);

	payload[0] =
		VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_CONFIG | VDO_OPOS(dp_opos));
	payload[1] = dp_create_vdo_cfg(port, pin_mode).raw_value;

	return 2;
};

__overridable void svdm_dp_post_config(int port)
{
	mux_state_t mux_mode = svdm_dp_get_mux_mode(port);
	/* Connect the SBU and USB lines to the connector. */
	typec_set_sbu(port, true);

	usb_mux_set(port, mux_mode, USB_SWITCH_CONNECT,
		    polarity_rm_dts(pd_get_polarity(port)));

	dp_flags[port] |= DP_FLAGS_DP_ON;
	if (!(dp_flags[port] & DP_FLAGS_HPD_HI_PENDING))
		return;

	dp_hpd_gpio_set(port, true, false);

	usb_mux_hpd_update(port,
			   USB_PD_MUX_HPD_LVL | USB_PD_MUX_HPD_IRQ_DEASSERTED);

#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, 1);
#endif
}

__overridable int svdm_dp_attention(int port, uint32_t *payload)
{
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);
	mux_state_t mux_state;

	dp_status[port] = payload[1];

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) && (irq || lvl))
		/*
		 * Wake up the AP.  IRQ or level high indicates a DP sink is now
		 * present.
		 */
		if (IS_ENABLED(CONFIG_MKBP_EVENT))
			pd_notify_dp_alt_mode_entry(port);

	/* Its initial DP status message prior to config */
	if (!(dp_flags[port] & DP_FLAGS_DP_ON)) {
		if (lvl)
			dp_flags[port] |= DP_FLAGS_HPD_HI_PENDING;
		return 1;
	}

	if (dp_hpd_gpio_set(port, lvl, irq) != EC_SUCCESS)
		return 0;

	mux_state = (lvl ? USB_PD_MUX_HPD_LVL : USB_PD_MUX_HPD_LVL_DEASSERTED) |
		    (irq ? USB_PD_MUX_HPD_IRQ : USB_PD_MUX_HPD_IRQ_DEASSERTED);
	usb_mux_hpd_update(port, mux_state);

#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, lvl);
#endif

	/* ack */
	return 1;
}

__overridable void svdm_exit_dp_mode(int port)
{
	dp_flags[port] = 0;
	dp_status[port] = 0;
	dp_hpd_gpio_set(port, false, false);
	usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
					 USB_PD_MUX_HPD_IRQ_DEASSERTED);
#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, 0);
#endif
}

#ifdef CONFIG_CMD_MFALLOW
static int command_mfallow(int argc, const char **argv)
{
	char *e;
	int port;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 10);
	if (*e || port >= board_get_usb_pd_port_count())
		return EC_ERROR_PARAM1;

	if (!strcasecmp(argv[2], "true"))
		dp_port_mf_allow[port] = true;
	else if (!strcasecmp(argv[2], "false"))
		dp_port_mf_allow[port] = false;
	else
		return EC_ERROR_PARAM2;

	ccprintf("Port: %d multi function allowed is %s ", port, argv[2]);
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(mfallow, command_mfallow, "port [true | false]",
			"Controls Multifunction choice during DP Altmode.");
#endif

/* VESA DisplayPort Alt Mode on USB Type-C Standard
 * (DisplayPort Alt Mode) Version 2.1
 * Figure 5–3: Example Cable Support Flow
 * returns true if DP21 is not enabled
 */
bool dp_mode_entry_allowed(int port)
{
	if (!IS_ENABLED(CONFIG_USB_PD_DP21_MODE))
		return true;

	const struct pd_discovery *disc;
#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
	union tbt_mode_resp_cable tbt_cable_mode_resp;
#endif
	union dp_mode_resp_cable dp_cable_mode_resp;
	bool usb20_only;
	enum idh_ptype product_type;

	disc = pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);
	product_type = disc->identity.idh.product_type;

	if (product_type != IDH_PTYPE_PCABLE &&
	    product_type != IDH_PTYPE_ACABLE) {
		CPRINTF("Port: %d Not Emark Cable\n", port);
		return true;
	}

	dp_cable_mode_resp.raw_value =
		dp_get_mode_vdo(port, TCPCI_MSG_SOP_PRIME);
#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
	tbt_cable_mode_resp.raw_value =
		pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME);
#endif

	/* No DP Support, if passive cable and USB2.0 only */
	if (pd_get_rev(port, TCPCI_MSG_SOP_PRIME) == PD_REV30)
		usb20_only = (disc->identity.product_t1.p_rev30.ss ==
			      USB_R30_SS_U2_ONLY) ?
				     true :
				     false;
	else
		usb20_only = (disc->identity.product_t1.p_rev20.ss ==
			      USB_R20_SS_U2_ONLY) ?
				     true :
				     false;

	if (product_type == IDH_PTYPE_PCABLE && usb20_only)
		return false;

	/* No DP Support, if Active Cable and  Modal Operation = NO */
	if (product_type == IDH_PTYPE_ACABLE &&
	    !disc->identity.idh.modal_support)
		return false;

	/* No DP Support,
	 * if Active Cable, Modal Operation = Yes and !DPSID and !TBTSID
	 */
	if (product_type == IDH_PTYPE_ACABLE &&
	    disc->identity.idh.modal_support && !dp_cable_mode_resp.raw_value
#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
	    && !tbt_cable_mode_resp.raw_value
#endif
	)
		return false;

#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
	/* No DP Support,
	 * if Active/Passive Cable, Modal Operation = Yes and TBTSID
	 * and (Retimed Bit:22 = 1 or Thunderbolt Alt Mode VDO bit 25
	 * is Active.
	 */
	if ((product_type == IDH_PTYPE_ACABLE ||
	     product_type == IDH_PTYPE_PCABLE) &&
	    disc->identity.idh.modal_support && !dp_cable_mode_resp.raw_value &&
	    tbt_cable_mode_resp.raw_value &&
	    (tbt_cable_mode_resp.retimer_type ||
	     tbt_cable_mode_resp.tbt_active_passive))
		return false;
#endif

	return true;
}

uint32_t dp_get_mode_vdo(int port, enum tcpci_msg_type type)
{
	uint32_t dp_mode_vdo[VDO_MAX_OBJECTS];

	return pd_get_mode_vdo_for_svid(port, type, USB_SID_DISPLAYPORT,
					dp_mode_vdo) ?
		       dp_mode_vdo[0] :
		       0;
}

enum usb_pd_svdm_ver dp_resolve_svdm_version(int port, enum tcpci_msg_type type)
{
	int idx;
	const struct svid_mode_data *mode_discovery = NULL;
	const struct pd_discovery *disc;

	disc = pd_get_am_discovery(port, type);

	for (idx = 0; idx < disc->svid_cnt; ++idx) {
		if (pd_get_svid(port, idx, type) == USB_SID_DISPLAYPORT) {
			mode_discovery = &disc->svids[idx];
			break;
		}
	}

	if (mode_discovery)
		return disc->svdm_vers;

	return SVDM_VER_2_0;
}

enum dpam_version dp_resolve_dpam_version(int port, enum tcpci_msg_type type)
{
	union dp_mode_resp_cable discover_mode;

	if (dp_resolve_svdm_version(port, type) == SVDM_VER_2_1) {
		discover_mode.raw_value = dp_get_mode_vdo(port, type);
		if (discover_mode.dpam_ver) {
			return DPAM_VERSION_21;
		}
	}

	return DPAM_VERSION_20;
}

static enum dp_bit_rate usb_rev30_to_dp_speed(enum usb_rev30_ss ss)
{
	switch (ss) {
	case USB_R30_SS_U32_U40_GEN1:
	case USB_R30_SS_U32_U40_GEN2:
		return DP_UHBR10;
	case USB_R30_SS_U40_GEN3:
		return DP_UHBR20;
	default:
		return DP_HBR3;
	}
}

static enum dp_bit_rate usb_rev20_to_dp_speed(enum usb_rev20_ss ss)
{
	switch (ss) {
	case USB_R20_SS_U31_GEN1:
	case USB_R20_SS_U31_GEN1_GEN2:
		return DP_UHBR10;
	default:
		return DP_HBR3;
	}
}

#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
static enum dp_bit_rate tbt_to_dp_speed(enum tbt_compat_cable_speed ss)
{
	switch (ss) {
	case TBT_SS_U31_GEN1:
	case TBT_SS_U32_GEN1_GEN2:
		return DP_UHBR10;
	case TBT_SS_TBT_GEN3:
		return DP_UHBR20;
	default:
		return DP_HBR3;
	}
}
#endif

static enum dp_bit_rate dp_signaling_to_speed(uint8_t signaling)
{
	if (signaling & DP_UHBR20)
		return DP_UHBR20;
	else if (signaling & DP_UHBR10)
		return DP_UHBR10;

	return DP_HBR3;
}

enum dp_bit_rate dp_get_cable_bit_rate(int port)
{
	const struct pd_discovery *disc;
	union dp_mode_resp_cable dp_cable_mode_resp;
	union tbt_mode_resp_cable tbt_cable_mode_resp;
	enum idh_ptype product_type;

	disc = pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);
	product_type = disc->identity.idh.product_type;
	dp_cable_mode_resp.raw_value =
		IS_ENABLED(CONFIG_USB_PD_DP21_MODE) ?
			dp_get_mode_vdo(port, TCPCI_MSG_SOP_PRIME) :
			0;
	tbt_cable_mode_resp.raw_value =
		IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE) ?
			pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME) :
			0;

	/* Below logic is based on DP 2.1 Spec Figure 5-3 */
	if (product_type == IDH_PTYPE_PCABLE &&
	    (!disc->identity.idh.modal_support ||
	     (disc->identity.idh.modal_support &&
	      !dp_cable_mode_resp.raw_value &&
	      !tbt_cable_mode_resp.raw_value))) {
		if (pd_get_rev(port, TCPCI_MSG_SOP_PRIME) == PD_REV30)
			return usb_rev30_to_dp_speed(
				disc->identity.product_t1.p_rev30.ss);
		else
			return usb_rev20_to_dp_speed(
				disc->identity.product_t1.p_rev20.ss);
	}

	if ((product_type == IDH_PTYPE_ACABLE ||
	     product_type == IDH_PTYPE_PCABLE) &&
	    disc->identity.idh.modal_support == 1) {
		enum dpam_version cable_dpam_ver =
			dp_resolve_dpam_version(port, TCPCI_MSG_SOP_PRIME);
		if (cable_dpam_ver == DPAM_VERSION_21) {
			return dp_signaling_to_speed(
				dp_cable_mode_resp.signaling);
#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
		} else if ((tbt_cable_mode_resp.raw_value &&
			    !tbt_cable_mode_resp.retimer_type &&
			    !tbt_cable_mode_resp.tbt_active_passive)) {
			return tbt_to_dp_speed(get_tbt_cable_speed(port));
#endif
		}
	}
	return DP_HBR3;
}

/*
 * Combines the following information into a struct
 * Active/Passive cable
 * Retimer/Redriver cable
 * Optical/Non-optical cable
 */
struct dp_cable_type_flags dp_get_pd_cable_type_flags(int port)
{
	union tbt_mode_resp_cable tbt_cable_resp;
	union dp_mode_resp_cable dp_cable_resp;
	struct dp_cable_type_flags cable_flags = { 0 };

	if (!IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP) ||
	    !IS_ENABLED(CONFIG_USB_PD_DP21_MODE))
		return cable_flags;

	dp_cable_resp.raw_value = dp_get_mode_vdo(port, TCPCI_MSG_SOP_PRIME);
	tbt_cable_resp.raw_value =
		IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE) ?
			pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME) :
			0;

	if (dp_resolve_dpam_version(port, TCPCI_MSG_SOP_PRIME) ==
	    DPAM_VERSION_21) {
		cable_flags.active = (dp_cable_resp.active_comp ==
					      DP21_ACTIVE_RETIMER_CABLE ||
				      dp_cable_resp.active_comp ==
					      DP21_ACTIVE_REDRIVER_CABLE);
		cable_flags.retimer = (dp_cable_resp.active_comp ==
				       DP21_ACTIVE_RETIMER_CABLE);
		cable_flags.optical =
			(dp_cable_resp.active_comp == DP21_OPTICAL_CABLE);
	} else if (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE)) {
		cable_flags.active =
			(get_usb_pd_cable_type(port) == IDH_PTYPE_ACABLE ||
			 tbt_cable_resp.tbt_active_passive == TBT_CABLE_ACTIVE);
		cable_flags.retimer =
			(tbt_cable_resp.retimer_type == USB_RETIMER);
		cable_flags.optical =
			(tbt_cable_resp.tbt_cable == TBT_CABLE_OPTICAL);
	}
	return cable_flags;
}

/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Device Policy Manager implementation
 * Refer to USB PD 3.0 spec, version 2.0, sections 8.2 and 8.3
 */

#include "builtin/assert.h"
#include "charge_state.h"
#include "chipset.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "power.h"
#include "power_button.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "temp_sensor.h"
#include "usb_dp_alt_mode.h"
#include "usb_mode.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dpm.h"
#include "usb_pd_pdo.h"
#include "usb_pd_tcpm.h"
#include "usb_pd_timer.h"
#include "usb_pe_sm.h"
#include "usb_tbt_alt_mode.h"

#ifdef CONFIG_ZEPHYR
#include "temp_sensor/temp_sensor.h"
#endif

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
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
	enum dpm_pd_button_state pd_button_state;
} dpm[CONFIG_USB_PD_PORT_MAX_COUNT];

#define DPM_SET_FLAG(port, flag) atomic_or(&dpm[(port)].flags, (flag))
#define DPM_CLR_FLAG(port, flag) atomic_clear_bits(&dpm[(port)].flags, (flag))
#define DPM_CHK_FLAG(port, flag) (dpm[(port)].flags & (flag))

/* Flags for internal DPM state */
#define DPM_FLAG_MODE_ENTRY_DONE BIT(0)
#define DPM_FLAG_EXIT_REQUEST BIT(1)
#define DPM_FLAG_ENTER_DP BIT(2)
#define DPM_FLAG_ENTER_TBT BIT(3)
#define DPM_FLAG_ENTER_USB4 BIT(4)
#define DPM_FLAG_ENTER_ANY \
	(DPM_FLAG_ENTER_DP | DPM_FLAG_ENTER_TBT | DPM_FLAG_ENTER_USB4)
#define DPM_FLAG_SEND_ATTENTION BIT(5)
#define DPM_FLAG_DATA_RESET_REQUESTED BIT(6)
#define DPM_FLAG_DATA_RESET_DONE BIT(7)
#define DPM_FLAG_PD_BUTTON_PRESSED BIT(8)
#define DPM_FLAG_PD_BUTTON_RELEASED BIT(9)

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

__overridable bool board_is_tbt_usb4_port(int port)
{
	return true;
}

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
	if (DPM_CHK_FLAG(port, DPM_FLAG_ENTER_DP | DPM_FLAG_ENTER_TBT |
				       DPM_FLAG_ENTER_USB4))
		return EC_RES_BUSY;

	switch (mode) {
	case TYPEC_MODE_DP:
		if (dp_is_idle(port))
			dp_init(port);
		DPM_SET_FLAG(port, DPM_FLAG_ENTER_DP);
		break;
#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
	case TYPEC_MODE_TBT:
		/* TODO(b/235984702#comment21): Refactor alt mode modules
		 * to better support mode reentry. */
		if (dp_is_idle(port))
			dp_init(port);
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
	dpm[port].pd_button_state = DPM_PD_BUTTON_IDLE;
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
	DPM_CLR_FLAG(port, DPM_FLAG_DATA_RESET_DONE);
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
		IS_ENABLED(CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY) ? false : true;
	enum dpm_msg_setup_status status = MSG_SETUP_UNSUPPORTED;

	if (pd_get_data_role(port) != PD_ROLE_DFP) {
		if (DPM_CHK_FLAG(port, DPM_FLAG_ENTER_DP | DPM_FLAG_ENTER_TBT |
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

#ifdef CONFIG_AP_POWER_CONTROL
	/*
	 * Do not try to enter mode while CPU is off.
	 * CPU transitions (e.g b/158634281) can occur during the discovery
	 * phase or during enter/exit negotiations, and the state
	 * of the modes can get out of sync, causing the attempt to
	 * enter the mode to fail prematurely.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ANY_SUSPEND | CHIPSET_STATE_ON))
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
	if (IS_ENABLED(CONFIG_USB_PD_USB4) && board_is_tbt_usb4_port(port) &&
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
		status = tbt_setup_next_vdm(port, &vdo_count, vdm, &tx_type);
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

	/* First, try Data Reset. If Data Reset completes, all the alt mode
	 * state checked below will reset to its inactive state. If Data Reset
	 * is not supported, exit active modes individually.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_DATA_RESET_MSG)) {
		if (!DPM_CHK_FLAG(port, DPM_FLAG_DATA_RESET_REQUESTED) &&
		    !DPM_CHK_FLAG(port, DPM_FLAG_DATA_RESET_DONE)) {
			pd_dpm_request(port, DPM_REQUEST_DATA_RESET);
			DPM_SET_FLAG(port, DPM_FLAG_DATA_RESET_REQUESTED);
			return;
		} else if (!DPM_CHK_FLAG(port, DPM_FLAG_DATA_RESET_DONE)) {
			return;
		}
	}

	/* TODO(b/209625351): Data Reset is the only real way to exit from USB4
	 * mode. If that failed, the TCPM shouldn't try anything else.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_USB4) && enter_usb_entry_is_done(port)) {
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

	if (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE) && tbt_is_active(port)) {
		/*
		 * When the port is in USB4 mode and receives an exit request,
		 * it leaves USB4 SOP in active state.
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

void dpm_handle_alert(int port, uint32_t ado)
{
	if (ado & ADO_EXTENDED_ALERT_EVENT) {
		/* Extended Alert */
		if (pd_get_data_role(port) == PD_ROLE_DFP &&
		    (ADO_EXTENDED_ALERT_EVENT_TYPE & ado) ==
			    ADO_POWER_BUTTON_PRESS) {
			DPM_SET_FLAG(port, DPM_FLAG_PD_BUTTON_PRESSED);
		} else if (pd_get_data_role(port) == PD_ROLE_DFP &&
			   (ADO_EXTENDED_ALERT_EVENT_TYPE & ado) ==
				   ADO_POWER_BUTTON_RELEASE) {
			DPM_SET_FLAG(port, DPM_FLAG_PD_BUTTON_RELEASED);
		}
	}
}

static void dpm_run_pd_button_sm(int port)
{
#ifdef CONFIG_AP_POWER_CONTROL
	if (!IS_ENABLED(CONFIG_POWER_BUTTON_X86) &&
	    !IS_ENABLED(CONFIG_CHIPSET_SC7180) &&
	    !IS_ENABLED(CONFIG_CHIPSET_SC7280)) {
		/* Insufficient chipset API support for USB PD power button. */
		DPM_CLR_FLAG(port, DPM_FLAG_PD_BUTTON_PRESSED);
		DPM_CLR_FLAG(port, DPM_FLAG_PD_BUTTON_RELEASED);
		return;
	}

	/*
	 * Check for invalid flag combination. Alerts can only send a press or
	 * release event at once and only one flag should be set. If press and
	 * release flags are both set, we cannot know the order they were
	 * received. Clear the flags, disable the timer and return to an idle
	 * state.
	 */
	if (DPM_CHK_FLAG(port, DPM_FLAG_PD_BUTTON_PRESSED) &&
	    DPM_CHK_FLAG(port, DPM_FLAG_PD_BUTTON_RELEASED)) {
		DPM_CLR_FLAG(port, DPM_FLAG_PD_BUTTON_PRESSED |
					   DPM_FLAG_PD_BUTTON_RELEASED);
		pd_timer_disable(port, DPM_TIMER_PD_BUTTON_SHORT_PRESS);
		pd_timer_disable(port, DPM_TIMER_PD_BUTTON_LONG_PRESS);
		dpm[port].pd_button_state = DPM_PD_BUTTON_IDLE;
		return;
	}

	switch (dpm[port].pd_button_state) {
	case DPM_PD_BUTTON_IDLE:
		if (DPM_CHK_FLAG(port, DPM_FLAG_PD_BUTTON_PRESSED)) {
			pd_timer_enable(port, DPM_TIMER_PD_BUTTON_SHORT_PRESS,
					CONFIG_USB_PD_SHORT_PRESS_MAX_MS *
						MSEC);
			pd_timer_enable(port, DPM_TIMER_PD_BUTTON_LONG_PRESS,
					CONFIG_USB_PD_LONG_PRESS_MAX_MS * MSEC);
			dpm[port].pd_button_state = DPM_PD_BUTTON_PRESSED;
		}
		break;
	case DPM_PD_BUTTON_PRESSED:
		if (DPM_CHK_FLAG(port, DPM_FLAG_PD_BUTTON_PRESSED)) {
			pd_timer_enable(port, DPM_TIMER_PD_BUTTON_SHORT_PRESS,
					CONFIG_USB_PD_SHORT_PRESS_MAX_MS *
						MSEC);
			pd_timer_enable(port, DPM_TIMER_PD_BUTTON_LONG_PRESS,
					CONFIG_USB_PD_LONG_PRESS_MAX_MS * MSEC);
		} else if (pd_timer_is_expired(
				   port, DPM_TIMER_PD_BUTTON_LONG_PRESS)) {
			pd_timer_disable(port, DPM_TIMER_PD_BUTTON_SHORT_PRESS);
			pd_timer_disable(port, DPM_TIMER_PD_BUTTON_LONG_PRESS);
			dpm[port].pd_button_state = DPM_PD_BUTTON_IDLE;
		} else if (DPM_CHK_FLAG(port, DPM_FLAG_PD_BUTTON_RELEASED)) {
			if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
				/*
				 * Wake chipset on any button press when the
				 * system is off.
				 */
				chipset_power_on();
			} else if (chipset_in_state(
					   CHIPSET_STATE_ANY_SUSPEND) ||
				   chipset_in_state(CHIPSET_STATE_ON)) {
				if (pd_timer_is_expired(
					    port,
					    DPM_TIMER_PD_BUTTON_SHORT_PRESS)) {
					/*
					 * Shutdown chipset on long USB PD power
					 * button press.
					 */
					chipset_force_shutdown(
						CHIPSET_SHUTDOWN_BUTTON);
				} else {
					/*
					 * Simulate a short power button press
					 * on short USB PD power button press.
					 * This will wake the system from
					 * suspend, or bring up the power UI
					 * when the system is on.
					 */
					power_button_simulate_press(
						USB_PD_SHORT_BUTTON_PRESS_MS);
				}
			}
			pd_timer_disable(port, DPM_TIMER_PD_BUTTON_SHORT_PRESS);
			pd_timer_disable(port, DPM_TIMER_PD_BUTTON_LONG_PRESS);
			dpm[port].pd_button_state = DPM_PD_BUTTON_IDLE;
		}
		break;
	}
#endif /* CONFIG_AP_POWER_CONTROL */

	/* After checking flags, clear them. */
	DPM_CLR_FLAG(port, DPM_FLAG_PD_BUTTON_PRESSED);
	DPM_CLR_FLAG(port, DPM_FLAG_PD_BUTTON_RELEASED);
}

void dpm_run(int port)
{
	if (pd_get_data_role(port) == PD_ROLE_DFP) {
		/* Run DFP related DPM requests */
		if (DPM_CHK_FLAG(port, DPM_FLAG_EXIT_REQUEST))
			dpm_attempt_mode_exit(port);
		else if (!DPM_CHK_FLAG(port, DPM_FLAG_MODE_ENTRY_DONE))
			dpm_attempt_mode_entry(port);

		/* Run USB PD Power button state machine */
		dpm_run_pd_button_sm(port);
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
static uint32_t max_current_claimed;
K_MUTEX_DEFINE(max_current_claimed_lock);

/* Ports with PD sink needing > 1.5 A */
static atomic_t sink_max_pdo_requested;
/* Ports with FRS source needing > 1.5 A */
static atomic_t source_frs_max_requested;
/* Ports with non-PD sinks, so current requirements are unknown */
static atomic_t non_pd_sink_max_requested;

/* BIST shared test mode */
static bool bist_shared_mode_enabled;

#define LOWEST_PORT(p) __builtin_ctz(p) /* Undefined behavior if p == 0 */

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

	/*
	 * Turn off all shared power logic while BIST shared test mode is active
	 * on the system.
	 */
	if (bist_shared_mode_enabled)
		return;

	mutex_lock(&max_current_claimed_lock);

	/* Remove any ports which no longer require 3.0 A */
	removed_ports = max_current_claimed &
			~(sink_max_pdo_requested | source_frs_max_requested |
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
			typec_select_src_current_limit_rp(
				rem_non_pd,
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
			typec_select_src_current_limit_rp(
				rem_non_pd,
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

void dpm_evaluate_request_rdo(int port, uint32_t rdo)
{
	int idx;
	int op_ma;

	if (CONFIG_USB_PD_3A_PORTS == 0)
		return;

	idx = RDO_POS(rdo);
	/* Check for invalid index */
	if (!idx)
		return;

	op_ma = (rdo >> 10) & 0x3FF;
	if ((BIT(port) & sink_max_pdo_requested) && (op_ma <= 150)) {
		/*
		 * sink_max_pdo_requested will be set when we get 5V/3A sink
		 * capability from port partner. If port partner only request
		 * 5V/1.5A, we need to provide 5V/1.5A.
		 */
		atomic_clear_bits(&sink_max_pdo_requested, BIT(port));

		balance_source_ports();
	}
}

void dpm_remove_sink(int port)
{
	if (CONFIG_USB_PD_3A_PORTS == 0)
		return;

	if (!(BIT(port) & (uint32_t)sink_max_pdo_requested) &&
	    !(BIT(port) & (uint32_t)non_pd_sink_max_requested))
		return;

	atomic_clear_bits(&sink_max_pdo_requested, BIT(port));
	atomic_clear_bits(&non_pd_sink_max_requested, BIT(port));

	/* Restore selected default Rp on the port */
	typec_select_src_current_limit_rp(
		port, typec_get_default_current_limit_rp(port));

	balance_source_ports();
}

void dpm_remove_source(int port)
{
	if (CONFIG_USB_PD_3A_PORTS == 0)
		return;

	if (!IS_ENABLED(CONFIG_USB_PD_FRS))
		return;

	if (!(BIT(port) & (uint32_t)source_frs_max_requested))
		return;

	atomic_clear_bits(&source_frs_max_requested, BIT(port));

	balance_source_ports();
}

void dpm_bist_shared_mode_enter(int port)
{
	/*
	 * From 6.4.3.3.1 BIST Shared Test Mode Entry:
	 *
	 * "When any Master Port in a shared capacity group receives a BIST
	 * Message with a BIST Shared Test Mode Entry BIST Data Object, while
	 * in the PE_SRC_Ready State, the UUT Shall enter a compliance test
	 * mode where the maximum source capability is always offered on every
	 * port, regardless of the availability of shared power i.e. all shared
	 * power management is disabled.
	 * . . .
	 * On entering this mode, the UUT Shall send a new Source_Capabilities
	 * Message from each Port in the shared capacity group within
	 * tBISTSharedTestMode. The Tester will not exceed the shared capacity
	 * during this mode."
	 */

	/* Shared mode is unnecessary without at least one 3.0 A port */
	if (CONFIG_USB_PD_3A_PORTS == 0)
		return;

	/* Enter mode only if this port had been in PE_SRC_Ready */
	if (pd_get_power_role(port) != PD_ROLE_SOURCE)
		return;

	bist_shared_mode_enabled = true;

	/* Trigger new source caps on all source ports */
	for (int i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (pd_get_power_role(i) == PD_ROLE_SOURCE)
			typec_select_src_current_limit_rp(i, TYPEC_RP_3A0);
	}
}

void dpm_bist_shared_mode_exit(int port)
{
	/*
	 * From 6.4.3.3.2 BIST Shared Test Mode Exit:
	 *
	 * "Upon receipt of a BIST Message, with a BIST Shared Test Mode Exit
	 * BIST Data Object, the UUT Shall return a GoodCRC Message and Shall
	 * exit the BIST Shared Capacity Test Mode.
	 * . . .
	 * On exiting the mode, the UUT May send a new Source_Capabilities
	 * Message to each port in the shared capacity group or the UUT May
	 * perform ErrorRecovery on each port."
	 */

	/* Shared mode is unnecessary without at least one 3.0 A port */
	if (CONFIG_USB_PD_3A_PORTS == 0)
		return;

	/* Do nothing if Exit was received with no Entry */
	if (!bist_shared_mode_enabled)
		return;

	bist_shared_mode_enabled = false;

	/* Declare error recovery bankruptcy */
	for (int i = 0; i < board_get_usb_pd_port_count(); i++) {
		pd_set_error_recovery(i);
	}
}

/*
 * Note: all ports receive the 1.5 A source offering until they are found to
 * match a criteria on the 3.0 A priority list (ex. through sink capability
 * probing), at which point they will be offered a new 3.0 A source capability.
 *
 * All ports must be offered our full capability while in BIST shared test mode.
 */
__overridable int dpm_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	/* Max PDO may not exist on boards which don't offer 3 A */
#if CONFIG_USB_PD_3A_PORTS > 0
	if (max_current_claimed & BIT(port) || bist_shared_mode_enabled) {
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

	if (max_current_claimed & BIT(port) || bist_shared_mode_enabled)
		return 3000;
	else if (typec_get_default_current_limit_rp(port) == TYPEC_RP_1A5)
		return 1500;
	else
		return 500;
}

__overridable enum pd_sdb_power_indicator
board_get_pd_sdb_power_indicator(enum pd_sdb_power_state power_state)
{
	/*
	 *  LED on for S0 and blinking for S0ix/S3.
	 *  LED off for all other power states (S4, S5, G3, NOT_SUPPORTED)
	 */
	switch (power_state) {
	case PD_SDB_POWER_STATE_S0:
		return PD_SDB_POWER_INDICATOR_ON;
	case PD_SDB_POWER_STATE_MODERN_STANDBY:
	case PD_SDB_POWER_STATE_S3:
		return PD_SDB_POWER_INDICATOR_BLINKING;
	default:
		return PD_SDB_POWER_INDICATOR_OFF;
	}
}

static uint8_t get_status_internal_temp(void)
{
	/*
	 * Internal Temp
	 */
#ifdef CONFIG_TEMP_SENSOR
	int temp_k, temp_c;

	if (temp_sensor_read(CONFIG_USB_PD_TEMP_SENSOR, &temp_k) != EC_SUCCESS)
		return 0;

	/* Check temp is in expected range (<255°C) */
	temp_c = K_TO_C(temp_k);
	if (temp_c > 255)
		return 0;
	else if (temp_c < 2)
		temp_c = 1;

	return (uint8_t)temp_c;
#else
	return 0;
#endif
}

static enum pd_sdb_temperature_status get_status_temp_status(void)
{
	/*
	 * Temperature Status
	 *
	 * OTP events are currently unsupported by the EC, the temperature
	 * status will be reported as "not supported" on temp sensor read
	 * failures and "Normal" otherwise.
	 */
#ifdef CONFIG_TEMP_SENSOR
	int temp_k;

	if (temp_sensor_read(CONFIG_USB_PD_TEMP_SENSOR, &temp_k) != EC_SUCCESS)
		return PD_SDB_TEMPERATURE_STATUS_NOT_SUPPORTED;

	return PD_SDB_TEMPERATURE_STATUS_NORMAL;
#else
	return PD_SDB_TEMPERATURE_STATUS_NOT_SUPPORTED;
#endif
}

static uint8_t get_status_power_state_change(void)
{
	enum pd_sdb_power_state ret = PD_SDB_POWER_STATE_NOT_SUPPORTED;

#ifdef CONFIG_AP_POWER_CONTROL
	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_HARD_OFF)) {
		ret = PD_SDB_POWER_STATE_G3;
	} else if (chipset_in_or_transitioning_to_state(
			   CHIPSET_STATE_SOFT_OFF)) {
		/*
		 * SOFT_OFF includes S4; chipset_state API doesn't support S4
		 * specifically, so fold S4 to S5
		 */
		ret = PD_SDB_POWER_STATE_S5;
	} else if (chipset_in_or_transitioning_to_state(
			   CHIPSET_STATE_SUSPEND)) {
		ret = PD_SDB_POWER_STATE_S3;
	} else if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON)) {
		ret = PD_SDB_POWER_STATE_S0;
	} else if (chipset_in_or_transitioning_to_state(
			   CHIPSET_STATE_STANDBY)) {
		ret = PD_SDB_POWER_STATE_MODERN_STANDBY;
	}
#endif /* CONFIG_AP_POWER_CONTROL */

	return ret | board_get_pd_sdb_power_indicator(ret);
}

int dpm_get_status_msg(int port, uint8_t *msg, uint32_t *len)
{
	struct pd_sdb sdb;
	struct rmdo partner_rmdo;

	/* TODO(b/227236917): Fill in fields of Status message */

	/* Internal Temp */
	sdb.internal_temp = get_status_internal_temp();

	/* Present Input */
	sdb.present_input = 0x0;

	/* Present Battery Input */
	sdb.present_battery_input = 0x0;

	/* Event Flags */
	sdb.event_flags = 0x0;

	/* Temperature Status */
	sdb.temperature_status = get_status_temp_status();

	/* Power Status */
	sdb.power_status = 0x0;

	partner_rmdo = pe_get_partner_rmdo(port);
	if ((partner_rmdo.major_rev == 3 && partner_rmdo.minor_rev >= 1) ||
	    partner_rmdo.major_rev > 3) {
		/* USB PD Rev 3.1: 6.5.2 Status Message */
		sdb.power_state_change = get_status_power_state_change();
		*len = 7;
	} else {
		/* USB PD Rev 3.0: 6.5.2 Status Message */
		sdb.power_state_change = 0;
		*len = 6;
	}

	memcpy(msg, &sdb, *len);
	return EC_SUCCESS;
}

enum ec_status pd_set_bist_share_mode(uint8_t enable)
{
	/*
	 * This command is not allowed if system is locked.
	 */
	if (CONFIG_USB_PD_3A_PORTS == 0 || system_is_locked())
		return EC_RES_ACCESS_DENIED;

	if (enable)
		bist_shared_mode_enabled = true;
	else
		bist_shared_mode_enabled = false;

	return EC_RES_SUCCESS;
}

uint8_t pd_get_bist_share_mode(void)
{
	return bist_shared_mode_enabled;
}

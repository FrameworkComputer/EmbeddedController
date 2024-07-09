/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_USB_COMMON_H
#define __CROS_EC_USB_COMMON_H

/* Functions that are shared between old and new PD stacks */
#include "ec_commands.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#ifdef __cplusplus
extern "C" {
#endif

enum pd_drp_next_states {
	DRP_TC_DEFAULT,
	DRP_TC_UNATTACHED_SNK,
	DRP_TC_ATTACHED_WAIT_SNK,
	DRP_TC_UNATTACHED_SRC,
	DRP_TC_ATTACHED_WAIT_SRC,
	DRP_TC_DRP_AUTO_TOGGLE
};

/**
 * Returns the next state to transition to while in the drp auto toggle state.
 *
 * @param drp_sink_time timer for handling TOGGLE_OFF/FORCE_SINK mode when
 *			auto-toggle enabled. This is an in/out variable.
 * @param power_role current power role
 * @param drp_state dual role states
 * @param cc1 value of CC1 set by tcpm_get_cc
 * @param cc2 value of CC2 set by tcpm_get_cc
 * @param auto_toggle_supported indicates hardware auto toggle support.
 *			Hardware auto toggle support will perform the
 *			unattached to attached debouncing before notifying
 *			us of a connection.
 *
 */
enum pd_drp_next_states drp_auto_toggle_next_state(
	uint64_t *drp_sink_time, enum pd_power_role power_role,
	enum pd_dual_role_states drp_state, enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2, bool auto_toggle_supported);

enum pd_pref_type {
	/* prefer voltage larger than or equal to pd_pref_config.mv */
	PD_PREFER_BUCK,
	/* prefer voltage less than or equal to pd_pref_config.mv */
	PD_PREFER_BOOST,
};

struct pd_pref_config_t {
	/* Preferred PD voltage in mV */
	int mv;
	/* above which percent the battery is in constant voltage stage */
	int cv;
	/* System PLT (minimum consuming) power in mW. */
	int plt_mw;
	/* Preferred PD voltage pick strategy */
	enum pd_pref_type type;
};

/*
 * This function converts an 8 character ascii string with hex digits, without
 * the 0x prefix, into a signed 32-bit number.
 *
 * @param str pointer to hex string to convert
 * @param val pointer to where the integer version is stored
 * @return EC_SUCCSSS on success else EC_ERROR_INVAL on failure
 */
int hex8tou32(char *str, uint32_t *val);

/*
 * When AP requests to suspend PD traffic on the EC so it can do
 * firmware upgrade (retimer firmware, or TCPC chips firmware),
 * it calls this function to check if power is ready for performing
 * the upgrade.
 * @param port USB-C port number
 * @dreturn true  - power is ready
 *          false - power is not ready
 */
bool pd_firmware_upgrade_check_power_readiness(int port);

/* Returns the battery percentage [0-100] of the system. */
int usb_get_battery_soc(void);

/*
 * Returns type C current limit (mA), potentially with the DTS flag, based upon
 * states of the CC lines on the partner side.
 *
 * @param polarity port polarity
 * @param cc1 value of CC1 set by tcpm_get_cc
 * @param cc2 value of CC2 set by tcpm_get_cc
 * @return current limit (mA) with DTS flag set if appropriate
 */
typec_current_t usb_get_typec_current_limit(enum tcpc_cc_polarity polarity,
					    enum tcpc_cc_voltage_status cc1,
					    enum tcpc_cc_voltage_status cc2);

/**
 * Returns the polarity of a Sink.
 *
 * @param cc1 value of CC1 set by tcpm_get_cc
 * @param cc2 value of CC2 set by tcpm_get_cc
 * @return polarity
 */
enum tcpc_cc_polarity get_snk_polarity(enum tcpc_cc_voltage_status cc1,
				       enum tcpc_cc_voltage_status cc2);

/**
 * Returns the polarity of a Source.
 *
 * @param cc1 value of CC1 set by tcpm_get_cc
 * @param cc2 value of CC2 set by tcpm_get_cc
 * @return polarity
 */
enum tcpc_cc_polarity get_src_polarity(enum tcpc_cc_voltage_status cc1,
				       enum tcpc_cc_voltage_status cc2);

/**
 * Find PDO index that offers the most amount of power and stays within
 * max_mv voltage.
 *
 * @param src_cap_cnt
 * @param src_caps
 * @param max_mv maximum voltage (or -1 if no limit)
 * @param pdo raw pdo corresponding to index, or index 0 on error (output)
 * @return index of PDO within source cap packet
 */
int pd_find_pdo_index(uint32_t src_cap_cnt, const uint32_t *const src_caps,
		      int max_mv, uint32_t *selected_pdo);

/**
 * Extract power information out of a Power Data Object (PDO)
 *
 * @param pdo raw pdo to extract
 * @param ma current of the PDO (output)
 * @param max_mv maximum voltage of the PDO (output)
 * @param min_mv minimum voltage of the PDO (output)
 */
void pd_extract_pdo_power(uint32_t pdo, uint32_t *ma, uint32_t *max_mv,
			  uint32_t *min_mv);

/**
 * Decide which PDO to choose from the source capabilities.
 *
 * @param vpd_vdo VPD VDO
 * @param rdo  requested Request Data Object.
 * @param ma  selected current limit (stored on success)
 * @param mv  selected supply voltage (stored on success)
 * @param port USB-C port number
 */
void pd_build_request(int32_t vpd_vdo, uint32_t *rdo, uint32_t *ma,
		      uint32_t *mv, int port);

/**
 * Notifies a task that is waiting on a system jump, that it's complete.
 */
void notify_sysjump_ready(void);

/**
 * Set USB MUX with current data role
 *
 * @param port USB-C port number
 */
void set_usb_mux_with_current_data_role(int port);

/**
 * Check if the mux should be set to enable USB3.1 mode based only on being in a
 * UFP data role. This is mode is required when attached to a port partner that
 * is type-c only, but still needs to enable USB3.1 mode.
 *
 * @param port USB-C port number
 * @return true if USB3 mode should be enabled, false otherwise
 */
__override_proto bool usb_ufp_check_usb3_enable(int port);

/**
 * Configure the USB MUX in safe mode.
 * Before entering into alternate mode, state of the USB-C MUX needs to be in
 * safe mode.
 * Ref: USB Type-C Cable and Connector Specification
 * Section E.2.2 Alternate Mode Electrical Requirements
 *
 * @param port The PD port number
 */
void usb_mux_set_safe_mode(int port);

/**
 * Configure the USB MUX in safe mode while exiting an alternate mode.
 * Although the TCSS (virtual mux) has a distinct safe mode state, it
 * needs to be in a disconnected state to properly exit an alternate
 * mode. Therefore, do not treat the virtual mux as a special case, as
 * usb_mux_set_safe_mode does.
 *
 * @param port The PD port number
 */
void usb_mux_set_safe_mode_exit(int port);

/**
 * Get the PD flags stored in BB Ram
 *
 * @param port USB-C port number
 * @param flags pointer where flags are written to
 * @return EC_SUCCESS on success
 */
int pd_get_saved_port_flags(int port, uint8_t *flags);

/**
 * Update the flag in BB Ram with the give value
 *
 * @param port USB-C port number
 * @param flag BB Ram flag to update
 * @param do_set value written to the BB Ram flag
 */
void pd_update_saved_port_flags(int port, uint8_t flag, uint8_t do_set);

/**
 * Sets up and sends PD alert message with given ADO on all ports.
 *
 * @param ado - Alert Data Object defining alert sent to the PD partner
 * @return EC_SUCCESS on success else EC_ERROR_INVAL
 */
int pd_broadcast_alert_msg(uint32_t ado);

/**
 * Sets up and sends PD alert message with given ADO on one port.
 *
 * @param port - the port to send the alert message on
 * @param ado - Alert Data Object defining alert sent to the PD partner
 * @return EC_SUCCESS on success else EC_ERROR_INVAL
 */
int pd_send_alert_msg(int port, uint32_t ado);

/**
 * Sets up a hard reset to send on the port
 *
 * @param port - the port to send the hard reset on
 */
void pd_send_hard_reset(int port);

/**
 * During USB retimer firmware update, process operation
 * requested by AP
 *
 * @param port USB-C port number
 * @param op
 *       0 - USB_RETIMER_FW_UPDATE_QUERY_PORT
 *       1 - USB_RETIMER_FW_UPDATE_SUSPEND_PD
 *       2 - USB_RETIMER_FW_UPDATE_RESUME_PD
 *       3 - USB_RETIMER_FW_UPDATE_GET_MUX
 *       4 - USB_RETIMER_FW_UPDATE_SET_USB
 *       5 - USB_RETIMER_FW_UPDATE_SET_SAFE
 *       6 - USB_RETIMER_FW_UPDATE_SET_TBT
 *       7 - USB_RETIMER_FW_UPDATE_DISCONNECT
 */
void usb_retimer_fw_update_process_op(int port, int op);

/**
 * Get result of last USB retimer firmware update operation requested
 * by AP. Result is passed to AP via EC_CMD_ACPI_READ.
 *
 * @return Result of last operation. It's
 *         which port has retimer if last operation is
 *         USB_RETIMER_FW_UPDATE_QUERY_PORT;
 *         PD task is enabled or not if last operations are
 *         USB_RETIMER_FW_UPDATE_SUSPEND_PD or
 *         USB_RETIMER_FW_UPDATE_QUERY_PORT;
 *         current mux if last operations are
 *         USB_RETIMER_FW_UPDATE_GET_MUX, USB_RETIMER_FW_UPDATE_SET_USB,
 *         USB_RETIMER_FW_UPDATE_SET_SAFE, USB_RETIMER_FW_UPDATE_SET_TBT,
 *         or USB_RETIMER_FW_UPDATE_DISCONNECT.
 */
int usb_retimer_fw_update_get_result(void);

/**
 * Process deferred retimer firmware update operations.
 *
 * @param port USB-C port number
 */
void usb_retimer_fw_update_process_op_cb(int port);

/**
 * Dump SourceCap information.
 *
 * @param port USB-C port number
 */
void pd_srccaps_dump(int port);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_COMMON_H */

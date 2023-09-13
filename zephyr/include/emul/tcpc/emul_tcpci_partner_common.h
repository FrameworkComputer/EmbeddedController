/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Common code used by TCPCI partner device emulators
 */

#ifndef __EMUL_TCPCI_PARTNER_COMMON_H
#define __EMUL_TCPCI_PARTNER_COMMON_H

#include "ec_commands.h"
#include "emul/tcpc/emul_tcpci.h"
#include "usb_pd.h"

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

/**
 * @brief Common code used by TCPCI partner device emulators
 * @defgroup tcpci_partner Common code for TCPCI partner device emulators
 * @{
 *
 * Common code for TCPCI partner device emulators allows to send SOP messages
 * in generic way using optional delay.
 */

/** Timeout for other side to respond to PD message */
#define TCPCI_PARTNER_RESPONSE_TIMEOUT_MS 30
#define TCPCI_PARTNER_RESPONSE_TIMEOUT K_MSEC(TCPCI_PARTNER_RESPONSE_TIMEOUT_MS)
/** Timeout for source to transition to requested state after accept */
#define TCPCI_PARTNER_TRANSITION_TIMEOUT_MS 550
#define TCPCI_PARTNER_TRANSITION_TIMEOUT \
	K_MSEC(TCPCI_PARTNER_TRANSITION_TIMEOUT_MS)
/** Timeout for source to send capability again after failure */
#define TCPCI_SOURCE_CAPABILITY_TIMEOUT_MS 150
#define TCPCI_SOURCE_CAPABILITY_TIMEOUT \
	K_MSEC(TCPCI_SOURCE_CAPABILITY_TIMEOUT_MS)
/** Timeout for source to send capability message after power swap */
#define TCPCI_SWAP_SOURCE_START_TIMEOUT_MS 20
#define TCPCI_SWAP_SOURCE_START_TIMEOUT \
	K_MSEC(TCPCI_SWAP_SOURCE_START_TIMEOUT_MS)

/** Common data for TCPCI partner device emulators */
struct tcpci_partner_data {
	/** List of extensions used in TCPCI partner emulator */
	struct tcpci_partner_extension *extensions;
	/** Operations used by TCPCI emulator */
	struct tcpci_emul_partner_ops ops;
	/** Timer used to send message with delay */
	struct k_timer delayed_send;
	/** Reserved for fifo, used for scheduling messages */
	void *fifo_data;
	/** Pointer to connected TCPCI emulator */
	const struct emul *tcpci_emul;
	/** Queue for delayed messages */
	sys_slist_t to_send;
	/** Mutex for to_send queue */
	struct k_mutex to_send_mutex;
	/** Next SOP message id */
	int sop_msg_id;
	/** Next SOP' message id */
	int sop_prime_msg_id;
	/** Last received message id */
	int sop_recv_msg_id;
	/** Last received SOP' message id */
	int sop_prime_recv_msg_id;
	/** Power role (used in message header) */
	enum pd_power_role power_role;
	/** Data role (used in message header) */
	enum pd_data_role data_role;
	/** Whether this partner will Accept a Data Role Swap that would switch
	 * the partner from DFP to UFP.
	 */
	bool drs_to_ufp_supported;
	/** Whether this partner will Accept a Data Role Swap that would switch
	 * the partner from UFP to DFP.
	 */
	bool drs_to_dfp_supported;
	/** VCONN role */
	enum pd_vconn_role vconn_role;
	/** Revision (used in message header) */
	enum pd_rev_type rev;
	/** The response message that will be sent in response to VCONN Swap. */
	enum pd_ctrl_msg_type vcs_response;
	/** Resistor set at the CC1 line of partner emulator */
	enum tcpc_cc_voltage_status cc1;
	/** Resistor set at the CC2 line of partner emulator */
	enum tcpc_cc_voltage_status cc2;
	/**
	 * Polarity of the partner emulator. It controls to which CC line of
	 * TCPC emulator the partner emulator CC1 line should be connected.
	 */
	enum tcpc_cc_polarity polarity;
	/**
	 * Mask for control message types that shouldn't be handled
	 * in common message handler
	 */
	uint32_t common_handler_masked;
	/**
	 * True if accept and reject messages shouldn't trigger soft reset
	 * in common message handler
	 */
	bool wait_for_response;
	/**
	 * If emulator triggers soft reset, it waits for accept. If accept
	 * doesn't arrive, hard reset is triggered.
	 */
	bool in_soft_reset;
	/** Current AMS Control request being handled */
	enum pd_ctrl_msg_type cur_ams_ctrl_req;
	/**
	 * If common code should send GoodCRC for each message. If false,
	 * then one of extensions should call tcpci_emul_partner_msg_status().
	 * If message is handled by common code, than GoodCRC is send regardless
	 * of send_goodcrc value.
	 */
	bool send_goodcrc;
	/**
	 * Mutex for TCPCI transmit handler. Should be used to synchronise
	 * access to partner emulator with TCPCI emulator.
	 */
	struct k_mutex transmit_mutex;
	/** Delayed work which is executed when response timeout occurs */
	struct k_work_delayable sender_response_timeout;
	/** Number of TCPM timeouts. Test may chekck if timeout occurs */
	int tcpm_timeouts;
	/** List with logged PD messages */
	sys_slist_t msg_log;
	/** Flag which controls if messages should be logged */
	bool collect_msg_log;
	/** Mutex for msg_log */
	struct k_mutex msg_log_mutex;
	/**
	 * Pointer to last received message status. This pointer is set only
	 * when message logging is enabled. It used to track if partner set
	 * any status to received message.
	 */
	enum tcpci_emul_tx_status *received_msg_status;
	/** Whether port partner is configured in DisplayPort mode */
	bool displayport_configured;
	/** The number of Enter Mode REQs received since connection
	 *  or the last Hard Reset, whichever was more recent.
	 */
	atomic_t mode_enter_attempts;
	/* SVID of entered mode (0 if no mode is entered) */
	uint16_t entered_svid;

	enum tcpc_cc_voltage_status tcpm_cc1;
	enum tcpc_cc_voltage_status tcpm_cc2;

	/* VDMs with which the partner responds to discovery REQs. The VDM
	 * buffers include the VDM header, and the VDO counts include 1 for the
	 * VDM header. This structure has space for the mode response for a
	 * single supported SVID.
	 */
	uint32_t identity_vdm[VDO_MAX_SIZE];
	int identity_vdos;
	/* Discover SVIDs ACK VDM */
	uint32_t svids_vdm[VDO_MAX_SIZE];
	int svids_vdos;
	/* Discover Modes ACK VDM (implicitly for the first SVID) */
	uint32_t modes_vdm[VDO_MAX_SIZE];
	int modes_vdos;
	/* VDMs sent when responding to a mode entry command */
	uint32_t enter_mode_vdm[VDO_MAX_SIZE];
	int enter_mode_vdos;
	/* VDMs sent when responding to DisplayPort status update command */
	uint32_t dp_status_vdm[VDO_MAX_SIZE];
	int dp_status_vdos;
	/* VDMs sent when responding to DisplayPort config command */
	uint32_t dp_config_vdm[VDO_MAX_SIZE];
	int dp_config_vdos;
	struct {
		/* Index of the last battery we requested capabilities for. The
		 * BCDB response does not include the index so we need to track
		 * it manually. -1 indicates no outstanding request.
		 */
		int index;
		/* Stores Battery Capability Data Blocks (BCDBs) requested and
		 * received from the TCPM for later analysis. See USB-PD spec
		 * Rev 3.1, Ver 1.3 section 6.5.5
		 */
		struct pd_bcdb bcdb[PD_BATT_MAX];
		/* Stores a boolean status for each battery index indicating
		 * whether we have received a BCDB response for that battery.
		 */
		bool have_response[PD_BATT_MAX];
	} battery_capabilities;
	/* RMDO returned by partner in response to a Get_Revision message */
	uint32_t rmdo;
	/* Used to control accept/reject for partner port of Enter_USB msg */
	bool enter_usb_accept;

	/*
	 * Cable which is "plugged in" to this port partner
	 * Note: Much as in real life, cable should be attached before the port
	 * partner can be plugged in to properly discover its information.
	 * For tests, this means this poitner should be set before connecting
	 * the source or sink partner.
	 */
	struct tcpci_cable_data *cable;
};

struct tcpci_cable_data {
	/*
	 * Identity VDM ACKs which the cable is expected to send
	 * These include the VDM header
	 */
	uint32_t identity_vdm[VDO_MAX_SIZE];
	int identity_vdos;
	/* Discover SVIDs ACK VDM */
	uint32_t svids_vdm[VDO_MAX_SIZE];
	int svids_vdos;
	/* Discover Modes ACK VDM (implicitly for the first SVID) */
	uint32_t modes_vdm[VDO_MAX_SIZE];
	int modes_vdos;
};

/** Structure of message used by TCPCI partner emulator */
struct tcpci_partner_msg {
	/** Reserved for sys_slist_* usage */
	sys_snode_t node;
	/** TCPCI emulator message */
	struct tcpci_emul_msg msg;
	/** Time when message should be sent if message is delayed */
	uint64_t time;
	/** Message type that is placed in the Message Header. Its meaning
	 *  depends on the class of message:
	 *   - for Control Messages, see `enum pd_ctrl_msg_type`
	 *   - for Data Messages, see `enum pd_data_msg_type`
	 *   - for Extended Messages, see `enum pd_ext_msg_type`
	 */
	int type;
	/** Number of data objects */
	int data_objects;
	/** True if this is an extended message */
	bool extended;
};

/** Identify sender of logged PD message */
enum tcpci_partner_msg_sender {
	TCPCI_PARTNER_SENDER_PARTNER,
	TCPCI_PARTNER_SENDER_TCPM
};

/** Structure of logged PD message */
struct tcpci_partner_log_msg {
	/** Reserved for sys_slist_* usage */
	sys_snode_t node;
	/** Pointer to buffer for header and message */
	uint8_t *buf;
	/** Number of bytes in buf */
	int cnt;
	/** Type of message (SOP, SOP', etc) */
	uint8_t sop;
	/** Time when message was send or received by partner emulator */
	uint64_t time;
	/** Sender of the message */
	enum tcpci_partner_msg_sender sender;
	/** Status of sending this message */
	enum tcpci_emul_tx_status status;
};

/** Result of common handler */
enum tcpci_partner_handler_res {
	TCPCI_PARTNER_COMMON_MSG_HANDLED,
	TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED,
	TCPCI_PARTNER_COMMON_MSG_HARD_RESET,
	TCPCI_PARTNER_COMMON_MSG_NO_GOODCRC,
};

/** Structure of TCPCI partner extension */
struct tcpci_partner_extension {
	/** Pointer to next extension or NULL */
	struct tcpci_partner_extension *next;
	/** Pointer to callbacks of the extension */
	struct tcpci_partner_extension_ops *ops;
};

/**
 * Extension callbacks. They are called after the common partner emulator code
 * starting from the extension pointed by extensions field in
 * struct tcpci_partner_data. Rest of extensions are called in order established
 * by next field in struct tcpci_partner_extension.
 * If not required, each callback can be NULL. The NULL callback is ignored and
 * next extension in chain is called.
 * It may be useful for extension to mask message handling in common code using
 * @ref tcpci_partner_common_handler_mask_msg to alter emulator behavior in case
 * of receiving some messages.
 */
struct tcpci_partner_extension_ops {
	/**
	 * @brief Function called when message from TCPM is handled
	 *
	 * @param ext Pointer to partner extension
	 * @param common_data Pointer to TCPCI partner emulator
	 * @param msg Pointer to received message
	 *
	 * @return TCPCI_PARTNER_COMMON_MSG_HANDLED to indicate that message
	 *         is handled and ignore other extensions sop_msg_handler
	 * @return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED to indicate that
	 *         message wasn't handled
	 */
	enum tcpci_partner_handler_res (*sop_msg_handler)(
		struct tcpci_partner_extension *ext,
		struct tcpci_partner_data *common_data,
		const struct tcpci_emul_msg *msg);

	/**
	 * @brief Function called when HardReset message is received or sent
	 *
	 * @param ext Pointer to partner extension
	 * @param common_data Pointer to TCPCI partner emulator
	 */
	void (*hard_reset)(struct tcpci_partner_extension *ext,
			   struct tcpci_partner_data *common_data);

	/**
	 * @brief Function called when SoftReset message is received
	 *
	 * @param ext Pointer to partner extension
	 * @param common_data Pointer to TCPCI partner emulator
	 */
	void (*soft_reset)(struct tcpci_partner_extension *ext,
			   struct tcpci_partner_data *common_data);

	void (*control_change)(struct tcpci_partner_extension *ext,
			       struct tcpci_partner_data *common_data);

	/**
	 * @brief Function called when partner emulator is disconnected from
	 *        TCPM
	 *
	 * @param ext Pointer to partner extension
	 * @param common_data Pointer to TCPCI partner emulator
	 */
	void (*disconnect)(struct tcpci_partner_extension *ext,
			   struct tcpci_partner_data *common_data);

	/**
	 * @brief Function called when partner emulator is connected to TCPM.
	 *        In connect callback, any message cannot be sent with 0 delay
	 *
	 * @param ext Pointer to partner extension
	 * @param common_data Pointer to TCPCI partner emulator
	 *
	 * @return Negative value on error
	 * @return 0 on success
	 */
	int (*connect)(struct tcpci_partner_extension *ext,
		       struct tcpci_partner_data *common_data);
};

/**
 * @brief Initialise common TCPCI partner emulator. Need to be called before
 *        any other tcpci_partner_* function and init functions of extensions.
 *
 * @param data Pointer to USB-C charger emulator
 * @param rev PD revision of the emulator
 */
void tcpci_partner_init(struct tcpci_partner_data *data, enum pd_rev_type rev);

/**
 * @brief Set the partner emulator to support or not support swapping data roles
 * to UFP and DFP. If the partner supports a swap, it should respond to DR_Swap
 * with Accept with that role as the new data role.
 *
 * @param data Pointer to USB-C partner emulator
 * @param drs_to_ufp_supported Whether the partner supports swapping to UFP
 * @param drs_to_dfp_supported Whether the partner supports swapping to DFP
 */
void tcpci_partner_set_drs_support(struct tcpci_partner_data *data,
				   bool drs_to_ufp_supported,
				   bool drs_to_dfp_supported);

/**
 * @brief Set the response message for VCONN Swap.
 *
 * A compliant partner should not change the response from Accept/Reject to
 * Not_Supported or vice versa while attached. However, there are real devices
 * that pretend to stop supporting VCONN after completing a VCONN Swap.
 *
 * The default behavior of the partner is to Accept VCONN Swaps.
 *
 * @param data Pointer to USB-C partner emulator
 * @param vcs_response PD_CTRL_ACCEPT to support sourcing VCONN,
 *        PD_CTRL_NOT_SUPPORTED to not support it, or other message types
 *        as needed.
 */
void tcpci_partner_set_vcs_response(struct tcpci_partner_data *data,
				    enum pd_ctrl_msg_type vcs_response);
/**
 * @brief Free message's memory
 *
 * @param msg Pointer to message
 */
void tcpci_partner_free_msg(struct tcpci_partner_msg *msg);

/**
 * @brief Set header of the message
 *
 * @param data Pointer to TCPCI partner emulator
 * @param msg Pointer to message
 */
void tcpci_partner_set_header(struct tcpci_partner_data *data,
			      struct tcpci_partner_msg *msg);

/**
 * @brief Send message to TCPCI emulator or schedule message. On error message
 *        is freed.
 *
 * @param data Pointer to TCPCI partner emulator
 * @param msg Pointer to message to send
 * @param delay Optional delay
 *
 * @return TCPCI_EMUL_TX_SUCCESS on success
 * @return TCPCI_EMUL_TX_FAILED when TCPCI is configured to not handle
 *                              messages of this type
 * @return negative on failure
 */
int tcpci_partner_send_msg(struct tcpci_partner_data *data,
			   struct tcpci_partner_msg *msg, uint64_t delay);

/**
 * @brief Send control message with optional delay
 *
 * @param data Pointer to TCPCI partner emulator
 * @param type Type of message
 * @param delay Optional delay
 *
 * @return TCPCI_EMUL_TX_SUCCESS on success
 * @return TCPCI_EMUL_TX_FAILED when TCPCI is configured to not handle
 *                              messages of this type
 * @return -ENOMEM when there is no free memory for message
 * @return negative on failure
 */
int tcpci_partner_send_control_msg(struct tcpci_partner_data *data,
				   enum pd_ctrl_msg_type type, uint64_t delay);

/**
 * @brief Send data message with optional delay. Data objects are copied to
 *        message.
 *
 * @param data Pointer to TCPCI partner emulator
 * @param type Type of message
 * @param data_obj Pointer to array of data objects
 * @param data_obj_num Number of data objects
 * @param delay Optional delay in milliseconds
 *
 * @return TCPCI_EMUL_TX_SUCCESS on success
 * @return TCPCI_EMUL_TX_FAILED when TCPCI is configured to not handle
 *                              messages of this type
 * @return -ENOMEM when there is no free memory for message
 * @return negative on failure
 */
int tcpci_partner_send_data_msg(struct tcpci_partner_data *data,
				enum pd_data_msg_type type, uint32_t *data_obj,
				int data_obj_num, uint64_t delay);

/**
 * @brief Send an extended PD message to the port partner
 *
 * @param data Pointer to TCPCI partner emulator
 * @param type Extended message type
 * @param delay Message send delay in milliseconds, or zero for no delay.
 * @param payload Pointer to data payload. Does not include any headers.
 * @param payload_size Number of bytes in above payload
 * @return negative on failure, 0 on success
 */
int tcpci_partner_send_extended_msg(struct tcpci_partner_data *data,
				    enum pd_ext_msg_type type, uint64_t delay,
				    uint8_t *payload, size_t payload_size);

/**
 * @brief Remove all messages that are in delayed message queue
 *
 * @param data Pointer to TCPCI partner emulator
 *
 * @return 0 on success
 * @return negative on failure
 */
int tcpci_partner_clear_msg_queue(struct tcpci_partner_data *data);

/**
 * @brief Send hard reset and set common data to state after hard reset (reset
 *        counters, flags, clear message queue)
 *
 * @param data Pointer to TCPCI partner emulator
 */
void tcpci_partner_common_send_hard_reset(struct tcpci_partner_data *data);

/**
 * @brief Send hard reset and set common data to state after soft reset (reset
 *        counters, set flags to wait for accept)
 *
 * @param data Pointer to TCPCI partner emulator
 */
void tcpci_partner_common_send_soft_reset(struct tcpci_partner_data *data);

/**
 * @brief Send a Get Battery Capabilities request to the TCPM
 *
 * @param data Pointer to TCPCI partner emulator
 * @param battery_index Request capability info on this battery. Must
 *        be (0 <= battery_index < PD_BATT_MAX)
 */
void tcpci_partner_common_send_get_battery_capabilities(
	struct tcpci_partner_data *data, int battery_index);

/**
 * @brief Resets the data structure used for tracking battery capability
 *        requests and responses.
 *
 * @param data Emulator state
 */
void tcpci_partner_reset_battery_capability_state(
	struct tcpci_partner_data *data);

/**
 * @brief Start sender response timer for TCPCI_PARTNER_RESPONSE_TIMEOUT_MS.
 *        If @ref tcpci_partner_stop_sender_response_timer wasn't called before
 *        timeout, @ref tcpci_partner_sender_response_timeout is called.
 *        The wait_for_response flag is set on timer start.
 *
 * @param data Pointer to TCPCI partner emulator
 */
void tcpci_partner_start_sender_response_timer(struct tcpci_partner_data *data);

/**
 * @brief Stop sender response timer. The wait_for_response flag is unset.
 *        Timeout handler will not execute.
 *
 * @param data Pointer to TCPCI partner emulator
 */
void tcpci_partner_stop_sender_response_timer(struct tcpci_partner_data *data);

/**
 * @brief Select if @ref tcpci_partner_common_msg_handler should handle specific
 *        control message type.
 *
 * @param data Pointer to TCPCI partner emulator
 * @param type Control message to mask/unmask
 * @param enable If true message of that type is handled, if false common
 *               handler doesn't handle message of that type
 */
void tcpci_partner_common_handler_mask_msg(struct tcpci_partner_data *data,
					   enum pd_ctrl_msg_type type,
					   bool enable);

/**
 * @brief Common disconnect function which clears messages queue, sets
 *        tcpci_emul field in struct tcpci_partner_data to NULL, and stops
 *        timers.
 *
 * @param data Pointer to TCPCI partner emulator
 */
void tcpci_partner_common_disconnect(struct tcpci_partner_data *data);

/**
 * @brief Select if PD messages should be logged or not.
 *
 * @param data Pointer to TCPCI partner emulator
 * @param enable If true, PD messages are logged, false otherwise
 *
 * @return 0 on success
 * @return non-zero on failure
 */
int tcpci_partner_common_enable_pd_logging(struct tcpci_partner_data *data,
					   bool enable);

/**
 * @brief Print all logged PD messages
 *
 * @param data Pointer to TCPCI partner emulator
 */
void tcpci_partner_common_print_logged_msgs(struct tcpci_partner_data *data);

/**
 * @brief Clear all logged PD messages
 *
 * @param data Pointer to TCPCI partner emulator
 */
void tcpci_partner_common_clear_logged_msgs(struct tcpci_partner_data *data);

/**
 * @brief Set the discovery responses (Vendor Defined Messages) for the partner.
 *
 * If a response for a command type is not defined, the partner will ignore
 * requests of that type. To emulate compliant behavior, the discover responses
 * should be internally consistent, e.g., if there is a DisplayPort VID in
 * Discover SVIDs ACK, there should be a Discover Modes ACK for DisplayPort.
 *
 * @param data          Pointer to TCPCI partner emulator
 * @param identity_vdos Number of 32-bit Vendor Defined Objects in the Discover
 *                      Identity response VDM
 * @param identity_vdm  Pointer to the Discover Identity response VDM, including
 *                      the VDM header
 * @param svids_vdos    Number of VDOs in the Discover SVIDs response
 * @param svids_vdm     Pointer to the Discover SVIDs response
 * @param modes_vdos    Number of VDOs in the Discover Modes response
 * @param modes_vdm     Pointer to the Discover Modes response; only currently
 *                      supports a response for a single SVID
 */
void tcpci_partner_set_discovery_info(struct tcpci_partner_data *data,
				      int identity_vdos, uint32_t *identity_vdm,
				      int svids_vdos, uint32_t *svids_vdm,
				      int modes_vdos, uint32_t *modes_vdm);

/**
 * @brief Sets cur_ams_ctrl_req to msg_type to track current request
 *
 * @param data          Pointer to TCPCI partner data
 * @param msg_type      enum pd_ctrl_msg_type
 */
void tcpci_partner_common_set_ams_ctrl_msg(struct tcpci_partner_data *data,
					   enum pd_ctrl_msg_type msg_type);

/**
 * @brief Sets cur_ams_ctrl_req to INVALID
 *
 * @param data          Pointer to TCPCI partner data
 */
void tcpci_partner_common_clear_ams_ctrl_msg(struct tcpci_partner_data *data);

/**
 * @brief Called by partner emulators internally. Resets the common tcpci
 * partner data with the provided role.
 *
 * @param data          Pointer to TCPCI partner data
 * @param power_role    USB PD power role
 */
void tcpci_partner_common_hard_reset_as_role(struct tcpci_partner_data *data,
					     enum pd_power_role power_role);

/**
 * @brief Connect emulated device to TCPCI. The connect callback is executed on
 *        all extensions.
 *
 * @param data Pointer to TCPCI partner emulator
 * @param tcpci_emul Pointer to TCPCI emulator to connect
 *
 * @return 0 on success
 * @return negative on TCPCI connect error
 */
int tcpci_partner_connect_to_tcpci(struct tcpci_partner_data *data,
				   const struct emul *tcpci_emul);

/**
 * @brief Inform TCPCI about status of received message (TCPCI_EMUL_TX_SUCCESS
 *        GoodCRC send to TCPCI, TCPCI_EMUL_TX_DISCARDED partner message send in
 *        the same time as TCPCI message, TCPCI_EMUL_TX_FAILED GoodCRC doesn't
 *        send to TCPCI)
 *
 * @param data Pointer to TCPCI partner emulator
 * @param status Status of received message
 */
void tcpci_partner_received_msg_status(struct tcpci_partner_data *data,
				       enum tcpci_emul_tx_status status);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_COMMON_H */

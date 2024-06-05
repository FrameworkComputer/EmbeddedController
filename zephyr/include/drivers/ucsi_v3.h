/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief UCSI data structures and types used for USB-C PDC drivers
 *
 * The information in this file was taken from the UCSI
 * Revision 3.0
 */

/**
 * @file drivers/ucsi_v3.h
 * @brief UCSI Data Structures and Types.
 *
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_UCSI_V3_H_
#define ZEPHYR_INCLUDE_DRIVERS_UCSI_V3_H_

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UCSI version in BCD
 */
#define UCSI_VERSION 0x0300

/**
 * @brief Mamimun number of data bytes the PDC can transfer or receive
 *        at a time
 */
#define PDC_MAX_DATA_LENGTH 256

/**
 * @brief UCSI Commands
 */
enum ucsi_command_t {
	/** UCSI COMMAND 0x00 RESERVED */
	/** UCSI PPM RESET */
	UCSI_PPM_RESET = 0x01,
	/** UCSI CANCEL */
	UCSI_CANCEL = 0x02,
	/** UCSI CONNECTOR RESET */
	UCSI_CONNECTOR_RESET = 0x03,
	/** UCSI ACK CC CI */
	UCSI_ACK_CC_CI = 0x04,
	/** UCSI SET NOTIFICATION ENABLE */
	UCSI_SET_NOTIFICATION_ENABLE = 0x05,
	/** UCSI CAPABILITY */
	UCSI_GET_CAPABILITY = 0x06,
	/** UCSI CONNECTOR CAPABILITY */
	UCSI_GET_CONNECTOR_CAPABILITY = 0x07,
	/** UCSI SET CCOM */
	UCSI_SET_CCOM = 0x08,
	/** UCSI SET UOR */
	UCSI_SET_UOR = 0x09,
	/** UCSI SET PDM 0x0a OBSOLETE*/
	/** UCSI SET PDR */
	UCSI_SET_PDR = 0x0b,
	/** UCSI ALTERNATE MODES */
	UCSI_GET_ALTERNATE_MODES = 0x0c,
	/** UCSI GET CAM SUPPORTED */
	UCSI_GET_CAM_SUPPORTED = 0x0d,
	/** UCSI GET CURRENT CAM */
	UCSI_GET_CURRENT_CAM = 0x0e,
	/** UCSI NEW CAM */
	UCSI_SET_NEW_CAM = 0x0f,
	/** UCSI GET PDOS */
	UCSI_GET_PDOS = 0x10,
	/** UCSI GET CABLE PROPERTY */
	UCSI_GET_CABLE_PROPERTY = 0x11,
	/** UCSI GET CONNECTOR STATUS */
	UCSI_GET_CONNECTOR_STATUS = 0x12,
	/** UCSI GET ERROR STATUS */
	UCSI_GET_ERROR_STATUS = 0x13,
	/** UCSI SET POWER LEVEL */
	UCSI_SET_POWER_LEVEL = 0x14,
	/** UCSI GET PD MESSAGE */
	UCSI_GET_PD_MESSAGE = 0x15,
	/** UCSI GET ATTENTION VDO */
	UCSI_GET_ATTENTION_VDO = 0x16,
	/** UCSI COMMAND 0x17 RESERVED */
	/** UCSI GET CAM CS */
	UCSI_GET_CAM_CS = 0x18,
	/** UCSI LPM FW UPDATE REQUEST */
	UCSI_LPM_FW_UPDATE_REQUEST = 0x19,
	/** UCSI SECURITY REQUEST */
	UCSI_SECURITY_REQUEST = 0x1a,
	/** UCSI SET RETIMER MODE */
	UCSI_SET_RETIMER_MODE = 0x1b,
	/** UCSI SET SINK PATH */
	UCSI_SET_SINK_PATH = 0x1c,
	/** UCSI SET PDOS */
	UCSI_SET_PDOS = 0x1d,
	/** UCSI READ POWER LEVEL */
	UCSI_READ_POWER_LEVEL = 0x1e,
	/** UCSI CHUNKING SUPPORTED */
	UCSI_CHUNKING_SUPPORT = 0x1f,
	/** UCSI VENDOR DEFINED COMMAND */
	UCSI_VENDOR_DEFINED_COMMAND = 0x20,
	/** UCSI SET USB */
	UCSI_SET_USB = 0x21,
	/** UCSI GET LPM PPM INFO */
	UCSI_GET_LPM_PPM_INFO = 0x22,
};

/**
 * @brief Returns a pointer to the string name of a UCSI Command
 * @note The get_ucsi_command_name function must be updated when the enum
 * ucsi_command_t is updated
 *
 * @param cmd UCSI command
 * @ret pointer to string name of the UCSI command
 */
const char *const get_ucsi_command_name(enum ucsi_command_t cmd);

/**
 * @brief PDO Offset to start reading PDOs
 */
enum pdo_offset_t {
	/** PDO Offset 0 */
	PDO_OFFSET_0,
	/** PDO Offset 1 */
	PDO_OFFSET_1,
	/** PDO Offset 2 */
	PDO_OFFSET_2,
	/** PDO Offset 3 */
	PDO_OFFSET_3,
	/** PDO Offset 4 */
	PDO_OFFSET_4,
	/** PDO Offset 5 */
	PDO_OFFSET_5,
	/** PDO Offset 6 */
	PDO_OFFSET_6,
	/** PDO Offset 7 */
	PDO_OFFSET_7,
	/** Enum end marker */
	PDO_OFFSET_MAX,
};

/**
 * @brief USB Type-C Current
 */
enum usb_typec_current_t {
	/** PPM defined default */
	TC_CURRENT_PPM_DEFINED = 0,
	/** Rp set to 3.0A */
	TC_CURRENT_3_0A = 1,
	/** Rp set to 1.5A */
	TC_CURRENT_1_5A = 2,
	/** Rp set to USB default */
	TC_CURRENT_USB_DEFAULT = 3
};

/**
 * @brief Type of source caps
 */
enum source_caps_t {
	/**
	 * The Provider Capabilities that the Source currently supports.
	 * These could change dynamically and could be lower than the
	 * Maximum Source Capabilities if the system is Reaching Power
	 * Budget Limit due to multiple connected Sinks or if the Power
	 * Budget has been lowered due to it being unplugged from external
	 * power supply.
	 */
	CURRENT_SUPPORTED_SOURCE_CAPS = 0,
	/**
	 * The Provider Capabilities that are advertised by the Source
	 * during PD contract negotiation. These could be lower due to
	 * the Cable’s current carrying capabilities. This is only valid
	 * when a port partner is present.
	 */
	ADVERTISED_CAPS = 1,
	/**
	 * The Maximum Provider Capabilities that the Source can support.
	 * These wouldn’t change for a connector.
	 */
	MAX_SUPP_SOURCE_CAPS = 2,
};

/**
 * @brief Type of PD reset to send
 */
enum connector_reset {
	/** PD Soft Reset */
	PD_HARD_RESET = 0,
	/** PD Hard Reset */
	PD_DATA_RESET = 1,
};

/**
 * @brief CC Operation Mode
 */
enum ccom_t {
	/** CCOM Rp */
	CCOM_RP,
	/** CCOM Rd */
	CCOM_RD,
	/** CCOM DRP */
	CCOM_DRP
};

/**
 * @brief DRP Mode
 */
enum drp_mode_t {
	/** DRP Normal */
	DRP_NORMAL,
	/** DRP Try.SRC */
	DRP_TRY_SRC,
	/** DRP Try.SNK */
	DRP_TRY_SNK,
	/** DRP Invalid */
	DRP_INVALID,
};

/**
 * @brief PDO Type
 */
enum pdo_type_t {
	/** Sink PDO */
	SINK_PDO,
	/** Source PDO */
	SOURCE_PDO,
};

/**
 * @brief Port Partner Connection Type
 */
enum conn_partner_type_t {
	/** DFP_ATTACHED */
	DFP_ATTACHED = 1,
	/** UFP_ATTACHED */
	UFP_ATTACHED = 2,
	/** POWERED_CABLE_NO_UFP_ATTACHED */
	POWERED_CABLE_NO_UFP_ATTACHED = 3,
	/** POWERED_CABLE_UFP_ATTACHED */
	POWERED_CABLE_UFP_ATTACHED = 4,
	/** DEBUG_ACCESSORY_ATTACHED */
	DEBUG_ACCESSORY_ATTACHED = 5,
	/** AUDIO_ADAPTER_ACCESSORY_ATTACHED */
	AUDIO_ADAPTER_ACCESSORY_ATTACHED = 6,
};

/**
 * @brief Power Operation Mode
 */
enum power_operation_mode_t {
	/** USB_DEFAULT_OPERATION */
	USB_DEFAULT_OPERATION = 1,
	/** BC_OPERATION */
	BC_OPERATION = 2,
	/** PD_OPERATION */
	PD_OPERATION = 3,
	/** USB_TC_CURRENT_1_5A */
	USB_TC_CURRENT_1_5A = 4,
	/** USB_TC_CURRENT_3A */
	USB_TC_CURRENT_3A = 5,
	/** USB_TC_CURRENT_5A */
	USB_TC_CURRENT_5A = 6,
};

/**
 * @brief List of possible VDO message origins
 */
enum vdo_origin_t {
	/** Retrieve VDO from PDC port */
	VDO_ORIGIN_PORT = 0,
	/** Retrieve VDO from port partner */
	VDO_ORIGIN_SOP = 1,
	/** Retrieve VDO from port cable (SOP') */
	VDO_ORIGIN_SOP_PRIME = 2,
	/** Retrieve VDO from port cable (SOP'') */
	VDO_ORIGIN_SOP_PRIME_PRIME = 3,
};

/**
 * @brief
 * List of types of VDOs that can be retrieved via the Realtek GET_VDO command.
 * Refer to GET_VDO command details in section 4.2 Realtek Power Delivery
 * Command Interface spec
 */
enum vdo_type_t {
	VDO_ID_HEADER = 1,
	VDO_CERT_STATE = 2,
	VDO_PRODUCT = 3,
	VDO_PRODUCT_TYPE_1 = 4,
	VDO_PRODUCT_TYPE_2 = 5,
	VDO_PRODUCT_TYPE_3 = 6,
	VDO_SVID_RESPONSE_1 = 7,
	VDO_SVID_RESPONSE_2 = 8,
	VDO_SVID_RESPONSE_3 = 9,
	VDO_SVID_RESPONSE_4 = 10,
	VDO_SVID_RESPONSE_5 = 11,
	VDO_SVID_RESPONSE_6 = 12,
	VDO_PD_DP_CAPS = 13,
	VDO_PD_DP_STATUS = 14,
	VDO_PD_DP_CFG = 15,
};

/**
 * @brief CCI - USB Type-C Command Status and Connector Change Indication
 */
union cci_event_t {
	struct {
		/**
		 * Used in multi-chunk commands such as a FW update
		 * request (FW Update Request Indicator =1) or
		 * security request (Security Request Indicator =1).
		 * For all other commands, it is reserved and shall
		 * be set to 0.
		 */
		uint32_t end_of_message : 1;
		/**
		 * Used to indicate the connector number that a change
		 * occurred on. Valid values are 0 to the maximum number
		 * of connectors supported on the platform.
		 * If this field is set to zero, then no change occurred
		 * on any of the connectors.
		 */
		uint32_t connector_change : 7;
		/**
		 * Length of valid data in bytes. If this value is greater
		 * than zero, then the user's Data Structure contents are
		 * valid. The value in this register shall be less than or
		 * equal to PDC_MAX_DATA_LENGTH.
		 */
		uint32_t data_len : 8;
		/**
		 * This bit is set when a custom defined message is ready.
		 * It is mutually exclusive with any other Indicator. On
		 * some commands, like the FW update, this bit is repurposed.
		 *
		 * Vendor Defined Behavior:
		 *   This bit has been repurposed as an interrupt indicator.
		 *   It is set when an interrupt has occurred.
		 */
		uint32_t vendor_defined_indicator : 1;
		/** Reserved and shall be set to zero. */
		uint32_t reserved0 : 6;
		/**
		 * For a Security Request, set to 1 when the request comes
		 * from the Port Partner (Asynchronous message). Otherwise
		 * set to 0.
		 */
		uint32_t security_request : 1;
		/**
		 * For an LPM FW Update Request, set to 1 when the request
		 * comes from the Port Partner (Asynchronous message).
		 * Otherwise set to 0.
		 */
		uint32_t fw_update_request : 1;
		/**
		 * Indicate's that the PDC does not currently support a
		 * command. This field shall only be valid when the
		 * Command Completed Indicator field is set to one.
		 */
		uint32_t not_supported : 1;
		/**
		 * Is set to one when a command has been canceled.
		 * This field shall only be valid when the Command Completed
		 * Indicator field is set to one.
		 */
		uint32_t cancel_completed : 1;
		/**
		 * Is set when the PPM_RESET command is completed.
		 * If this field is set to one, then no other bits in this
		 * Data Structure shall be set.
		 */
		uint32_t reset_completed : 1;
		/**
		 * Is set when the PDC or driver is busy. If this field is
		 * set to one, then no other bits in this Data Structure
		 * shall be set.
		 */
		uint32_t busy : 1;
		/** Not used */
		uint32_t acknowledge_command : 1;
		/**
		 * Is set when the PDC or driver encounters an error.
		 * This field shall only be valid when the Command Completed
		 * Indicator field is set to one.
		 *
		 * Vendor Defined Behavior:
		 *   If the vendor_defined_indicator is set, this bit
		 *   is used to indicate an error occurred while processing
		 *   the interrupt.
		 */
		uint32_t error : 1;
		/** Is set a command is completed. */
		uint32_t command_completed : 1;
	};
	uint32_t raw_value;
};

/**
 * @brief Indicates the reason for a reported error
 */
union error_status_t {
	struct {
		/** Unrecognized command */
		uint32_t unrecognized_command : 1;
		/** Non-existent connector number */
		uint32_t non_existent_connector_number : 1;
		/** Invalid command specific parameters */
		uint32_t invalid_command_specific_param : 1;
		/** Incompatible connector partner */
		uint32_t incompatible_connector_partner : 1;
		/** CC communication error */
		uint32_t cc_communication_error : 1;
		/** Command unsuccessful due to dead battery condition */
		uint32_t cmd_unsuccessful_dead_batt : 1;
		/** Contract negotiation failure */
		uint32_t contract_negotiation_failed : 1;
		/** Overcurrent */
		uint32_t overcurrent : 1;
		/** Undefined */
		uint32_t undefined : 1;
		/** Port partner rejected swap */
		uint32_t port_partner_rejected_swap : 1;
		/** Hard Reset */
		uint32_t hard_reset : 1;
		/** PPM Policy Conflict */
		uint32_t ppm_policy_conflict : 1;
		/** Swap Rejected */
		uint32_t swap_rejected : 1;
		/** Reverse Current Protection */
		uint32_t reverse_current_protection : 1;
		/** Set Sink Path Rejected */
		uint32_t set_sink_path_rejected : 1;
		/** Reserved and shall be set to zero */
		uint32_t reserved0 : 1;

		/** Vendor Specific Bits follow */

		/** I2C communication with PDC succeeds, but the data read is
		 * invalid */
		uint32_t pdc_internal_error : 1;
		/** PDC init failed */
		uint32_t pdc_init_failed : 1;
		/** I2C Read Error */
		uint32_t i2c_read_error : 1;
		/** I2c Write Error */
		uint32_t i2c_write_error : 1;
		/** Null Buffer Error */
		uint32_t null_buffer_error : 1;
		/** Port Disabled */
		uint32_t port_disabled : 1;
	};
	uint32_t raw_value;
};

/**
 * @brief PDC Notifications that trigger an IRQ
 */
union notification_enable_t {
	struct {
		/** Command Completed */
		uint32_t command_completed : 1;
		/** (Optional) External Supply Change */
		uint32_t external_supply_change : 1;
		/** Power Operation Mode Change */
		uint32_t power_operation_mode_change : 1;
		/** (Optional) Attention */
		uint32_t attention : 1;
		/** (Optional) FW Update Request */
		uint32_t fw_update_request : 1;
		/** (Optional) Provider Capabilities Change */
		uint32_t provider_capability_change_supported : 1;
		/** (Optional) Negotiated Power Level Change */
		uint32_t negotiated_power_level_change : 1;
		/** (Optional) PD Reset Complete */
		uint32_t pd_reset_complete : 1;
		/** (Optional) Supported CAM Change */
		uint32_t support_cam_change : 1;
		/** Battery Charging Status Change */
		uint32_t battery_charging_status_change : 1;
		/** (Optional) Security Request from Port Partner */
		uint32_t security_request_from_port_partner : 1;
		/** Connector partner Change */
		uint32_t connector_partner_change : 1;
		/** Power Direction Change */
		uint32_t power_direction_change : 1;
		/** (Option) Set Re-timer Mode */
		uint32_t set_retimer_mode : 1;
		/** Connect Change */
		uint32_t connect_change : 1;
		/** Error */
		uint32_t error : 1;
		/** Sink Path Status Change */
		uint32_t sink_path_status_change : 1;
		/** Reserved and shall be set to zero */
		uint32_t reserved0 : 15;
	};
	uint32_t raw_value;
};

/**
 * @brief capabilities of a connector
 */
union connector_capability_t {
	struct {
		/**
		 * The op_mode_x fields indicate wht mode
		 * that the connector supports
		 */

		/** RP only */
		uint32_t op_mode_rp_only : 1;
		/** RD only */
		uint32_t op_mode_rd_only : 1;
		/** DRP */
		uint32_t op_mode_drp : 1;
		/** Analog audio Accessory (Ra/Ra) */
		uint32_t op_mode_analog_audio : 1;
		/** Debug Accessory Mode (Rd/Rd) */
		uint32_t op_mode_debug_acc : 1;
		/** USB2 */
		uint32_t op_mode_usb2 : 1;
		/** USB3 */
		uint32_t op_mode_usb3 : 1;
		/** Alternate Modes */
		uint32_t op_mode_alternate : 1;
		/**
		 * Valid only when the operation mode is DRP or
		 * Rp only. This bit shall be set to one if the
		 * connector is capable of providing power on
		 * this connector.
		 */
		uint32_t provider : 1;
		/**
		 * Valid only when the operation mode is DRP or
		 * Rd only. This bit shall be set to one if the
		 * connector is capable of consuming power on
		 * this connector.
		 */
		uint32_t consumer : 1;
		/**
		 * Valid only when the operation mode is DRP or
		 * Rp only or Rd only. This bit shall be set to one if the
		 * connector is capable of accepting swap to DFP.
		 */
		uint32_t swap_to_dfp : 1;
		/**
		 * Valid only when the operation mode is DRP or
		 * Rp only or Rd only. This bit shall be set to one if the
		 * connector is capable of accepting swap to UFP.
		 */
		uint32_t swap_to_ufp : 1;
		/**
		 * Valid only when the operation mode is DRP. This
		 * bit shall be set to one if the connector is capable of
		 * accepting swap to SRC.
		 */
		uint32_t swap_to_src : 1;
		/**
		 * Valid only when the operation mode is DRP. This
		 * bit shall be set to one if the connector is capable of
		 * accepting swap to SNK.
		 */
		uint32_t swap_to_snk : 1;
		/** USB4 Gen 2 */
		uint32_t ext_op_mode_usb4_gen2 : 1;
		/** EPR Source */
		uint32_t ext_op_mode_epr_source : 1;
		/** EPR Sink */
		uint32_t ext_op_mode_epr_sink : 1;
		/** USB4 Gen 3 */
		uint32_t ext_op_mode_usb4_gen3 : 1;
		/** USB4 Gen 4 */
		uint32_t ext_op_mode_usb4_gen4 : 1;
		/** Reserved */
		uint32_t ext_op_mode_reserved0 : 1;
		/** Reserved */
		uint32_t ext_op_mode_reserved1 : 1;
		/** Reserved */
		uint32_t ext_op_mode_reserved2 : 1;
		/** FW Update */
		uint32_t misc_caps_fw_update : 1;
		/** Security */
		uint32_t misc_caps_security : 1;
		/** Reserved, set to 0 */
		uint32_t misc_caps_reserved0 : 1;
		/** Reserved, set to 0 */
		uint32_t misc_caps_reserved1 : 1;
		/**
		 * Debug information. This bit shall be set to one
		 * the the feature is supported. Otherwise, this bit shall
		 * be set to zero.
		 */
		uint32_t reverse_current_prot : 1;
		/**
		 * Port Partner’s major USB PD Revision from the Specification
		 * Revision field of the USB PD message Header.
		 */
		uint32_t partner_pd_revision : 2;
		/** Reserved, set to 0 */
		uint32_t reserved : 3;
	};
	uint32_t raw_value;
};

/**
 * @brief Connector Status Change Field
 */
union conn_status_change_bits_t {
	struct {
		/** Reserved, set to 0 */
		uint16_t reserved0 : 1;
		/**
		 * When set to 1b, the GET_PDO command can be sent to the
		 * attached supply.
		 */
		uint16_t external_supply_change : 1;
		/**
		 * When set to 1b, the Power Operation Mode field in the STATUS
		 * Data Structure shall indicate the current power operational
		 * mode of the connector.
		 */
		uint16_t pwr_operation_mode : 1;
		/**
		 * This bit shall be set to 1b when the PDC receives an
		 * attention from the port partner.
		 */
		uint16_t attention : 1;
		/** Reserved, set to 0 */
		uint16_t reserved1 : 1;
		/**
		 * When set to 1b, the updated Power Data Objects should be
		 * requested using the GET_PDOS command.
		 */
		uint16_t supported_provider_caps : 1;
		/**
		 * When set to 1b, the Request Data Object field in the STATUS
		 * Data Structure shall indicate the newly negotiated power
		 * level.
		 */
		uint16_t negotiated_power_level : 1;
		/**
		 * This bit shall be set to 1b when the PDC completes a PD Hard
		 * Reset requested by the connector partner.
		 */
		uint16_t pd_reset_complete : 1;
		/**
		 * When set to 1b, the updated Alternate Modes should be
		 * read with the GET_CAM_SUPPORTED command.
		 */
		uint16_t supported_cam : 1;
		/**
		 * This bit shall be set to 1b when the Battery Charging
		 * status changes.
		 */
		uint16_t battery_charging_status : 1;
		/**
		 * Reserved, set to 0.
		 */
		uint16_t reserved2 : 1;
		/**
		 * This bit shall be set to 1b when the Connector Partner
		 * Type field or Connector Partner Flags change.
		 */
		uint16_t connector_partner : 1;
		/**
		 * This bit shall be set to 1b when the PDC completes a Power
		 * Role Swap is completed.
		 */
		uint16_t pwr_direction : 1;
		/**
		 * This bit shall be set to 1b when the Sink Path
		 * Status changes.
		 */
		uint16_t sink_path_status_change : 1;
		/**
		 * This bit shall be set to 1b when a device gets either
		 * connected or disconnected and the Connect Status field
		 * in the GET_CONNECTOR_STATUS Data Structure changes.
		 */
		uint16_t connect_change : 1;
		/**
		 * When set to 1b, this field shall indicate that an error
		 * has occurred on the connector.
		 */
		uint16_t error : 1;
	};
	uint16_t raw_value;
};

#define CONNECTOR_PARTNER_FLAG_USB BIT(0)
#define CONNECTOR_PARTNER_FLAG_ALTERNATE_MODE BIT(1)
#define CONNECTOR_PARTNER_FLAG_USB4_GEN3 BIT(2)
#define CONNECTOR_PARTNER_FLAG_USB4_GEN4 BIT(4)
#define CONNECTOR_PARTNER_PD_CAPABLE             \
	(CONNECTOR_PARTNER_FLAG_ALTERNATE_MODE | \
	 CONNECTOR_PARTNER_FLAG_USB4_GEN3 | CONNECTOR_PARTNER_FLAG_USB4_GEN4)

/**
 * @brief Current status of the connector
 */
union connector_status_t {
	struct {
		/**
		 * A bitmap indicating the types of status changes that have
		 * occurred on the connector.
		 */
		unsigned raw_conn_status_change_bits : 16;
		/**
		 * This field is only valid when the Connect Status field is set
		 * to one. This field shall indicate the current power operation
		 * mode of the connector.
		 */
		enum power_operation_mode_t power_operation_mode : 3;
		/**
		 * This field indicates the current connect status of the
		 * connector. This field shall be set to one when a device is
		 * connected to this connector.
		 */
		unsigned connect_status : 1;
		/**
		 * This field is only valid when the Connect Status field is set
		 * to one. The field shall indicate whether the connector is
		 * operating as a consumer or provider.
		 *  0 - Connector is operating as a consumer
		 *  1 - Connector is operating as a provider
		 */
		unsigned power_direction : 1;
		/**
		 * This field is only valid when the Connect Status field is set
		 * to one. This field indicates the current mode the connector
		 * is operating in. 0 - USB (USB 2.0 or USB 3.x) 1 - Alternate
		 * Mode 2 - USB4 gen 3 3 - USB4 gen 4 4 - Reserved 5 - Reserved
		 *  6 - Reserved
		 *  7 - Reserved
		 */
		unsigned conn_partner_flags : 8;

		/**
		 * This field is only valid when the Connect Status field is set
		 * to one. This field indicates the type of connector partner
		 * detected on this connector.
		 */
		enum conn_partner_type_t conn_partner_type : 3;
		/**
		 * This field is only valid when the Connect Status field is set
		 * to one and the Power Operation Mode field is set to PD.
		 * Optional: Is the Requested Data Object
		 */
		unsigned rdo : 32;
		/**
		 * This field is only valid if the connector is operating as a
		 * Sink. Slow or very slow charging rate shall be indicated only
		 * if it's determined that the currently negotiated contract (or
		 * current level) is not sufficient for nominal charging rate.
		 *
		 * As an example, if the nominal charging rate capability is
		 * 45W: Slow charging rate capability is indicated when the
		 *  negotiated power level is between 27W and 45W.
		 *
		 *  Very slow charging rate capability is indicated when the
		 *  negotiated power level is between 15W and 27W.
		 *
		 *  No charging capability is indicated when the negotiated
		 *  power level is less than 15W.
		 *
		 *  0 - Not charging
		 *  1 - Nominal charging rate
		 *  2 - Slow charging rate
		 *  3 - Very slow charging rate
		 */
		unsigned battery_charging_cap_status : 2;
		/**
		 * A bitmap indicating the reasons why the Provider capabilities
		 * of the connector have been limited. This field is only valid
		 * if the connector is operating as a provider.
		 *
		 * 0 - Power Budget Lowered
		 * 1 - Reaching Power Budget Limit
		 * 2 - Reserved
		 * 3 - Reserved
		 */
		unsigned provider_caps_limited_reason : 4;
		/**
		 * This field indicates the USB Power Delivery Specification
		 * Revision Number the connector uses during an Explicit
		 * Contract (as described in the [USBPD]), and the format is in
		 * Binary-Coded Decimal (e.g., Revision 3.0 is 300H).
		 */
		unsigned bcd_pd_version : 16;
		/**
		 * This field shall be set to 0 when the connection is in the
		 * direct orientation. This field shall be set to 1 when the
		 * connection is in the flipped orientation.
		 */
		unsigned orientation : 1;
		/**
		 * This field shall indicate the status of the Sink Path. The
		 * bit shall be set to one if the sink path is enabled and set
		 * to zero if the sink is disabled.
		 */
		unsigned sink_path_status : 1;
		/**
		 * This field is valid if the Reverse Current Protection Support
		 * field is set to one in the GET_CONNECTOR_CAPABILITY. This
		 * field shall be set to one when the Reverse Current Protection
		 * happens. Otherwise, this bit shall be set to zero.
		 */
		unsigned reverse_current_protection_status : 1;
		/**
		 * This field is set to 1 if the power reading is valid.
		 */
		unsigned power_reading_ready : 1;
		/**
		 * This field indicates the current resolution.
		 * Each bit is 5mA.
		 *
		 * Example of values:
		 *  1b – 5mA
		 *  101b – 25mA
		 */
		unsigned current_scale : 3;
		/**
		 * This field is a peak current measurement reading.
		 * If the ADC supports only less than 16 bits, the most
		 * significant bits shall be set to 0
		 */
		unsigned peak_current : 16;
		/**
		 * This field represents the moving average for the minimum
		 * time interval specified either in the READ_POWER_LEVEL
		 * command or default 100mS of total time with interval of 5mS
		 * if the READ_POWER_LEVEL command has not been issued.
		 * If the ADC supports less than 16 bits, the most significant
		 * bits shall be set to 0.
		 */
		unsigned average_current : 16;
		/**
		 * This field indicates the voltage resolution.
		 * Each bit is 5mV.
		 *
		 * Example of values:
		 *  010b – 10mV
		 *  0101b – 25mV
		 *  1010b – 50mV
		 */
		unsigned voltage_scale : 4;
		/**
		 * This field is the most recent VBUS voltage measurement
		 * within the time window specified by the
		 * READ_POWER_LEVEL command “Time to Read Power” or
		 * 100mS which is the default value.
		 * If the ADC supports less than 16 bits, the most significant
		 * bits shall be set to 0.
		 */
		unsigned voltage_reading : 16;
		unsigned reserved : 7;
	} __packed;
	uint8_t raw_value[19];
};

BUILD_ASSERT(sizeof(union connector_status_t) == DIV_ROUND_UP(145, 8),
	     "sizeof(connector_status_t) incorrect size");

/**
 * @brief Plug End Type
 */
enum plug_end_t {
	/** Type A */
	USB_TYPE_A = 0,
	/** Type B */
	USB_TYPE_B = 1,
	/** Type C */
	USB_TYPE_C = 2,
	/** Not USB */
	USB_TYPE_OTHER = 3
};

/**
 * @brief Cable Property
 */
union cable_property_t {
	struct {
		/* first 32-bit value */

		/**
		 * Supported cable speed.
		 *
		 * Bits[1:0]
		 * Speed Exponent (SE). This field
		 * defines the base 10 exponent times 3,
		 * that shall be applied to the Speed
		 * Mantissa (SM) when calculating the
		 * maximum bit rate that this Cable
		 * supports.
		 *
		 * Bits[15:2]
		 * This field defines the mantissa that
		 * shall be applied to the SE when
		 * calculating the maximum bit rate.
		 */
		uint32_t bm_speed_supported : 16;
		/**
		 * Return the amount of current the cable is designed
		 * for in 50ma units.
		 */
		uint32_t b_current_capability : 8;
		/**
		 * The PPM shall set this field to a one if the cable
		 * has a VBUS connection from end to end.
		 */
		uint32_t vbus_in_cable : 1;
		/**
		 * The PPM shall set this field to one if the cable is
		 * an Active cable otherwise it shall set this field to
		 * zero if the cable is a Passive cable.
		 */
		uint32_t cable_type : 1;
		/**
		 * The PPM shall set this field to one if the lane
		 * directionality is configurable else it shall set this
		 * field to zero if the lane directionality is fixed in
		 * the cable.
		 */
		uint32_t directionality : 1;
		/**
		 * Plug type.
		 */
		enum plug_end_t plug_end_type : 2;
		/**
		 * This field shall only be valid if the CableType field
		 * is set to one. This field shall indicate that the cable
		 * supports Alternate Modes.
		 *
		 * The OPM can use the GET_ALTERNATE_MODE command to get
		 * the list of modes this cable supports.
		 */
		uint32_t mode_support : 1;
		/**
		 * Cable’s major USB PD Revision from the Specification
		 * Revision field of the USB PD Message Header.
		 */
		uint32_t cable_pd_revision : 2;

		/* second 32-bit value */

		/**
		 * See Table 6-41 in the [USBPD] for additional
		 * information on the contents of this field.
		 */
		uint32_t latency : 4;
		/**
		 * Reserved
		 */
		uint32_t reserved : 28;
	};
	uint32_t raw_value[2];
};

/**
 * @brief Option Features
 */
union bmOptionalFeatures_t {
	struct {
		/** SET_CCOM supported */
		uint16_t set_ccom : 1;
		/** SET_POWER_LEVEL supported */
		uint16_t set_power_level : 1;
		/** Alternate mode details supported */
		uint16_t alt_mode_details : 1;
		/** Alternate mode override supported */
		uint16_t alt_mode_override : 1;
		/** PDO details supported */
		uint16_t pdo_details : 1;
		/** Cable details supported */
		uint16_t cable_details : 1;
		/** External supply notification supported */
		uint16_t external_supply_notify : 1;
		/** PD reset notification supported */
		uint16_t pd_reset_notify : 1;
		/** GET_PD_MESSAGE supported */
		uint16_t get_pd_message : 1;
		/** Get Attention VDO */
		uint16_t get_attention_vdo : 1;
		/** FW Update Request */
		uint16_t fw_update_request : 1;
		/** Negotiated Power Level Change */
		uint16_t negotiated_power_level_change : 1;
		/** Security Request */
		uint16_t security_request : 1;
		/** Set Re-timer Mode */
		uint16_t set_retimer_mode : 1;
		/** Chunking Support */
		uint16_t chunking_supported : 1;
		/** Reserved */
		uint16_t reserved : 1;
	};
	uint16_t raw_value;
};

/**
 * @brief bmAttributes of the connector
 */
union bmAttributes_t {
	struct {
		/**
		 * This bit shall be set to one to indicate this platform
		 * supports the Disabled State as defined in
		 * Section 4.5.2.2.1 in the [USBTYPEC].
		 */
		uint32_t disabled_state_supported : 1;
		/**
		 * This bit shall be set to one to indicate this platform
		 * supports the Battery Charging Specification as per the
		 * value reported in the bcdBCVersion field.
		 */
		uint32_t battery_charging : 1;
		/**
		 * This bit shall be set to one to indicate this platform
		 * supports the USB Power Delivery Specification as per the
		 * value reported in the bcdPDVersion field.
		 */
		uint32_t usb_power_delivery : 1;
		/** Reserved, set to 0 */
		uint32_t reserved0 : 3;
		/**
		 * This bit shall be set to one to indicate this platform
		 * supports power capabilities defined in the USB Type-C
		 * Specification as per the value reported in the
		 * bcdUSBTypeCVersion field.
		 */
		uint32_t usb_typec_current : 1;
		/** Reserved, set to 0 */
		uint32_t reserved1 : 1;
		/**
		 * bmPowerSource
		 * At least one of the following bits 8, 10 and 14 shall
		 * be set to indicate which power sources are supported.
		 */

		/** AC Supply */
		uint32_t power_source_ac_supply : 1;
		/** Reserved and shall be set to zero. */
		uint32_t reserved2 : 1;
		/** Other */
		uint32_t power_source_other : 1;
		/** Reserved and shall be set to zero. */
		uint32_t reserved3 : 3;
		/** Uses VBUS */
		uint32_t power_source_uses_vbus : 1;
		/** Reserved and shall be set to zero. */
		uint32_t reserved4 : 17;
	};
	uint32_t raw_value;
};

/**
 * @brief PDC capabilities
 */
struct capability_t {
	/** Bitmap encoding of supported PDC features */
	union bmAttributes_t bmAttributes;
	/** Number of Connectors on the PDC */
	uint8_t bNumConnectors;
	/**
	 * Bitmap encoding indicating which optional features are
	 * supported by the PDC
	 */
	union bmOptionalFeatures_t bmOptionalFeatures;
	/** Reserved, set to 0. */
	uint8_t reserved0;
	/**
	 * This field indicates the number of Alternate Modes that is
	 * supports.
	 *
	 * A value of zero in this field indicates that no Alternate Modes
	 * are supported.
	 *
	 * The complete list of Alternate Modes supported can be obtained
	 * using the GET_ALTERNATE_MODE command.
	 */
	uint8_t bNumAltModes;
	/** Reserved, set to 0. */
	uint8_t reserved1;
	/**
	 * Battery Charging Specification Release Number in BinaryCoded Decimal
	 * (e.g., V1.20 is 120H). This field shall only be valid if the device
	 * indicates that it supports BC in the bmAttributes field.
	 */
	uint16_t bcdBCVersion;
	/**
	 * USB Power Delivery Specification Revision Number in
	 * Binary-Coded Decimal (e.g. Revision 3.0 is 300h). This field
	 * shall only be valid if the device indicates that it supports PD
	 * in the bmAttributes field.
	 */
	uint16_t bcdPDVersion;
	/**
	 * USB Type-C Specification Release Number in Binary-Coded
	 * Decimal (e.g. Release 2.0 is 200h). This field shall only be
	 * valid if the device indicates that it supports USB Type-C in
	 * the bmAttributes field.
	 */
	uint16_t bcdUSBTypeCVersion;
} __packed;

/**
 * @brief CC Operation Mode
 */
union cc_operation_mode_t {
	struct {
		/**
		 * If this bit is set, then the connector
		 * shall operate as Rp Only.
		 */
		uint8_t rp_only : 1;
		/**
		 * If this bit is set, then the connector
		 * shall operate as Rd Only.
		 */
		uint8_t rd_only : 1;
		/**
		 * If this bit is set, then the connector
		 * shall operate as a DRP.
		 */
		uint8_t drp : 1;
		/**
		 * Reserved */
		uint8_t reserved : 5;
	};
	uint8_t raw_value;
};

/**
 * @brief USB Operation Role
 */
union uor_t {
	struct {
		/**
		 * This field indicates the connector whose USB
		 * operational role is to be modified.
		 */
		uint16_t connector_number : 7;
		/**
		 * If this bit is set, then the connector
		 * shall initiate swap to DFP if not
		 * already operating in DFP mode.
		 */
		uint16_t swap_to_dfp : 1;
		/**
		 * If this bit is set, then the connector
		 * shall initiate swap to UFP if not
		 * already operating in UFP mode.
		 */
		uint16_t swap_to_ufp : 1;
		/**
		 * If this bit is set, then the connector
		 * shall accept role swap change
		 * requests from the port partner.
		 * If this bit is cleared, then connector
		 * shall reject Role Swap change
		 * requests from the port partner.
		 */
		uint16_t accept_dr_swap : 1;
		/* Reserved */
		uint16_t reserved : 5;
	};
	uint16_t raw_value;
};

/**
 * @brief Power Direction Role
 */
union pdr_t {
	struct {
		/**
		 * This field indicates the connector whose Power
		 * Direction Role is to be modified.
		 */
		uint16_t connector_number : 7;
		/**
		 * If this bit is set then the connector
		 * shall initiate swap to Source, if not
		 * already operating as Source
		 */
		uint16_t swap_to_src : 1;
		/**
		 * If this bit is set then the connector
		 * shall initiate swap to Sink, if not
		 * already operating as Sink
		 */
		uint16_t swap_to_snk : 1;
		/**
		 * If this bit is set, then the connector
		 * shall accept power swap change
		 * requests from the port partner.
		 * If this bit is cleared, then the
		 * connector shall reject power swap
		 * change requests from the port
		 * partner
		 */
		uint16_t accept_pr_swap : 1;
		/** Reserved */
		uint16_t reserved : 5;
	};
	uint16_t raw_value;
};

/**
 * @brief PDOs received from source
 */
struct pdo_t {
	/* PDO0 */
	uint32_t pdo0;
	/* PDO1 */
	uint32_t pdo1;
	/* PDO2 */
	uint32_t pdo2;
	/* PDO3 */
	uint32_t pdo3;
};

/**
 * @brief USB Operation Role
 */
union connector_reset_t {
	struct {
		/**
		 * This field indicates the connector to reset
		 */
		uint8_t connector_number : 7;
		/**
		 * If this bit is set, then a DATA_RESET is requeseted,
		 * else the reset type is HARD_RESET
		 */
		uint8_t reset_type : 1;
	};
	uint8_t raw_value;
};

/**
 * @brief GET_VDO command
 */
union get_vdo_t {
	struct {
		/**
		 * Number of VDOs requested
		 */
		uint8_t num_vdos : 3;
		/**
		 * Used to specify if VDOs requested are from the PDC,
		 * port parter, or cable.
		 */
		uint8_t vdo_origin : 2;
		/** Reserved, set to 0. */
		uint8_t reserved : 3;
	};
	uint8_t raw_value;
};

/**
 * @brief response for UCSI_GET_LPM_PPM_INFO
 */
struct lpm_ppm_info_t {
	/** USB vendor ID */
	uint16_t vid;
	/** USB product ID */
	uint16_t pid;
	/** ID assigned by USB-IF for compliance */
	uint32_t xid;
	/** FW version */
	uint32_t fw_ver;
	/** FW sub-version */
	uint32_t fw_ver_sub;
	/** Hardware version */
	uint32_t hw_ver;
} __packed;

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_UCSI_V3_H_ */

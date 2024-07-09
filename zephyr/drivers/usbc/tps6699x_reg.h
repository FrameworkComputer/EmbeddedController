/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * @file
 * @brief TI TPS6699x Register addresses and i2c command structure
 */
#ifndef __CROS_EC_PDC_TPS6699X_REG_H
#define __CROS_EC_PDC_TPS6699X_REG_H

#include "compile_time_macros.h"

#include <stdint.h>

/**
 * @brief TPS6699x Registers Addresses
 */
enum tps6699x_reg {
	REG_VENDOR_ID = 0x00,
	REG_DEVICE_ID = 0x01,
	REG_PROTOCOL_VERSION = 0x02,
	REG_MODE = 0x03,
	REG_UID = 0x05,
	REG_CUSTOMER_USE = 0x06,
	REG_COMMAND_FOR_I2C1 = 0x08,
	REG_DATA_FOR_CMD1 = 0x09,
	REG_DEVICE_CAPABILITIES = 0x0d,
	REG_VERSION = 0x0f,
	REG_COMMAND_FOR_I2C2 = 0x10,
	REG_DATA_FOR_CMD2 = 0x11,
	REG_INTERRUPT_EVENT_FOR_I2C1 = 0x14,
	REG_INTERRUPT_EVENT_FOR_I2C2 = 0x15,
	REG_INTERRUPT_MASK_FOR_I2C1 = 0x16,
	REG_INTERRUPT_MASK_FOR_I2C2 = 0x17,
	REG_INTERRUPT_CLEAR_FOR_I2C1 = 0x18,
	REG_INTERRUPT_CLEAR_FOR_I2C2 = 0x19,
	REG_STATUS = 0x1a,
	REG_SX_CONFIG = 0x1f,
	REG_SET_SX_APP_CONFIG = 0x20,
	REG_DISCOVERED_SVIDS = 0x21,
	REG_CONNECTION_MANAGER_STATUS = 0x22,
	REG_USB_CONFIG = 0x23,
	REG_USB_STATUS = 0x24,
	REG_CONNECTION_MANAGER_CONTROL = 0x25,
	REG_POWER_PATH_STATUS = 0x26,
	REG_GLOBAL_SYSTEM_CONFIGURATION = 0x27,
	REG_PORT_CONFIGURATION = 0x28,
	REG_PORT_CONTROL = 0x29,
	REG_BOOT_FLAG = 0x2d,
	REG_BUILD_DESCRIPTION = 0x2e,
	REG_DEVICE_INFORMATION = 0x2f,
	REG_RECEIVED_SOURCE_CAPABILITIES = 0x30,
	REG_RECEIVED_SINK_CAPABILITIES = 0x31,
	REG_TRANSMIT_SOURCE_CAPABILITES = 0x32,
	REG_TRANSMIT_SINK_CAPABILITES = 0x33,
	REG_ACTIVE_PDO_CONTRACT = 0x34,
	REG_ACTIVE_RDO_CONTRACT = 0x35,
	REG_AUTONEGOTIATE_SINK = 0x37,
	REG_SPM_CLIENT_CONTROL = 0x3c,
	REG_SPM_CLIENT_STATUS = 0x3d,
	REG_PD_STATUS = 0x40,
	REG_PD3_STATUS = 0x41,
	REG_PD3_CONFIGURATION = 0x42,
	REG_DELAY_CONFIG = 0x43,
	REG_TX_IDENTITY = 0x47,
	REG_RECEIVED_SOP_IDENTITY_DATA_OBJECT = 0x48,
	REG_RECEIVED_SOP_PRIME_IDENTITY_DATA_OBJECT = 0x49,
	REG_USER_ALTERNATE_MODE_CONFIGURATION = 0x4a,
	REG_RECEIVED_ATTENTION_VDM = 0x4e,
	REG_DISPLAY_PORT_CONFIGURATION = 0x51,
	REG_THUNDERBOLT_CONFIGURATION = 0x52,
	REG_SPECIAL_CONFIGURATION = 0x55,
	REG_PROCHOT_CONFIGURATION = 0x56,
	REG_USER_VID_STATUS = 0x57,
	REG_DISPLAY_PORT_STATUS = 0x58,
	REG_INTEL_VID_STATUS = 0x59,
	REG_RETIMER_DEBUG = 0x5d,
	REG_DATA_STATUS = 0x5f,
	REG_RECEIVED_USER_SVID_ATTENTIONVDM = 0x60,
	REG_RECEIVED_USER_SVID_OTHER_VDM = 0x61,
	REG_APP_CONFIG_BINARY_DATA_INDICES = 0x62,
	REG_I2C_CONTROLLER_CONFIG = 0x64,
	REG_TYPEC_STATUS = 0x69,
	REG_ADC_RESULTS = 0x6a,
	REG_APP_CONFIG = 0x6c,
	REG_STATE_CONFIG = 0x6f,
	REG_SLEEP_CONTROL = 0x70,
	REG_GPIO_STATUS = 0x72,
	REG_TX_MANUFACTURER_INFO_SOP = 0x73,
	REG_RECEIVED_ALERT_DATA_OBJECT = 0x74,
	REG_TX_ALERT_DATA_OBJECT = 0x75,
	REG_TX_SOURCE_CAPABILITIES_EXTENDED_DATA_BLOCK = 0x77,
	REG_TRANSMITTED_STATUS_DATA_BLOCK = 0x79,
	REG_TRANSMITTED_PPS_STATUS_DATA_BLOCK = 0x7a,
	REG_TRANSMITTED_BATTERY_STATUS_DATA_OBJECT = 0x7b,
	REG_TX_BATTERY_CAPABILITIES = 0x7d,
	REG_TRANSMIT_SINK_CAPABILITIES_EXTENDED_DATA_BLOCK = 0x7e,
	REG_UUID_HANDLE = 0x80,
	REG_EXTERNAL_DCDC_STATUS = 0x94,
	REG_EXTERNAL_DCDC_PARAMETERS = 0x95,
	REG_EPR_CONFIG = 0x97,
	REG_GPIO_P0 = 0xa0,
	REG_GPIO_P1 = 0xa1,
	REG_GPIO_EVENT_CONFIG = 0xa3
};

/**
 * @brief Standard Task Response
 *
 * Returned in Output DATAX, bits 3:0, when a 4CC Task is sent
 */
enum std_task_response {
	TASK_COMPLETED_SUCCESSFULLY = 0,
	TASK_TIMED_OUT_OR_ABORTED = 1,
	TASK_REJECTED = 3,
	TASK_REJECTED_RX_BUFFER_LOCKED = 4,
};

/**
 * @brief 4.1 Vendor ID Register (Offset = 0x00)
 *
 * Intel-assigned Thunderbolt Vendor ID
 */
union reg_vendor_id {
	struct {
		/* TODO(b/345783692): These fields don't need to be included in
		 * every register definition. The I2C write process can write
		 * from 2 separate buffers.
		 */
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t data[4];
	} __packed;
	uint8_t raw_value[6];
};

/**
 * @brief 4.2 Device ID Register (Offset = 0x01)
 *
 * Vendor-specific Device ID
 */
union reg_device_id {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t data[4];
	} __packed;
	uint8_t raw_value[6];
};

/**
 * @brief 4.3 Protocol Version Register (Offset = 0x02)
 *
 * Thunderbolt Protocol Version
 */
union reg_protocol_version {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t data[4];
	} __packed;
	uint8_t raw_value[6];
};

/**
 * @brief 4.4 Mode Register (Offset = 0x03)
 *
 * Indicates the operational state of a port.
 */
union reg_mode {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t data[4];
	} __packed;
	uint8_t raw_value[6];
};

/**
 * @brief 4.5 Type Register (Offset = 0x04)
 *
 * Default response is "I2C " (not space as last character)
 */
union reg_type {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t data[4];
	} __packed;
	uint8_t raw_value[6];
};

/**
 * @brief 4.6 UID Register (Offset = 0x05)
 *
 * 128-bit unique ID (unique for each PD Controller Port)
 */
union reg_uid {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t data[16];
	} __packed;
	uint8_t raw_value[18];
};

/**
 * @brief 4.7 Customer Use Register (Offset = 0x06)
 *
 * These 8-bytes are allocated for customer use as needed. The PD controller
 * does not use this register.
 */
union reg_customer_use {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		/**
		 * The first byte is a version code, set using the firmware
		 * config tool.
		 */
		uint8_t fw_config_version;
		uint8_t data[7];
	} __packed;
	uint8_t raw_value[10];
};

/* Values to be written to the CMD registers, indicating the task to be started
 * by the PDC. The command field is nominally a 4-byte ASCII string, not
 * null-terminated. The values of this enum are the corresponding little-endian,
 * uint32_t values for each string of 4 bytes. These values are listed in
 * TPS6699x TRM chapter 10, 4CC Task Detailed Descriptions.
 * TODO(b/345783692): Give unused tasks their real values.
 */
enum command_task {
	/* Invalid command */
	COMMAND_TASK_NO_COMMAND = 0x444d4321,
	/* Cold reset request */
	COMMAND_TASK_GAID = 0x44494147,
	/* Simulate port disconnect */
	COMMAND_TASK_DISC = 0x43534944,
	/* PD PR_Swap to Sink */
	COMMAND_TASK_SWSK = 0x6b535753,
	/* PD PR_Swap to Source */
	COMMAND_TASK_SWSR = 0x72535753,
	/* PD DR_Swap to DFP */
	COMMAND_TASK_SWDF,
	/* PD DR_Swap to UFP */
	COMMAND_TASK_SWUF,
	/* PD Get Sink Capabilties */
	COMMAND_TASK_GSKC,
	/* PD Get Source Capabilities */
	COMMAND_TASK_GSRC,
	/* PD Get Port Partner Information */
	COMMAND_TASK_GPPI,
	/* PD Send Source Capabilities */
	COMMAND_TASK_SSRC,
	/* PD Data Reset */
	COMMAND_TASK_DRST,
	/* Message Buffer Read */
	COMMAND_TASK_MBRD,
	/* Send Alert Message */
	COMMAND_TASK_ALRT,
	/* PD Send Enter Mode */
	COMMAND_TASK_AMEN,
	/* PD Send Exit Mode */
	COMMAND_TASK_AMEX,
	/* PD Start Alternate Mode Discovery */
	COMMAND_TASK_AMDS,
	/* Get Custom Discovered Modes */
	COMMAND_TASK_GCDM,
	/* PD Send VDM */
	COMMAND_TASK_VDMS,
	/* System ready to enter sink power */
	COMMAND_TASK_SRDY = 0x59445253,
	/* SRDY reset */
	COMMAND_TASK_SRYR = 0x52595253,
	/* Firmware update tasks */
	COMMAND_TASK_TFUS = 0x73554654,
	COMMAND_TASK_TFUC = 0x63554654,
	COMMAND_TASK_TFUD = 0x64554654,
	COMMAND_TASK_TFUE = 0x65554654,
	COMMAND_TASK_TFUI = 0x69554654,
	COMMAND_TASK_TFUQ = 0x71554654,
	/* Abort current task */
	COMMAND_TASK_ABRT,
	/*Auto Negotiate Sink Update */
	COMMAND_TASK_ANEG,
	/* Clear Dead Battery Flag */
	COMMAND_TASK_DBFG,
	/* Error handling for I2C3m transactions */
	COMMAND_TASK_MUXR,
	/* Trigger an Input GPIO Event */
	COMMAND_TASK_TRIG,
	/* I2C read transaction */
	COMMAND_TASK_I2CR,
	/* I2C write transaction */
	COMMAND_TASK_I2CW,
	/* UCSI tasks */
	COMMAND_TASK_UCSI = 0x49534355,
};
BUILD_ASSERT(sizeof(enum command_task) == sizeof(uint32_t));

/**
 * @brief 4.8 Command Register for I2C1 (Offset = 0x08)
 * @brief 4.12 Command Register for I2C2 (Offset = 0x10)
 *
 * Command register for the primary command interface. If an unrecognized
 * command is written to this register, it is replaced by a 4CC value of "!CMD".
 */
union reg_command {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint32_t command : 32;
	} __packed;
	uint8_t raw_value[6];
};

/**
 * @brief 4.9 Data Register for CMD1 (Offset = 0x09)
 * @brief 4.13 Data Register for CMD2 (Offset = 0x11)
 *
 * Data register for the primary command interface.
 */
union reg_data {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t data[64];
	} __packed;
	uint8_t raw_value[66];
};

/**
 * @brief 4.10 Device Capabilities Register (Offset = 0x0d)
 *
 * Description of supported features.
 */
union reg_device_capabilities {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t power_role : 2;
		uint8_t usb_pd_capability : 1;
		uint8_t tbt_present : 1;
		uint8_t single_port : 1;
		uint8_t reserved0 : 3;

		uint8_t reserved1 : 8;
		uint8_t reserved2 : 8;
		uint8_t reserved3 : 8;
	} __packed;
	uint8_t raw_value[6];
};

/**
 * @brief 4.11 Version Register (Offset = 0x0f)
 *
 * Boot Firmware Version
 */
union reg_version {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint32_t version : 32;
	} __packed;
	uint8_t raw_value[6];
};

/**
 * @brief 4.14 Interrupt Event for I2C1 (Offset = 0x14)
 *        4.15 Interrupt Event for I2C2 (Offset = 0x15)
 *        4.16 Interrupt Mask for I2C1 (Offset = 0x16)
 *        4.17 Interrupt Mask for I2C2 (Offset = 0x17)
 *        4.18 Interrupt Clear for I2C1 (Offset = 0x18)
 *        4.19 Interrupt Clear for I2C2 (Offset = 0x19)
 *
 * Interrupt Event:
 *   Interrupt event bit field for I1C_EC_IRQ. If any bit is 1, then the
 * I2C_EC_IRQ pin is pulled low.
 *
 * Interrupt Mask:
 *   Interrupt mask bit field for INT_EVENT. A bit cannot be set if it is
 * cleared in this register.
 *
 * Interrupt Clear:
 *   Interrpt clear bit field for INT_EVENT. Bits set in this register are
 * cleared from INT_EVENT.
 *
 */
union reg_interrupt {
	struct {
		/** Used for */
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		/* Bits 0 - 7 */
		uint8_t reserved0 : 1;
		uint8_t pd_hardreset : 1;
		uint8_t reserved1 : 1;
		uint8_t plug_insert_or_removal : 1;
		uint8_t power_swap_complete : 1;
		uint8_t data_swap_complete : 1;
		uint8_t fr_swap_complete : 1;
		uint8_t source_cap_updated : 1;

		/* Bits 8 - 15 */
		uint8_t reserved2 : 1;
		uint8_t overcurent : 1;
		uint8_t attention_received : 1;
		uint8_t vdm_received : 1;
		uint8_t new_contract_as_consumer : 1;
		uint8_t new_contract_as_producer : 1;
		uint8_t source_caps_msg_received : 1;
		uint8_t sink_caps_msg_received : 1;

		/* Bits 16 - 23 */
		uint8_t reserved3 : 1;
		uint8_t power_swap_rquested : 1;
		uint8_t data_swap_requested : 1;
		uint8_t reserved4 : 1;
		uint8_t usb_host_present : 1;
		uint8_t usb_host_no_longer_present : 1;
		uint8_t reserved5 : 1;
		uint8_t power_path_switch_changed : 1;

		/* Bits 24 - 31 */
		uint8_t power_status_update : 1;
		uint8_t data_status_update : 1;
		uint8_t status_updated : 1;
		uint8_t pd_status_updated : 1;
		uint8_t reserved6 : 2;
		uint8_t cmd1_complete : 1;
		uint8_t cmd2_complete : 1;

		/* Bits 32 - 39 */
		uint8_t device_incompatible_error : 1;
		uint8_t cannot_provide_voltage_or_current_error : 1;
		uint8_t can_provide_voltage_or_current_later_error : 1;
		uint8_t power_event_occurred_error : 1;
		uint8_t missing_get_caps_msg_error : 1;
		uint8_t reserved7 : 1;
		uint8_t protocol_error : 1;
		uint8_t reserved8 : 1;

		/* Bits 40 - 47 */
		uint8_t reserved9 : 2;
		uint8_t sink_transition_completeed : 1;
		uint8_t plug_early_notification : 1;
		uint8_t prochot_notification : 1;
		uint8_t reserved10 : 1;
		uint8_t unable_to_source_error : 1;
		uint8_t reserved11 : 1;

		/* Bits 48 - 55 */
		uint8_t am_entry_fail : 1;
		uint8_t am_entered : 1;
		uint8_t reserved12 : 1;
		uint8_t discover_mode_completed : 1;
		uint8_t exit_mode_completed : 1;
		uint8_t data_reset_start : 1;
		uint8_t usb_status_update : 1;
		uint8_t connection_manager_update : 1;

		/* Bits 56 - 63 */
		uint8_t usvid_mode_entered : 1;
		uint8_t usvid_mode_exited : 1;
		uint8_t usvid_attention_vdm_received : 1;
		uint8_t usvid_other_vdm_received : 1;
		uint8_t reserved13 : 1;
		uint8_t externl_dcdc_event_received : 1;
		uint8_t dp_sid_status_updated : 1;
		uint8_t intel_vid_status_updated : 1;

		/* Bits 64 - 71 */
		uint8_t pd3_status_updated : 1;
		uint8_t tx_memory_buffer_empty : 1;
		uint8_t mbrd_bufer_ready : 1;
		uint8_t reserved14 : 3;
		uint8_t event_soc_ack_timeout : 1;
		uint8_t not_supported_received : 1;

		/* Bits 72 - 79 */
		uint8_t reserved15 : 2;
		uint8_t i2c_comm_error_with_external_PP : 1;
		uint8_t externl_dcdc_status_change : 1;
		uint8_t frs_signal_received : 1;
		uint8_t chunk_response_received : 1;
		uint8_t chunk_request_received : 1;
		uint8_t alert_message_received : 1;

		/* Bits 80 - 87 */
		uint8_t patch_loaded : 1;
		uint8_t ready_for_f211_image : 1;
		uint8_t reserved16 : 2;
		uint8_t boot_error : 1;
		uint8_t ready_for_next_data_block : 1;
		uint8_t reserved17 : 2;
	} __packed;
	uint8_t raw_value[13];
};

/**
 * @brief 4.20 Status Register (Offset = 0x1a)
 *
 * Status bit field for non-interrupt events.
 */
union reg_status {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t plug_present : 1;
		uint8_t connection_state : 3;
		uint8_t plug_orientation : 1;
		uint8_t port_role : 1;
		uint8_t data_role : 1;
		uint8_t epr_mode_is_active : 1;

		uint8_t reserved0 : 8;

		uint8_t reserved1 : 4;
		uint8_t vbus_status : 2;
		uint8_t usb_host_present : 2;

		uint8_t acting_as_legacy : 2;
		uint8_t reserved2 : 1;
		uint8_t bist : 1;
		uint8_t reserved4 : 2;
		uint8_t soc_ack_timeout : 1;
		uint8_t reserved5 : 1;

		uint8_t am_status : 2;
		uint8_t reserved6 : 6;
	};
	uint8_t raw_value[7];
};

/**
 * @brief 4.21 SX Config Register (Offset = 0x1f)
 *
 * Power state configuration. The Host may write the current system power state,
 * and a change in power state triggers a new Application Configuration to be
 * applied.
 */
union reg_sx_config {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t s0_config_enable : 1;
		uint8_t s0_config_address_port1 : 3;
		uint8_t reserved0 : 4;
		uint8_t reserved1[6];
		uint8_t s3_config_enable : 1;
		uint8_t s3_config_address_port1 : 3;
		uint8_t reserved2 : 4;
		uint8_t reserved3[6];
		uint8_t s4_config_enable : 1;
		uint8_t s4_config_address_port1 : 3;
		uint8_t reserved4 : 4;
		uint8_t reserved5[6];
		uint8_t s5_config_enable : 1;
		uint8_t s5_config_address_port1 : 3;
		uint8_t reserved6 : 4;
		uint8_t reserved7[6];
	};
	uint8_t raw_value[26];
};

/**
 * @brief 4.22 SX App Config Register (Offset = 0x20)
 *
 * Configuration based on system state. The Host may write the current system
 * power state, and change in power state triggers an new Application
 * Configuration to be applied.
 */
union reg_sx_app_config {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t sleep_state : 3;
		uint8_t reserved0 : 5;
		uint8_t reserved1 : 8;
	};
	uint8_t raw_value[4];
};

/**
 * @brief 4.23 Discovered SVIDs Register (Offset = 0x21)
 *
 * Received Discover SVID ACK message(s). This register contains the SVID
 * information returned from Discover SVIDs REQ messages.
 */
union reg_discovered_svids {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t number_sop_svids : 4;
		uint8_t number_sopprime_svids : 4;

		uint16_t svid_sop_0 : 16;
		uint16_t svid_sop_1 : 16;
		uint16_t svid_sop_2 : 16;
		uint16_t svid_sop_3 : 16;
		uint16_t svid_sop_4 : 16;
		uint16_t svid_sop_5 : 16;
		uint16_t svid_sop_6 : 16;
		uint16_t svid_sop_7 : 16;

		uint16_t svid_sopprime_0 : 16;
		uint16_t svid_sopprime_1 : 16;
		uint16_t svid_sopprime_2 : 16;
		uint16_t svid_sopprime_3 : 16;
		uint16_t svid_sopprime_4 : 16;
		uint16_t svid_sopprime_5 : 16;
		uint16_t svid_sopprime_6 : 16;
		uint16_t svid_sopprime_7 : 16;
	} __packed;
	uint8_t raw_value[35];
};

/**
 * @brief 4.24 Connection Manager Status Register (Offset 0x22)
 *
 * Connection Manager Status shows the capabilities of the host connected.
 */
union reg_connection_manager_status {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t reserved0 : 1;
		uint8_t usb2_host_connected : 1;
		uint8_t usb3_host_connected : 1;
		uint8_t dp_host_connected : 1;
		uint8_t tbt_host_connected : 1;
		uint8_t usb4_host_connected : 1;
		uint8_t pcie_host_connected : 1;
		uint8_t reserved1 : 1;
	} __packed;
	uint8_t raw_value[3];
};

/**
 * @brief 4.25 USB Config Register (Offset = 0x23)
 *
 * USB configuration.
 */
union reg_usb_config {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t reserved0 : 8;

		uint8_t reserved1 : 5;
		uint8_t host_present : 1;
		uint8_t tbt3_supported : 1;
		uint8_t dp_supported : 1;

		uint8_t pcie_supported : 1;
		uint8_t reserved2 : 7;

		uint8_t reserved3 : 1;
		uint8_t usb3_drd : 1;
		uint8_t usb4_drd : 1;
		uint8_t reserved4 : 5;
	} __packed;
	uint8_t raw_value[6];
};

/**
 * @brief 4.26 USB Status Register (Offset = 0x24)
 *
 * USB Status
 */
union reg_usb_status {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t eudo_sop_sent_or_received : 2;
		uint8_t usb4_required_plug_mode : 2;
		uint8_t usb_mode_active_on_plug : 1;
		uint8_t reserved0 : 1;
		uint8_t usb_rentry_needed : 1;
		uint8_t reserved1 : 1;

		uint32_t usb4_enter_usb_rx_tx : 32;
		uint32_t reserved2 : 32;
	} __packed;
	uint8_t raw_value[11];
};

/**
 * @brief 4.27 Connection Manager Control Register (Offset 0x25)
 *
 * Connection Manager Control used to exchange the capabilities from connection
 * Manager status
 */
union reg_connection_manager_control {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t pl4_vbus_vconn_enable : 1;
		uint8_t usb2_host_connected : 1;
		uint8_t usb3_host_connected : 1;
		uint8_t dp_host_connected : 1;
		uint8_t tbt_host_connected : 1;
		uint8_t usb4_host_connected : 1;
		uint8_t pcie_host_connected : 1;
		uint8_t reserved : 1;
	} __packed;
	uint8_t raw_value[3];
};

/**
 * @brief 4.28 Power Path Status Register (Offset 0x26)
 *
 * Power Path Status
 */
union reg_power_path_status {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint32_t pa_vconn_sw : 2;
		uint32_t pb_vconn_sw : 2;
		uint32_t reserved0 : 2;
		uint32_t pa_int_vbus_sw : 3;
		uint32_t pb_int_vbus_sw : 3;
		uint32_t pa_ext_vbus_sw : 3;
		uint32_t pb_ext_vbus_sw : 3;
		uint32_t reerved1 : 10;
		uint32_t pa_int_vbus_sw_oc : 1;
		uint32_t pb_int_vbus_sw_oc : 1;
		uint32_t reserved2 : 2;

		uint32_t reserved3 : 2;
		uint32_t pa_vconn_sw_oc : 1;
		uint32_t pb_vconn_sw_oc : 1;
		uint32_t reserved4 : 2;
		uint32_t power_source : 2;
	} __packed;
	uint8_t raw_value[7];
};

/**
 * @brief 4.29 Global System Configuration Register (Offset = 0x27)
 *
 * Global system configuration (all ports)
 */
union reg_global_system_configuration {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		/* Bits 0 - 7 */
		uint8_t pa_vconn_confg : 1;
		uint8_t reserved0 : 1;
		uint8_t pb_vconn_config : 1;
		uint8_t reserved1 : 5;

		/* Bits 8 - 15 */
		uint8_t pa_pp5v_vbus_sw_config : 3;
		uint8_t pb_pp5v_vbus_sw_config : 3;
		uint8_t ilim_over_shoot : 2;

		/* Bits 16 - 23 */
		uint8_t pa_ppext_vbus_sw_config : 3;
		uint8_t pb_ppext_vbus_sw_config : 3;
		uint8_t rc_threshold : 2;

		/* Bits 24 - 31 */
		uint8_t multi_port_sink_policy : 1;
		uint8_t reserved2 : 1;
		uint8_t tbt_controller_type : 3;
		uint8_t enable_one_ufp_policy : 1;
		uint8_t enable_spm : 1;
		uint8_t _reserved3 : 1;

		/* Bits 32 - 39 */
		uint8_t reserved4 : 1;
		uint8_t enable_i2c_multi_controller_mode : 1;
		uint8_t i2c_timeout : 3;
		uint8_t disable_eeprom_updates : 1;
		uint8_t emulate_single_port : 1;
		uint8_t minimum_current_advertisement : 1;

		/* Bits 40 - 47 */
		uint8_t reserved5 : 3;
		uint8_t usb_default_current : 2;
		uint8_t epr_supported_as_source : 1;
		uint8_t epr_supported_as_sink : 1;
		uint8_t enable_am_entry_exit_on_lp_mode : 1;

		/* Bits 48 - 55 */
		uint8_t reserved6 : 8;

		/* Bits 56 - 63 */
		uint8_t ext_dcdc_status_polling_interval : 8;

		/* Bits 64 - 71 */
		uint8_t port1_i2c2_target_address : 8;

		/* Bits 72 - 79 */
		uint8_t port2_i2c2_target_address : 8;

		/* Bits 80 - 87 */
		uint8_t vsys_prevents_high_power : 1;
		uint8_t wait_for_vin_3v3 : 1;
		uint8_t wait_for_minimum_power : 1;
		uint8_t reserved7 : 5;

		/* Bits 88 - 95 */
		uint8_t reserved8 : 8;

		/* Bits 96 - 103 */
		uint8_t reserved9 : 7;
		uint8_t source_policy_mode_bit0 : 1;

		/* Bits 104 - 111 */
		uint8_t source_policy_mode_bit1 : 1;
		uint8_t enable_hbretimer_startup : 1;
		uint8_t s4_or_s5_retimer_power_saving : 2;
		uint8_t reserved10 : 4;
	} __packed;
	uint8_t raw_value[16];
};

/**
 * @brief 4.30 Port Configuration Register (Offset = 0x28)
 *
 * Configuration for port-specific hardware.
 */
union reg_port_configuration {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t typec_state_machine : 2;
		uint8_t crossbar_type : 1;
		uint8_t reserved0 : 4;
		uint8_t pp_ext_active_low : 1;

		uint8_t typec_support_options : 2;
		uint8_t disable_pd : 1;
		uint8_t usb_communication_capable : 1;
		uint8_t debug_accessory_support : 1;
		uint8_t usb3_rate : 2;
		uint8_t crossbar_i2c_controller_enable : 1;

		uint8_t vbus_ovp_usage : 2;
		uint8_t soft_start : 2;
		uint8_t ovp_for_pp5v : 2;
		uint8_t crossbar_config_type1_extened : 1;
		uint8_t remove_safe_state_between_usb3_to_dp_transition : 1;

		uint8_t vbus_sink_vp_trip_hv : 3;
		uint8_t apdo_vbus_uvp_threshold : 2;
		uint8_t apdo_ilim_over_shoot : 2;
		uint8_t reserved1 : 1;

		uint16_t apdo_vbus_uvp_trip_point_offset : 16;

		uint16_t vbus_for_valid_pps_status : 16;

		uint8_t external_dcdc_type : 8;

		uint8_t sink_mode_i2c_irq_config : 1;
		uint8_t reserved2 : 7;

		uint16_t greater_than_threshold_voltage : 16;

		uint8_t reserved3 : 8;
		uint8_t reserved4 : 8;
		uint8_t reserved5 : 8;

		uint8_t reserved6 : 4;
		uint8_t enable_internal_aux_biasing : 1;
		uint8_t enable_internal_level_shifter : 1;
		uint8_t level_shifter_direction_cfg : 2;

		uint8_t sbu_mux_debug_setting : 3;
		uint8_t sbu_mux_default_setting : 3;
		uint8_t sbu_mux_usage : 2;

	} __packed;
	uint8_t raw_value[19];
};

/**
 * @brief 4.31 Port Control Register (Offset = 0x29)
 *
 * Configuration bits affecting system policy.
 */
union reg_port_control {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		/* Bits 0 - 7 */
		uint8_t typec_current : 2;
		uint8_t reserved : 2;
		uint8_t process_swap_to_sink : 1;
		uint8_t initiate_swap_to_sink : 1;
		uint8_t process_swap_to_source : 1;
		uint8_t initiate_swap_to_source : 1;

		/* Bits 8 - 15 */
		uint8_t automatic_cap_request : 1;
		uint8_t auto_alert_enable : 1;
		uint8_t auto_pps_status_enable : 1;
		uint8_t retimer_fw_update : 1;
		uint8_t process_swap_to_ufp : 1;
		uint8_t initiate_swap_to_ufp : 1;
		uint8_t process_swap_to_dfp : 1;
		uint8_t initiate_swap_to_dfp : 1;

		/* Bits 16 - 23 */
		uint8_t automatic_id_request : 1;
		uint8_t am_intrusive_mode : 1;
		uint8_t force_usb3_gen1 : 1;
		uint8_t unconstrained_power : 1;
		uint8_t enable_current_monitor : 1;
		uint8_t sink_control_bit : 1;
		uint8_t fw_swap_enabled : 1;
		uint8_t reserved0 : 1;

		/* Bits 24 - 31 */
		uint8_t reserved2 : 5;
		uint8_t usb_disable : 1;
		uint8_t reserved3 : 2;

		/* Bits 32 - 39 */
		uint8_t enable_peak_current : 1;
		uint8_t llim_threshold_hi : 4;
		uint8_t deglitch_cnt_hi : 3;

		/* Bits 40 - 47 */
		uint8_t deglitch_cnt_lo : 3;
		uint8_t vconn_current_limit : 2;
		uint8_t level_shifter_direction_ctrl : 1;
		uint8_t reserved4 : 2;
	} __packed;
	uint8_t raw_value[8];
};

/**
 * @brief 4.32 Boot Flags Register (Offset = 0x2d)
 *
 * Detailed status of boot process.
 */
union reg_boot_flags {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t boot_state : 4;
		uint8_t reserved0 : 4;
		uint8_t reserved1[7];
		uint8_t total_num_ports : 2;
		uint8_t is_ext_pp_present : 1;
		uint8_t reserved2 : 5;
		uint8_t reserved3[7];

		uint8_t dead_battery_flag : 1;
		uint8_t db_portb_power_provider : 1;
		uint8_t db_porta_power_provider : 1;
		uint8_t porta_sink_switch : 1;
		uint8_t portb_sink_switch : 1;
		uint8_t reserved4 : 3;
		uint8_t reserved5[3];
		uint8_t porta_i2c1_trgt_addr : 8;
		uint8_t portb_i2c1_trgt_addr : 8;
		uint8_t porta_i2c2_trgt_addr : 8;
		uint8_t portb_i2c2_trgt_addr : 8;
		uint8_t porta_i2c4_trgt_addr : 8;
		uint8_t portb_i2c4_trgt_addr : 8;
		uint8_t reserved6[2];
		uint8_t active_bank : 2;
		uint8_t bank0_valid : 1;
		uint8_t bank1_valid : 1;
		uint8_t reserved7 : 4;
		uint8_t reserved8[3];
		uint8_t bank0_fw_version[4];
		uint8_t bank1_fw_version[4];
		uint8_t adc_in_value[2];
		uint8_t adc_in_index[2];
		uint8_t reserved9[8];
	};
	uint8_t raw_value[54];
};

/**
 * @brief 4.33 Build Description Register (Offset = 0x2e)
 *
 * Build description. This is an ASCII string that uniquely identifies custom
 * build information.
 */
union reg_build_description {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t build_description[392];
	};
	uint8_t raw_value[394];
};

/**
 * @brief 4.34 Device Information Register (Offset = 0x2f)
 *
 * Device Information. This is an ASCII string with hardware and firmware
 * version information of the PD Controller.
 */
union reg_device_information {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t device_info[320];
	} __packed;
	uint8_t raw_value[322];
};

/**
 * @brief 4.35 Received Source Capabilities Register (Offset = 0x30)
 *
 * Received Source Capabilities. This register stores latest Source Capabilities
 * message received over BMC.
 */
union reg_received_source_capabilities {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t number_of_valid_pdos : 3;
		uint8_t number_of_valid_epr_pdos : 3;
		uint8_t last_src_cap_received_is_epr : 1;
		uint8_t reserved : 1;

		uint32_t spr_source_pdo[7];
		uint32_t epr_source_pdo[6];
	} __packed;
	uint8_t raw_value[55];
};

/**
 * @brief 4.36 Received Sink Capabilities Register (Offset = 0x31)
 *
 * Received Sink Capabilities. This register stores latest Sink Capabilities
 * message received over BMC.
 */
union reg_received_sink_capabilities {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t number_of_valid_pdos : 3;
		uint8_t rx_sink_num_valid_epr_pdos : 3;
		uint8_t last_snk_cap_received_is_epr : 1;
		uint8_t reserved : 1;

		uint32_t spr_sink_pdo[7];
		uint32_t epr_sink_pdo[6];
	} __packed;
	uint8_t raw_value[55];
};

/**
 * @brief 4.37 Transmit Source Capabilities Register (Offset = 0x32)
 *
 * Source Capabilities for sending. This register stores PDOs and settings for
 * outgoing Source Capabilities PD messages. Initialized by Application
 * Customization.
 */
union reg_transmit_source_capabilities {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t number_of_valid_pdos : 3;
		uint8_t tx_source_num_valid_epr_pdos : 3;
		uint8_t reserved0 : 2;

		uint8_t power_path_for_pdo1 : 2;
		uint8_t reserved1 : 6;

		uint8_t reserved2 : 8;

		uint32_t spr_tx_source_pdo[7];
		uint32_t epr_tx_source_pdo[6];

		uint8_t reserved3[4];

		uint8_t virtual_switch_enable_for_pdo1 : 1;
		uint8_t virtual_switch_enable_for_pdo2 : 1;
		uint8_t virtual_switch_enable_for_pdo3 : 1;
		uint8_t virtual_switch_enable_for_pdo4 : 1;
		uint8_t virtual_switch_enable_for_pdo5 : 1;
		uint8_t virtual_switch_enable_for_pdo6 : 1;
		uint8_t virtual_switch_enable_for_pdo7 : 1;
		uint8_t virtual_switch_enable_for_pdo8 : 1;

		uint8_t virtual_switch_enable_for_pdo9 : 1;
		uint8_t virtual_switch_enable_for_pdo10 : 1;
		uint8_t virtual_switch_enable_for_pdo11 : 1;
		uint8_t virtual_switch_enable_for_pdo12 : 1;
		uint8_t virtual_switch_enable_for_pdo13 : 1;
		uint8_t reserved4 : 3;

		uint8_t reserved5[2];
	} __packed;
	uint8_t raw_value[65];
};

/**
 * @brief 4.38 Transmit Sink Capabilities Register (Offset = 0x33)
 *
 * Sink Capabilities for sending. This register stores PDOs for outgoing Sink
 * Capabilities USB PD messages.
 */
union reg_transmit_sink_capabilities {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t number_of_valid_pdos : 3;
		uint8_t tx_sink_num_valid_epr_pdos : 3;
		uint8_t reserved : 2;

		uint32_t spr_tx_sink_pdo[7];
		uint32_t epr_tx_sink_pdo[6];
	} __packed;
	uint8_t raw_value[55];
};

/**
 * @brief 4.39 Active PDO Contract Register (Offset = 0x34)
 *
 * Power data object for active contract. This register stores PDO data for the
 * current explicit USB PD contract, or all zeroes if no contract.
 */
union reg_active_pdo_contract {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint32_t active_pdo : 32;
		/* NOTE: The upper 7 bits should be ignored */
		uint16_t first_pdo_control_bits;
	} _packed;
	uint8_t raw_value[8];
};

/**
 * @brief 4.40 Active RDO Contract Register (Offset = 0x35)
 *
 * Power data object for the active contract. This register stores the RDO of
 * the current explicit USB PD contract, or all zeroes if no contract.
 */
union reg_active_rdo_contract {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint32_t rdo : 32;
		uint32_t source_epr_mode_do : 32;
		uint32_t sink_epr_mode_do : 32;
	} __packed;
	uint8_t raw_value[14];
};

/**
 * @brief 4.41 Autonegotiate Sink Register (Offset = 0x37)
 *
 * Configuration for sink power negotiations. This register defines the voltage
 * range between which the system can function properly, allowing the PD
 * Controller to negotiate its own contracts.
 */
union reg_autonegotiate_sink {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint32_t auto_neg_rdo_priority : 1;
		uint32_t no_usb_suspend : 1;
		uint32_t auto_compute_sink_min_power : 1;
		uint32_t no_capability_mismatch : 1;
		uint32_t auto_compute_sink_min_voltage : 1;
		uint32_t auto_compute_sink_max_voltage : 1;
		uint32_t auto_disable_sink_upon_capability_mismatch : 1;
		uint32_t auto_disable_input_for_sink_standby : 1;
		uint32_t auto_disable_input_for_sink_standby_in_dbm : 1;
		uint32_t auto_enable_standby_srdy : 1;
		uint32_t reserved0 : 2;
		uint32_t auto_neg_max_current : 10;
		uint32_t auto_neg_sink_min_required_power : 10;

		uint32_t auto_neg_max_voltage : 10;
		uint32_t auto_neg_min_voltage : 10;
		uint32_t auto_neg_capabilities_mismach_power : 10;
		uint32_t reserved1 : 2;

		uint32_t pps_enable_sink_mode : 1;
		uint32_t pps_request_interval : 2;
		uint32_t pps_source_operating_mode : 1;
		uint32_t pps_reuired_full_voltage_range : 1;
		uint32_t pps_disable_sink_upon_non_apdo_contract : 1;
		uint32_t reserved2 : 26;

		uint32_t pps_operating_current : 7;
		uint32_t reserved3 : 2;
		uint32_t pps_output_voltage : 11;
		uint32_t reserved4 : 12;

		uint32_t epr_avs_enable_sink_mode : 1;
		uint32_t reserved5 : 31;

		uint32_t epr_avs_operating_current : 7;
		uint32_t reserved6 : 2;
		uint32_t epr_avs_output_voltage : 12;
		uint32_t reserved7 : 11;
	} __packed;
	uint8_t raw_value[26];
};

/**
 * @brief SPM Client Control Register (Offset = 0x3c)
 *
 * Source Policy Manager Client Control register used to allocate power to the
 * port. All PDO voltages are exposed, current for each PDO is adjusted to stay
 * within the allocated power.
 */
union reg_spm_client_control {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t spm_allocated_extra_power : 8;
		uint8_t reserved0 : 8;
		uint8_t spm_guaranteed_power : 8;
		uint8_t reserved1 : 8;
		uint8_t port_disabled_by_spm : 8;
		uint8_t spm_forced_safe_state_power : 8;
		uint8_t reserved2 : 8;
	} __packed;
	uint8_t raw_value[9];
};

/**
 * @brief SPM Client Status Register (Offset = 0x3d)
 *
 * Source Policy Manager Client Status register used to indicate power at the
 * port.
 */
union reg_spm_client_status {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intren_len : 8;

		uint8_t spm_total_vbus_power_used : 8;
		uint8_t reserved0 : 8;
		uint8_t spm_requested_total_vbus_power : 8;
		uint8_t reserved1 : 8;
		uint8_t spm_cap_mismatch : 8;
	} __packed;
	uint8_t raw_value[7];
};

/**
 * @brief 4.44 PD Status Register (Offset = 0x40)
 *
 * Status of PD and Type-C state-machine. This register contains details
 * regarding the status of PD messages and the Type-C state machine
 */
union reg_pd_status {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t reserved0 : 2;
		uint8_t cc_pullup : 2;
		uint8_t port_type : 2;
		uint8_t present_pd_role : 1;
		uint8_t reserved1 : 1;

		uint8_t soft_reset_details : 5;
		uint8_t reserved2 : 3;

		uint16_t hard_reset_details : 6;
		uint16_t error_recovery_details : 6;
		uint16_t data_reset_details : 3;
		uint16_t reserved3 : 1;
	} __packed;

	uint8_t raw_value[6];
};

/**
 * @brief 4.45 PD3 Status Register (Offset = 0x41)
 *
 * Status bit field for PD3.0 messages and state machine.
 */
union reg_pd3_status {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint16_t firmware_update_response_message_received : 1;
		uint16_t reserved0 : 4;
		uint16_t firmware_update_request_message_received : 1;
		uint16_t reserved1 : 5;
		uint16_t firmware_update_response_message_dropped : 1;
		uint16_t reserved2 : 1;
		uint16_t firmware_update_request_message_dropped : 1;
		uint16_t reserved3 : 2;

		uint16_t reserved4 : 3;
		uint16_t port_negotiated_vdm_version_minor : 2;
		uint16_t plug_negotiated_vdm_version_minor : 2;
		uint16_t port_negotiated_vdm_version : 2;
		uint16_t plug_negotiated_vdm_version : 2;
		uint16_t port_negotiated_spec_revision : 2;
		uint16_t plug_negotiated_spec_revision : 2;
		uint16_t use_unchunked_messages : 1;

		uint16_t reserved5 : 16;

		uint16_t version_minor : 4;
		uint16_t version_major : 4;
		uint16_t revision_minor : 4;
		uint16_t revision_major : 4;

		uint16_t reserved6 : 5;
		uint16_t vendor_defined_ext_msg_received : 1;
		uint16_t vendor_defined_ext_msg_dropped : 1;
		uint16_t reserved8 : 1;
	} __packed;
	uint8_t raw_value[11];
};

/**
 * @brief 4.46 PD3 Control Register (Offset = 0x42)
 *
 * PD3.0 configuration settings
 */
union reg_pd3_configuration {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t port_max_spec_revision : 2;
		uint8_t plug_max_spec_revision : 2;
		uint8_t unchunked_supported : 1;
		uint8_t reserved0 : 3;

		uint8_t support_source_extended_message : 1;
		uint8_t support_status_message : 1;
		uint8_t support_battery_capabilites_message : 1;
		uint8_t support_battery_status_message : 1;
		uint8_t support_manufacture_info_message : 1;
		uint8_t reserved1 : 1;
		uint8_t support_firmware_upgrade_messgae : 1;
		uint8_t reserved2 : 1;

		uint8_t support_country_code_info : 1;
		uint8_t support_sink_cap_extended : 1;
		uint8_t support_get_source_info : 1;
		uint8_t support_get_revision : 1;
		uint8_t support_pps_status : 1;
		uint8_t support_vendor_defined_extended : 1;
		uint8_t reserved3 : 2;

		uint8_t override_svdm_version_2_1 : 1;
		uint8_t reserved4 : 7;
	} __packed;
	uint8_t raw_value[6];
};

/**
 * @brief 4.48 TX Identity Register (Offset = 0x47)
 *
 * Data to use for Discover Identity ACK. This data is sent in the
 * response to Discover Identity REQ message. Initialized by Application
 * Customization.
 */
union reg_tx_identity {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t number_valid_vdos : 3;
		uint8_t reserved0 : 5;
		uint8_t vendor_id[2];
		uint8_t reserved1 : 7;
		uint8_t product_type_dfp_lo_bit : 1;
		uint8_t product_type_dfp_hi_bits : 2;

		uint8_t modal_operation_supported : 1;
		uint8_t product_type_ufp : 3;
		uint8_t usb_comms_capable_as_device : 1;
		uint8_t usb_comms_capable_as_host : 1;
		uint8_t certification_test_id[4];
		uint8_t bcd_device[2];
		uint8_t product_id[2];
		uint8_t ufp1_vdo[4];
		uint8_t reserved2[4];
		uint8_t dfp1_vdo[4];
		uint8_t reserved3[24];
	} __packed;
	uint8_t raw_value[51];
};

/**
 * @brief 4.49 Received SOP Identity Data Object Register (Offset = 0x48)
 *	  4.50 Received SOP Prime Identity Data Object Register (Offset = 49)
 *
 * Received SOP:
 *   Received Discover Identity ACK (SOP). Latest Discover Identity response
 *received over USB PD using SOP.
 *
 * Received SOP Prime:
 *   Received Discover Identity ACK (SOP' or SOP''). Latest Discover Identity
 *response received over USB PD using SOP'.
 */
union reg_received_identity_data_object {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t number_valid_vdos : 3;
		uint8_t reserved0 : 3;
		uint8_t response_type : 2;

		uint32_t vdo[6];
	} __packed;
	uint8_t raw_value[27];
};

/**
 * @brief 4.62 Data Status Register (Offset 0x5f)
 */
union reg_data_status {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint8_t data_connection : 1;
		uint8_t connection_orientation : 1;
		uint8_t retimer_or_redriver : 1;
		uint8_t over_current_or_temp : 1;
		uint8_t usb2_connection : 1;
		uint8_t usb3_connection : 1;
		uint8_t usb3_speed : 1;
		uint8_t data_role : 1;

		uint8_t dp_connection : 1;
		uint8_t dp_source_sink : 1;
		uint8_t dp_pin_assignment : 2;
		uint8_t debug_accessory_mode : 1;
		uint8_t reserved0 : 1;
		uint8_t hpd_irq_sticky : 1;
		uint8_t hpd_level : 1;

		uint8_t tbt_connection : 1;
		uint8_t tbt_type : 1;
		uint8_t cable_type : 1;
		uint8_t reserved1 : 1;
		uint8_t active_link_training : 1;
		uint8_t debug_alternate_mode : 1;
		uint8_t active_cable : 1;
		uint8_t usb4_connection : 1;

		uint8_t reserved2 : 1;
		uint8_t tbt_cable_support : 3;
		uint8_t tbt_cable_generation : 2;
		uint8_t reserved3 : 2;

		uint8_t debug_alternate_mode_id : 8;
	} __packed;
	uint8_t raw_value[7];
};

/**
 * @brief 4.68 ADC Result Register (Offset = 0x6A)
 */
union reg_adc_results {
	struct {
		uint8_t _intern_reg : 8;
		uint8_t _intern_len : 8;

		uint16_t pa_vbus : 16;
		uint16_t pa_cc1 : 16;
		uint16_t pa_cc2 : 16;
		uint16_t i_pa_pp5v : 16;
		uint16_t i_pa_vbus_avg : 16;
		uint16_t i_pa_vbus_peak : 16;
		uint16_t reserved0 : 16;
		uint16_t reserved1 : 16;
		uint16_t pb_vbus : 16;
		uint16_t pb_cc1 : 16;
		uint16_t pb_cc2 : 16;
		uint16_t i_pb_pp5v : 16;
		uint16_t i_pb_vbus_avg : 16;
		uint16_t i_pb_vbus_peak : 16;
		uint16_t reserved2[10];
		uint16_t ldo_3v3 : 16;
		uint16_t adc_in : 16;
		uint16_t p1_gpio0 : 16;
		uint16_t p1_gpio1 : 16;
		uint16_t p1_gpio2 : 16;
		uint16_t band_gap_temp : 16;
		uint16_t reserved3 : 16;
	} __packed;
	uint8_t raw_value[64];
};

#endif /* __CROS_EC_PDC_TPS6699X_REG_H */

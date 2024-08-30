/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_TPS6699X_PRIVATE_H_
#define __EMUL_TPS6699X_PRIVATE_H_

#include "drivers/ucsi_v3.h"
#include "emul/emul_tps6699x.h"

#include <stdint.h>

#include <zephyr/drivers/gpio.h>

enum tps6699x_reg_offset {
	/* TODO(b/345292002): Fill out */
	TPS6699X_REG_MODE = 0x03,
	TPS6699X_REG_CUSTOMER_USE = 0x06,
	TPS6699X_REG_COMMAND_I2C1 = 0x8,
	TPS6699X_REG_DATA_I2C1 = 0x9,
	TPS6699X_REG_VERSION = 0x0f,
	TPS6699X_REG_INTERRUPT_EVENT_FOR_I2C1 = 0x14,
	TPS6699X_REG_POWER_PATH_STATUS = 0x26,
	TPS6699X_REG_PORT_CONFIGURATION = 0x28,
	TPS6699X_REG_PORT_CONTROL = 0x29,
	TPS6699X_REG_TX_IDENTITY = 0x47,
	TPS6699X_REG_RECEIVED_SOP_IDENTITY_DATA_OBJECT = 0x48,
	TPS6699X_REG_RECEIVED_SOP_PRIME_IDENTITY_DATA_OBJECT = 0x49,
	TPS6699X_REG_ADC_RESULTS = 0x6a,
	TPS6699X_NUM_REG = 0xa4,
};

/* Helper function to convert TI task names to UINT32*/
#define TASK_TO_UINT32(a, b, c, d) \
	((uint32_t)(a | (b << 8) | (c << 16) | (d << 24)))

enum tps6699x_command_task {
	/* Command complete: Not a real command. The TPS6699x clears the command
	 * register when a command completes.
	 */
	COMMAND_TASK_COMPLETE = 0,
	/* Invalid command */
	COMMAND_TASK_NO_COMMAND = TASK_TO_UINT32('!', 'C', 'M', 'D'),
	/* Cold reset request */
	COMMAND_TASK_GAID = TASK_TO_UINT32('G', 'A', 'I', 'D'),
	/* Simulate port disconnect */
	COMMAND_TASK_DISC = TASK_TO_UINT32('D', 'I', 'S', 'C'),
	/* PD PR_Swap to Sink */
	COMMAND_TASK_SWSK = TASK_TO_UINT32('S', 'W', 'S', 'k'),
	/* PD PR_Swap to Source */
	COMMAND_TASK_SWSR = TASK_TO_UINT32('S', 'W', 'S', 'r'),
	/* PD DR_Swap to DFP */
	COMMAND_TASK_SWDF = TASK_TO_UINT32('S', 'W', 'D', 'F'),
	/* PD DR_Swap to UFP */
	COMMAND_TASK_SWUF = TASK_TO_UINT32('S', 'W', 'U', 'F'),
	/* PD Get Sink Capabilities */
	COMMAND_TASK_GSKC = TASK_TO_UINT32('G', 'S', 'k', 'C'),
	/* PD Get Source Capabilities */
	COMMAND_TASK_GSRC = TASK_TO_UINT32('G', 'S', 'r', 'C'),
	/* PD Get Port Partner Information */
	COMMAND_TASK_GPPI = TASK_TO_UINT32('G', 'P', 'P', 'I'),
	/* PD Send Source Capabilities */
	COMMAND_TASK_SSRC = TASK_TO_UINT32('S', 'S', 'r', 'C'),
	/* PD Data Reset */
	COMMAND_TASK_DRST = TASK_TO_UINT32('D', 'R', 'S', 'T'),
	/* Message Buffer Read */
	COMMAND_TASK_MBRD = TASK_TO_UINT32('M', 'B', 'R', 'd'),
	/* Send Alert Message */
	COMMAND_TASK_ALRT = TASK_TO_UINT32('A', 'L', 'R', 'T'),
	/* Send EPR Mode Message */
	COMMAND_TASK_EPRM = TASK_TO_UINT32('E', 'P', 'R', 'm'),
	/* PD Send Enter Mode */
	COMMAND_TASK_AMEN = TASK_TO_UINT32('A', 'M', 'E', 'n'),
	/* PD Send Exit Mode */
	COMMAND_TASK_AMEX = TASK_TO_UINT32('A', 'M', 'E', 'x'),
	/* PD Start Alternate Mode Discovery */
	COMMAND_TASK_AMDS = TASK_TO_UINT32('A', 'M', 'D', 's'),
	/* Get Custom Discovered Modes */
	COMMAND_TASK_GCDM = TASK_TO_UINT32('G', 'C', 'd', 'm'),
	/* PD Send VDM */
	COMMAND_TASK_VDMS = TASK_TO_UINT32('V', 'D', 'M', 's'),
	/* System ready to enter sink power */
	COMMAND_TASK_SRDY = TASK_TO_UINT32('S', 'R', 'D', 'Y'),
	/* SRDY reset */
	COMMAND_TASK_SRYR = TASK_TO_UINT32('S', 'R', 'Y', 'R'),
	/* Power Register Read */
	COMMAND_TASK_PPRD = TASK_TO_UINT32('P', 'P', 'R', 'd'),
	/* Power Register Write */
	COMMAND_TASK_PPWR = TASK_TO_UINT32('P', 'P', 'W', 'r'),
	/* Firmware update tasks */
	COMMAND_TASK_TFUS = TASK_TO_UINT32('T', 'F', 'U', 's'),
	COMMAND_TASK_TFUC = TASK_TO_UINT32('T', 'F', 'U', 'c'),
	COMMAND_TASK_TFUD = TASK_TO_UINT32('T', 'F', 'U', 'd'),
	COMMAND_TASK_TFUE = TASK_TO_UINT32('T', 'F', 'U', 'e'),
	COMMAND_TASK_TFUI = TASK_TO_UINT32('T', 'F', 'U', 'i'),
	COMMAND_TASK_TFUQ = TASK_TO_UINT32('T', 'F', 'U', 'q'),
	/* Abort current task */
	COMMAND_TASK_ABRT = TASK_TO_UINT32('A', 'B', 'R', 'T'),
	/*Auto Negotiate Sink Update */
	COMMAND_TASK_ANEG = TASK_TO_UINT32('A', 'N', 'e', 'g'),
	/* Clear Dead Battery Flag */
	COMMAND_TASK_DBFG = TASK_TO_UINT32('D', 'B', 'f', 'g'),
	/* Error handling for I2C3m transactions */
	COMMAND_TASK_MUXR = TASK_TO_UINT32('M', 'u', 'x', 'R'),
	/* Trigger an Input GPIO Event */
	COMMAND_TASK_TRIG = TASK_TO_UINT32('T', 'r', 'i', 'g'),
	/* I2C read transaction */
	COMMAND_TASK_I2CR = TASK_TO_UINT32('I', '2', 'C', 'r'),
	/* I2C write transaction */
	COMMAND_TASK_I2CW = TASK_TO_UINT32('I', '2', 'C', 'w'),
	/* UCSI tasks */
	COMMAND_TASK_UCSI = TASK_TO_UINT32('U', 'C', 'S', 'I')
};

/* Results of a task, indicated by the PDC in byte 1 of the relevant DATAX
 * register after a command completes. See TPS6699x TRM May 2023, table 10-1
 * Standard Task Response.
 */
enum tps6699x_command_result {
	COMMAND_RESULT_SUCCESS = 0,
	COMMAND_RESULT_TIMEOUT = 1,
	COMMAND_RESULT_REJECTED = 2,
	COMMAND_RESULT_RX_LOCKED = 4,
};

union reg_port_configuration {
	struct {
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
	uint8_t raw_value[17];
};
BUILD_ASSERT(sizeof(union reg_port_configuration) == 17);

union reg_port_control {
	struct {
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

union reg_adc_results {
	struct {
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

enum px_ext_vbus_sw {
	EXT_VBUS_SWITCH_DISABLED = 0,
	EXT_VBUS_SWITCH_DISABLED_FAULT = 1,
	EXT_VBUS_SWITCH_ENABLED_OUTPUT = 2,
	EXT_VBUS_SWITCH_ENABLED_INPUT = 3,
};

union reg_power_path_status {
	struct {
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
	uint8_t raw_value[5];
};

union reg_version {
	struct {
		uint32_t version : 32;
	} __packed;
	uint8_t raw_value[4];
};

union reg_tx_identity {
	struct {
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
	uint8_t raw_value[49];
};

union reg_customer_use {
	struct {
		/**
		 * The first byte is a version code, set using the firmware
		 * config tool.
		 */
		uint8_t data[8];
	} __packed;
	uint8_t raw_value[8];
};

enum tps_mode {
	/** Chip is booting */
	REG_MODE_BOOT = 0x544f4f42,
	/** Firmware update / both banks corrupted */
	REG_MODE_F211 = 0x31313246,
	/** Flash code running pre-appconfig */
	REG_MODE_APP0 = 0x30505041,
	/** Flash code running post-appconfig */
	REG_MODE_APP1 = 0x31505041,
	/** Flash code is waiting for power */
	REG_MODE_WTPR = 0x52505457,
};

union reg_mode {
	struct {
		uint8_t data[4];
	} __packed;
	uint8_t raw_value[6];
};

union reg_interrupt {
	struct {
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
		uint8_t ucsi_connector_status_change_notification : 1;
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
	uint8_t raw_value[11];
};

union reg_received_identity_data_object {
	struct {
		uint8_t number_valid_vdos : 3;
		uint8_t reserved0 : 3;
		uint8_t response_type : 2;

		uint32_t vdo[6];
	} __packed;
	uint8_t raw_value[25];
};

#endif /* __EMUL_TPS6699X_PRIVATE_H_ */

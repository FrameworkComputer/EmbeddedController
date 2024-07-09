/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for Realtek RTS5453P Type-C Power Delivery Controller
 * emulator
 */

#ifndef __EMUL_REALTEK_RTS5453P_H
#define __EMUL_REALTEK_RTS5453P_H

#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_realtek_rts54xx_public.h"
#include "zephyr/kernel.h"

#include <stdint.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

union pd_status_t {
	uint32_t raw_value;
	struct {
		uint32_t command_complete : 1;
		uint32_t external_supply_charge : 1;
		uint32_t power_operation_mode_change : 1;
		uint32_t reserved0 : 2;
		uint32_t provider_capabilities_change : 1;
		uint32_t negotiated_power_level_change : 1;
		uint32_t pd_reset_complete : 1;
		uint32_t supported_cam_change : 1;
		uint32_t battery_charging_status_change : 1;
		uint32_t reserved1 : 1;
		uint32_t port_partner_changed : 1;
		uint32_t power_direction_changed : 1;
		uint32_t reserved2 : 1;
		uint32_t connect_change : 1;
		uint32_t error : 1;
		uint32_t ir_drop : 1;
		uint32_t soft_reset_completed : 1;
		uint32_t error_recovery_occurred : 1;
		uint32_t pd_pio_status_change : 1;
		uint32_t alternate_flow_change : 1;
		uint32_t dp_status_change : 1;
		uint32_t dfp_ocp_change : 1;
		uint32_t port_operation_mode_change : 1;
		uint32_t power_control_request : 1;
		uint32_t vdm_received : 1;
		uint32_t source_sink_cap_received : 1;
		uint32_t data_message_received : 1;
		uint32_t reserved3 : 1;
		uint32_t system_misc_change : 1;
		uint32_t reserved4 : 1;
		uint32_t pd_ams_change : 1;
	};
};

union rts54_request {
	uint8_t raw_data[0];
	struct rts54_command {
		uint8_t command_code;
		uint8_t data_len;
		uint8_t data[32];
	} request;

	struct rts54_subcommand_header {
		uint8_t command_code;
		uint8_t data_len;
		uint8_t sub_cmd;
	} req_subcmd;
	struct vendor_cmd_enable {
		struct rts54_subcommand_header header;
		uint8_t sub_cmd2;
		union vendor_cmd {
			uint8_t raw;
			struct {
				uint8_t smbus : 1;
				uint8_t flash : 1;
				uint8_t reserved : 6;
			};
		} sub_cmd3;
	} vendor_cmd_enable;

	struct set_notification_enable_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
		union pd_status_t data;
	} set_notification_enable;

	struct ppm_reset_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
	} ppm_reset;

	struct tcpm_reset_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
		uint8_t reset_type : 2;
		uint8_t reserved : 6;
	} tcpm_reset;

	struct connector_reset_req {
		struct rts54_subcommand_header header;
		uint8_t data_len;
		union connector_reset_t reset;
	} connector_reset;

	struct get_capability_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
	} get_capability;

	struct get_connector_capability_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
	} get_connector_capability;

	struct get_error_status_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
	} get_error_status;

	struct get_connector_status_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
	} get_connector_status;

	struct get_rtk_status_req {
		uint8_t command_code;
		uint8_t data_len;
		uint8_t offset;
		uint8_t port_num;
		uint8_t sts_len;
	} get_rtk_status;

	struct set_uor_req {
		struct rts54_subcommand_header header;
		uint8_t data_len;
		union uor_t uor;
	} set_uor;

	struct set_pdr_req {
		struct rts54_subcommand_header header;
		uint8_t data_len;
		union pdr_t pdr;
	} set_pdr;

	struct set_rdo_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
		uint32_t rdo;
	} set_rdo;

	struct get_rdo_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
	} get_rdo;

	struct set_tpc_rp_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
		union tpc_rp_t {
			uint8_t raw_value;
			struct {
				uint8_t reserved0 : 2;
				uint8_t tpc_rp : 2;
				uint8_t pd_rp : 2;
				uint8_t reserved1 : 2;
			};
		} tpc_rp;
	} set_tpc_rp;

	struct set_tpc_csd_operation_mode_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
		union csd_op_mode_t {
			uint8_t raw_value;
			struct {
				uint8_t csd_mode : 2;
				uint8_t accessory_support : 1;
				uint8_t drp_mode : 2;
				uint8_t reserved : 3;
			};
		} op_mode;
	} set_tpc_csd_operation_mode;

	struct set_ccom_req {
		struct rts54_subcommand_header header;
		union port_and_ccom_t {
			uint16_t raw_value;
			struct {
				uint16_t port_num : 7;
				uint16_t ccom : 4;
				uint16_t reserved : 5;
			};
		} port_and_ccom;
	} set_ccom;

	struct force_set_power_switch_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
		struct force_set_power_switch_t {
			uint8_t vbsin_en : 2;
			uint8_t lp_en : 2;
			uint8_t reserved : 2;
			uint8_t vbsin_en_control : 1;
			uint8_t lp_en_control : 1;
		} data;
	} force_set_power_switch;

	struct set_tpc_reconnect_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
		uint8_t param0;
	} set_tpc_reconnect;

	struct read_power_level_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
	} read_power_level;

	struct get_pdo {
		struct rts54_subcommand_header header;
		uint8_t port_num;
		struct {
			uint8_t src : 1;
			uint8_t partner : 1;
			uint8_t offset : 3;
			uint8_t num : 3;
		};
		uint32_t pdos[PDO_OFFSET_MAX];
	} __packed get_pdos;

	struct get_cable_property {
		struct rts54_subcommand_header header;
		uint8_t port_num;
	} get_cable_property;

	struct get_vdo_req {
		struct rts54_subcommand_header header;
		uint8_t data_len;
		union get_vdo_t vdo_req;
		uint8_t vdo_type[7];
	} get_vdo;

	struct get_ic_status_req {
		uint8_t command_code;
		uint8_t data_len;
		uint8_t offset;
		uint8_t reserved;
		uint8_t sts_len;
	} get_ic_status;

	struct get_pch_data_status_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
	} get_pch_data_status;

	struct ack_cc_ci_req {
		uint8_t command_code;
		uint8_t data_len;
		uint8_t reserved;
		uint8_t port_num;
		union conn_status_change_bits_t ci;
		uint16_t vendor_defined_ci;
		struct {
			uint8_t cc_ack : 1;
			uint8_t rsvd : 7;
		};
	} __packed ack_cc_ci;
};

union rts54_response {
	uint8_t raw_data[0];
	uint8_t byte_count;
	struct rts54_ic_status {
		uint8_t byte_count;
		uint8_t is_flash_code;
		uint8_t reserved0[2];
		uint8_t fw_main_version;
		uint8_t fw_sub_version[2];
		uint8_t reserved1[2];
		uint8_t pd_ready : 1;
		uint8_t reserved2 : 2;
		uint8_t typec_connected : 1;
		uint8_t reserved3 : 4;
		uint8_t vid[2];
		uint8_t pid[2];
		uint8_t reserved4;
		uint8_t running_flash_bank_offset;
		uint8_t reserved5[7];
		uint8_t pd_revision[2];
		uint8_t pd_version[2];
		uint8_t project_name[12];
	} __packed ic_status;

	struct rts54_ucsi_get_lpm_ppm_info {
		uint8_t byte_count;
		struct lpm_ppm_info_t info;
	} __packed lpm_ppm_info;

	struct get_capability_response {
		uint8_t byte_count;
		struct capability_t caps;
	} __packed capability;

	struct get_connector_capability_response {
		uint8_t byte_count;
		union connector_capability_t caps;
	} __packed connector_capability;

	struct get_error_status_response {
		uint8_t byte_count;
		uint16_t unrecognized_command : 1;
		uint16_t non_existent_connector_number : 1;
		uint16_t invalid_command_specific_param : 1;
		uint16_t incompatible_connector_partner : 1;
		uint16_t cc_communication_error : 1;
		uint16_t cmd_unsuccessful_dead_batt : 1;
		uint16_t contract_negotiation_failed : 1;
		uint16_t overcurrent : 1;
		uint16_t undefined : 1;
		uint16_t port_partner_rejected_swap : 1;
		uint16_t hard_reset : 1;
		uint16_t ppm_policy_conflict : 1;
		uint16_t swap_rejected : 1;
		uint16_t reverse_current_protection : 1;
		uint16_t set_sink_path_rejected : 1;
		uint16_t reserved0 : 1;
	} __packed error_status;

	struct get_connector_status_response {
		uint8_t byte_count;
		union connector_status_t status;
	} __packed connector_status;

	struct get_rtk_status_response {
		/* BYTE 0 */
		uint8_t byte_count;
		/* BYTE 1-4 */
		union pd_status_t pd_status;
		/* BYTE 5 */
		uint8_t supply : 1;
		uint8_t port_operation_mode : 3;
		uint8_t insertion_detect : 1;
		uint8_t pd_capable_cable : 1;
		uint8_t power_direction : 1;
		uint8_t connect_status : 1;
		/* BYTE 6 */
		uint8_t port_partner_flags;
		/* BYTE 7-10 */
		uint32_t request_data_object;
		/* BYTE 11 */
		uint8_t port_partner_type : 3;
		uint8_t battery_charging_status : 2;
		uint8_t pd_sourcing_vconn : 1;
		uint8_t pd_responsible_vconn : 1;
		uint8_t pd_ams_in_progress : 1;
		/* BYTE 12 */
		uint8_t last_or_current_pd_ams : 4;
		uint8_t port_partner_not_support_pd : 1;
		uint8_t plug_direction : 1;
		uint8_t dp_role : 1;
		uint8_t pd_connected : 1;
		/* BYTE 13 */
		uint8_t vbsin_en_switch_status : 2;
		uint8_t lp_en_switch_status : 2;
		uint8_t cable_spec_version : 2;
		uint8_t port_partner_spec_version : 2;
		/* BYTE 14 */
		uint8_t alt_mode_related_status : 3;
		uint8_t dp_lane_swap : 1;
		uint8_t contract_valid : 1;
		uint8_t unchunked_message_support : 1;
		uint8_t fr_swap_support : 1;
		uint8_t reserved : 1;
		/* BYTE 15 */
		uint8_t ucsi_b_power_reading_ready : 1;
		uint8_t ucsi_b_scale_cur : 3;
		uint8_t ucsi_b_scale_vol : 4;
		/* BYTE 16-17 */
		uint8_t average_current_low;
		uint8_t average_current_high;
		/* BYTE 18-19 */
		uint8_t voltage_reading_low;
		uint8_t voltage_reading_high;
	} __packed rtk_status;

	struct get_rdo_response {
		uint8_t byte_count;
		uint32_t rdo;
	} __packed get_rdo;

	struct get_pdo_response {
		uint8_t byte_count;
		uint32_t pdos[PDO_OFFSET_MAX];
	} __packed get_pdos;

	struct get_cable_property_response {
		uint8_t byte_count;
		union {
			struct {
				uint16_t bm_speed_supported;
				uint8_t b_current_capability;
				uint8_t vbus_in_cable : 1;
				uint8_t cable_type : 1;
				uint8_t directionality : 1;
				uint8_t plug_type : 2;
				uint8_t mode_support : 1;
				uint8_t reserved0 : 2;
				uint8_t latency : 4;
				uint8_t reserved1 : 4;
			} __packed;
			uint8_t raw_value[5];
		};
	} __packed get_cable_property;

	struct get_vdo_response {
		uint8_t byte_count;
		uint32_t vdo[7];
	} __packed get_vdo;

	struct get_pch_data_status_response {
		uint8_t byte_count;
		uint8_t pch_data_status[5];
	} __packed get_pch_data_status;
};

enum cmd_sts_t {
	/** Command has not been started */
	CMD_BUSY = 0,
	/** Command has completed */
	CMD_COMPLETE = 1,
	/** Command has been started but has not completed */
	CMD_DEFERRED = 2,
	/** Command completed with error. Send GET_ERROR_STATUS for details */
	CMD_ERROR = 3,
};

struct ping_status {
	/** Command status */
	uint8_t cmd_sts : 2;
	/** Length of data read to read */
	uint8_t data_len : 6;
};

/** @brief Emulated properties */
struct rts5453p_emul_pdc_data {
	struct gpio_dt_spec irq_gpios;
	uint16_t ucsi_version;
	union vendor_cmd vnd_command;
	union connector_reset_t reset;
	union pd_status_t notification_data[2];
	struct rts54_ic_status ic_status;
	struct capability_t capability;
	union connector_capability_t connector_capability;
	union connector_status_t connector_status;
	union uor_t uor;
	union pdr_t pdr;
	union error_status_t error;
	uint32_t rdo;
	union tpc_rp_t tpc_rp;
	union csd_op_mode_t csd_op_mode;
	union port_and_ccom_t set_ccom_mode;
	struct force_set_power_switch_t set_power_switch_data;
	uint8_t set_tpc_reconnect_param;
	struct pdc_info_t info;
	struct lpm_ppm_info_t lpm_ppm_info;
	union cable_property_t cable_property;

	union rts54_request request;

	bool read_ping;
	union {
		struct ping_status ping_status;
		uint8_t ping_raw_value;
	};
	uint8_t read_offset;
	union rts54_response response;

	uint16_t delay_ms;
	struct k_work_delayable delay_work;

	uint32_t snk_pdos[PDO_OFFSET_MAX];
	uint32_t src_pdos[PDO_OFFSET_MAX];
};

/**
 * @brief Returns pointer to i2c_common_emul_data for argument emul
 *
 * @param emul Pointer to rts5453p emulator
 * @return Pointer to i2c_common_emul_data from argument emul
 */
struct i2c_common_emul_data *
rts5453p_emul_get_i2c_common_data(const struct emul *emul);

#endif /* __EMUL_REALTEK_RTS5453P_H */

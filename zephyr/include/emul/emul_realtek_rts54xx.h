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

#include "emul/emul_common_i2c.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

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
		union set_notification_data {
			uint32_t raw;
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
		} data;
	} set_notification_enable;

	struct ppm_reset_req {
		struct rts54_subcommand_header header;
		uint8_t port_num;
	} ppm_reset;
};

union rts54xx_response {
	uint8_t raw_data[0];
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
		uint8_t reserved6[6];
	} ic_status;
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
	union vendor_cmd vnd_command;
	union set_notification_data notification_data[2];
	struct rts54_ic_status ic_status;
	union rts54_request request;

	bool read_ping;
	union {
		struct ping_status ping_status;
		uint8_t ping_raw_value;
	};
	union rts54xx_response response;
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

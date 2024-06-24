/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SUBSYS_UCSI_INCLUDE_PPM_H_
#define ZEPHYR_SUBSYS_UCSI_INCLUDE_PPM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

/* Steady-state PPM states.
 *
 * Use to keep track of states that won't immediately be updated synchronously
 * but may persist waiting for some communication with either the OPM or LPM.
 */
enum ppm_states {
	/* Only handle PPM_RESET or async event for PPM reset.
	 * This is the default state before we are ready to handle any OPM
	 * commands.
	 */
	PPM_STATE_NOT_READY,

	/* Only accept Set Notification Enable. Everything else no-ops. */
	PPM_STATE_IDLE,

	/* Handle most commands. */
	PPM_STATE_IDLE_NOTIFY,

	/* Unused state. */
	/* PPM_STATE_BUSY, */

	/* PPM_STATE_PROCESS_COMMAND is a hidden state that happens
	 * synchronously.
	 */

	/* Processing current command. */
	PPM_STATE_PROCESSING_COMMAND,

	/* Waiting for command complete acknowledgment from OPM. */
	PPM_STATE_WAITING_CC_ACK,
	/* PPM_STATE_PROCESS_CC_ACK, */

	/* Waiting for async event acknowledgment from OPM. */
	PPM_STATE_WAITING_ASYNC_EV_ACK,
	/* PPM_STATE_PROCESS_ASYNC_EV_ACK, */

	/* PPM_STATE_CANCELLING_COMMAND, */

	/* Just for bounds checking. */
	PPM_STATE_MAX,
};

/* Indicators of pending data states in the PPM. */
struct ppm_pending_data {
	/* Async events are received from the LPM. */
	uint16_t async_event : 1;

	/* Command is pending from OPM. */
	uint16_t command : 1;
};

/* Constants for UCSI commands (up to date for UCSI 3.0). */
enum ucsi_commands {
	UCSI_CMD_RESERVED = 0,
	UCSI_CMD_PPM_RESET = 0x01,
	UCSI_CMD_CANCEL = 0x02,
	UCSI_CMD_CONNECTOR_RESET = 0x03,
	UCSI_CMD_ACK_CC_CI = 0x04,
	UCSI_CMD_SET_NOTIFICATION_ENABLE = 0x05,
	UCSI_CMD_GET_CAPABILITY = 0x06,
	UCSI_CMD_GET_CONNECTOR_CAPABILITY = 0x07,
	UCSI_CMD_SET_CCOM = 0x08,
	UCSI_CMD_SET_UOR = 0x09,
	obsolete_UCSI_CMD_SET_PDM = 0x0A,
	UCSI_CMD_SET_PDR = 0x0B,
	UCSI_CMD_GET_ALTERNATE_MODES = 0x0C,
	UCSI_CMD_GET_CAM_SUPPORTED = 0x0D,
	UCSI_CMD_GET_CURRENT_CAM = 0x0E,
	UCSI_CMD_SET_NEW_CAM = 0x0F,
	UCSI_CMD_GET_PDOS = 0x10,
	UCSI_CMD_GET_CABLE_PROPERTY = 0x11,
	UCSI_CMD_GET_CONNECTOR_STATUS = 0x12,
	UCSI_CMD_GET_ERROR_STATUS = 0x13,
	UCSI_CMD_SET_POWER_LEVEL = 0x14,
	UCSI_CMD_GET_PD_MESSAGE = 0x15,
	UCSI_CMD_GET_ATTENTION_VDO = 0x16,
	UCSI_CMD_reserved_0x17 = 0x17,
	UCSI_CMD_GET_CAM_CS = 0x18,
	UCSI_CMD_LPM_FW_UPDATE_REQUEST = 0x19,
	UCSI_CMD_SECURITY_REQUEST = 0x1A,
	UCSI_CMD_SET_RETIMER_MODE = 0x1B,
	UCSI_CMD_SET_SINK_PATH = 0x1C,
	UCSI_CMD_SET_PDOS = 0x1D,
	UCSI_CMD_READ_POWER_LEVEL = 0x1E,
	UCSI_CMD_CHUNKING_SUPPORT = 0x1F,
	UCSI_CMD_VENDOR_CMD = 0x20,
	UCSI_CMD_MAX,
};

/* Byte offsets to UCSI data */
#define UCSI_VERSION_OFFSET 0
#define UCSI_CCI_OFFSET 4
#define UCSI_CONTROL_OFFSET 8
#define UCSI_MESSAGE_IN_OFFSET 16
#define UCSI_MESSAGE_OUT_OFFSET 272

/* Message sizes in UCSI data structure */
#define MESSAGE_IN_SIZE 255
#define MESSAGE_OUT_SIZE 255

/* UCSI version struct */
struct ucsi_version {
	uint16_t version;
	uint8_t lpm_address;
	uint8_t unused0;
} __attribute__((__packed__));

/* UCSI Connector Change Indication data structure */
struct ucsi_cci {
	uint32_t end_of_message : 1;
	uint32_t connector_changed : 7;

	uint32_t data_length : 8;

	uint32_t vendor_defined_message : 1;
	uint32_t reserved_0 : 6;
	uint32_t security_request : 1;

	uint32_t fw_update_request : 1;
	uint32_t not_supported : 1;
	uint32_t cancel_completed : 1;
	uint32_t reset_completed : 1;
	uint32_t busy : 1;
	uint32_t ack_command : 1;
	uint32_t error : 1;
	uint32_t cmd_complete : 1;
} __attribute__((__packed__));

/* UCSI Control Data structure */
struct ucsi_control {
	uint8_t command;
	uint8_t data_length;
	uint8_t command_specific[6];
} __attribute__((__packed__));

/* Overall memory layout for OPM to PPM communication. */
struct ucsi_memory_region {
	struct ucsi_version version;
	struct ucsi_cci cci;
	struct ucsi_control control;
	/* TODO - Message sizes depends on chunking support. */
	/* May not need to be full 256. */
	uint8_t message_in[MESSAGE_IN_SIZE]; /* PPM output */
	uint8_t reserved_0;
	uint8_t message_out[MESSAGE_OUT_SIZE]; /* PPM input */
	uint8_t reserved_1;
} __attribute__((__packed__));

/* Commands and data below */

/* ACK_CCI_CI Command */
struct ucsiv3_ack_cc_ci_cmd {
	unsigned connector_change_ack : 1;
	unsigned command_complete_ack : 1;

	/* 46-bits reserved */
	unsigned reserved_0 : 32;
	unsigned reserved_1 : 14;
} __attribute__((__packed__));

/* GET_PD_MESSAGE Command */
struct ucsiv3_get_pd_message_cmd {
	unsigned connector_number : 7;
	unsigned recipient : 3;
	unsigned message_offset : 8;
	unsigned number_of_bytes : 8;
	unsigned response_message_type : 6;

	/* 16-bits reserved */
	unsigned reserved_0 : 16;
} __attribute__((__packed__));

struct ucsiv3_set_new_cam_cmd {
	unsigned connector_number : 7;
	unsigned enter_or_exit : 1;
	unsigned new_cam : 8;
	unsigned am_specific : 32;
} __attribute__((__packed__));

/* GET_CAPABILITY data */
struct ucsiv3_get_capability_data {
	uint32_t attributes;
	uint8_t num_connectors : 7;
	uint8_t reserved_0 : 1;

	uint32_t optional_features : 24;
	uint32_t num_alt_modes : 8;

	uint8_t reserved_1;
	uint16_t bcd_bc_version;
	uint16_t bcd_pd_version;
	uint16_t bcd_typec_version;
} __attribute__((__packed__));

/* GET_CONNECTOR_STATUS data */
struct ucsiv3_get_connector_status_data {
	unsigned connector_status_change : 16;
	unsigned power_operation_mode : 3;
	unsigned connect_status : 1;
	unsigned power_direction : 1;
	unsigned connector_partner_flags : 8;
	unsigned connector_partner_type : 3;
	unsigned request_data_object : 32;
	unsigned bc_capability_status : 2;
	unsigned provider_caps_limited_reason : 4;
	unsigned bcd_pd_version_op_mode : 16;
	unsigned orientation : 1;
	unsigned sink_path_status : 1;
	unsigned reverse_current_protect_status : 1;
	unsigned power_reading : 1;
	unsigned current_scale : 3;
	unsigned peak_current : 16;
	unsigned avg_current : 16;
	unsigned volt_scale : 4;
	unsigned volt_reading : 16;

	/* 110-bits reserved (32 + 32 + 32 + 14) */
	unsigned reserved_0 : 32;
	unsigned reserved_1 : 32;
	unsigned reserved_2 : 32;
	unsigned reserved_3 : 14;
} __attribute__((__packed__));

struct ucsiv3_get_error_status_data {
	struct error_information {
		uint16_t unrecognized_command : 1;
		uint16_t nonexistent_connector_number : 1;
		uint16_t invalid_cmd_specific_params : 1;
		uint16_t incompatible_connector_partner : 1;
		uint16_t cc_comm_error : 1;
		uint16_t cmd_unsuccessful_dead_battery : 1;
		uint16_t contract_negotiation_failure : 1;
		uint16_t overcurrent : 1;
		uint16_t undefined : 1;
		uint16_t port_partner_reject_swap : 1;
		uint16_t hard_reset : 1;
		uint16_t ppm_policy_conflict : 1;
		uint16_t swap_rejected : 1;
		uint16_t reverse_current_protection : 1;
		uint16_t set_sink_path_rejected : 1;
		uint16_t reserved_0 : 1;
	} error_information;
	uint16_t vendor_defined;
} __attribute__((__packed__));

/* Forward declarations. */
struct ucsi_ppm_device;
struct ucsi_ppm_driver;

/**
 * Wait for the PPM to be initialized and ready for use.
 *
 * @param device: Data for PPM implementation.
 * @param num_ports: Number of ports to initialize for this PPM.
 *
 * @return 0 on success and -1 on error.
 */
typedef int(ucsi_ppm_init_and_wait)(struct ucsi_ppm_device *device,
				    uint8_t num_ports);

/**
 * Get the next connector status if a connector change indication is
 * currently active.
 *
 * @param device: Data for PPM implementation.
 * @param out_port_num: Port number for active connector change indication.
 * @param out_connector_status: Next active connector status.
 *
 * @return True if we have pending connector change indications.
 */
typedef bool(ucsi_ppm_get_next_connector_status)(
	struct ucsi_ppm_device *device, uint8_t *out_port_num,
	struct ucsiv3_get_connector_status_data **out_connector_status);

/**
 * Read data from UCSI at a specific data offset.
 *
 * @param device: Data for PPM implementation.
 * @param offset: Memory offset in OPM/PPM data structures.
 * @param buf: Buffer to read into.
 * @param length: Length of data to read.
 *
 * @return Bytes read or -1 for errors.
 */
typedef int(ucsi_ppm_read)(struct ucsi_ppm_device *device, unsigned int offset,
			   void *buf, size_t length);

/**
 * Write data for UCSI to a specific data offset.
 *
 * @param device: Data for PPM implementation.
 * @param offset: Memory offset in OPM/PPM data structures.
 * @param buf: Buffer to write from.
 * @param length: Length of data to write.
 *
 * @return Bytes written or -1 for errors.
 */
typedef int(ucsi_ppm_write)(struct ucsi_ppm_device *device, unsigned int offset,
			    const void *buf, size_t length);

/**
 * Function to send OPM a notification (doorbell).
 *
 * @param context: Context data for the OPM notifier.
 */
typedef void(ucsi_ppm_notify)(void *context);

/**
 * Register a notification callback with the driver. If there is already an
 * existing callback, this will replace it.
 *
 * @param device: Data for PPM implementation.
 * @param callback: Function to call to notify OPM.
 * @param context: Context data to pass back to callback.
 *
 * @return 0 if new callback set or 1 if callback replaced.
 */
typedef int(ucsi_ppm_register_notify)(struct ucsi_ppm_device *device,
				      ucsi_ppm_notify *callback, void *context);

/**
 * Alert the PPM that an LPM has sent a notification.
 *
 * @param device: Data for PPM implementation.
 * @param port_id: Port on which the change was made.
 */
typedef void(ucsi_ppm_lpm_alert)(struct ucsi_ppm_device *device,
				 uint8_t port_id);

/**
 * Clean up the given PPM driver. Call before freeing.
 *
 * @param driver: Driver object to clean up.
 */
typedef void(ucsi_ppm_cleanup)(struct ucsi_ppm_driver *driver);

/* TODO(b/339702957) - Remove ppm driver and replace with direct calls */
struct ucsi_ppm_driver {
	struct ucsi_ppm_device *dev;

	ucsi_ppm_init_and_wait *init_and_wait;
	ucsi_ppm_get_next_connector_status *get_next_connector_status;
	ucsi_ppm_read *read;
	ucsi_ppm_write *write;
	ucsi_ppm_register_notify *register_notify;
	ucsi_ppm_lpm_alert *lpm_alert;

	ucsi_ppm_cleanup *cleanup;
};

/**
 * Configure LPM IRQ for this PD driver.
 *
 * Initialize and configure LPM IRQ handling for this PD driver. Interrupts that
 * occur before the PPM is initialized will be dropped (as there is nothing to
 * do with them).
 *
 * @return 0 if IRQ is configured (or already configured). -1 on error.
 */
typedef int(ucsi_pd_configure_lpm_irq)(const struct device *dev);

/**
 * Initialize the PPM associated with this PD driver.
 *
 * This will block until the PPM is ready to be used. Call this after
 * registering OPM and LPM mechanisms.
 *
 * @param dev: Device object for this PD controller.
 *
 * @return 0 on success, -1 on error.
 */
typedef int(ucsi_pd_init_ppm)(const struct device *dev);

/**
 * Grab a pointer to the PPM.
 *
 * @param dev: Device object for this PD controller.
 */
typedef struct ucsi_ppm_driver *(ucsi_pd_get_ppm)(const struct device *dev);

/**
 * Execute a command in the PPM.
 *
 * While the PPM handles the overall OPM<->PPM interaction, this method is
 * called by the PPM in order to actually send the command to the LPM and handle
 * the response. This method should not modify the CCI and let the PPM
 * implementation do so instead.
 *
 * @param dev: Device object for this PD controller.
 * @param control: Command to execute.
 * @param lpm_data_out: Data returned from LPM.
 *
 * @returns -1 on error or the number of bytes read on success.
 */
typedef int(ucsi_pd_execute_command)(const struct device *dev,
				     struct ucsi_control *control,
				     uint8_t *lpm_data_out);

/**
 * Get the number of ports supported by this PD device.
 *
 * @param dev: Device object for this PD controller.
 *
 * @returns -1 on error or the number of active ports.
 */
typedef int(ucsi_pd_get_active_port_count)(const struct device *dev);

/**
 * Clean up the given PD driver. Call before freeing.
 *
 * @param driver: Driver object to clean up.
 */
typedef void(ucsi_pd_cleanup)(const struct device *dev);

/**
 * General driver for PD controllers.
 *
 * When constructing, must be provided a PPM implementation.
 */
struct ucsi_pd_driver {
	ucsi_pd_configure_lpm_irq *configure_lpm_irq;
	ucsi_pd_init_ppm *init_ppm;
	ucsi_pd_get_ppm *get_ppm;
	ucsi_pd_execute_command *execute_cmd;
	ucsi_pd_get_active_port_count *get_active_port_count;

	ucsi_pd_cleanup *cleanup;
};
#endif /* ZEPHYR_SUBSYS_UCSI_INCLUDE_PPM_H_ */

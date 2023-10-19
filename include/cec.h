/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "task.h"

#ifdef CONFIG_ZEPHYR
#include <zephyr_cec.h>
#endif

/* Size of the buffer inside the rx queue */
#define CEC_RX_BUFFER_SIZE 20
#if CEC_RX_BUFFER_SIZE < MAX_CEC_MSG_LEN + 1
#error "Buffer must fit at least a CEC message and a length byte"
#endif
#if CEC_RX_BUFFER_SIZE > 255
#error "Buffer size must not exceed 255 since offsets are uint8_t"
#endif

/* Notification from interrupt to CEC task that data has been received */
#define CEC_TASK_EVENT_RECEIVED_DATA TASK_EVENT_CUSTOM_BIT(0)
#define CEC_TASK_EVENT_OKAY TASK_EVENT_CUSTOM_BIT(1)
#define CEC_TASK_EVENT_FAILED TASK_EVENT_CUSTOM_BIT(2)

/*
 * CEC broadcast address (when used as destination). Also the highest possible
 * CEC address.
 */
#define CEC_BROADCAST_ADDR 15

/*
 * Unregistered logical address (when used as initiator). Used when a device has
 * no valid physical address (e.g. it's unplugged), or as a fallback when no
 * type-specific logical addresses are available.
 */
#define CEC_UNREGISTERED_ADDR 15

/* Address to indicate that no logical address has been set. */
#define CEC_INVALID_ADDR 255

/*
 * The CEC specification requires at least one and a maximum of
 * five resends attempts
 */
#define CEC_MAX_RESENDS 5

/* Bit timing */
#define CEC_NOMINAL_BIT_PERIOD_US 2400
#define CEC_NOMINAL_SAMPLE_TIME_US 1050
#define CEC_START_BIT_LOW_US 3700
#define CEC_START_BIT_HIGH_US 800
#define CEC_DATA_ZERO_LOW_US 1500
#define CEC_DATA_ZERO_HIGH_US 900
#define CEC_DATA_ONE_LOW_US 600
#define CEC_DATA_ONE_HIGH_US 1800

/* Free time timing */
/* Resend */
#define CEC_FREE_TIME_RS_US (3 * (CEC_NOMINAL_BIT_PERIOD_US))
/* New initiator */
#define CEC_FREE_TIME_NI_US (5 * (CEC_NOMINAL_BIT_PERIOD_US))
/* Present initiator */
#define CEC_FREE_TIME_PI_US (7 * (CEC_NOMINAL_BIT_PERIOD_US))

/* All return EC_SUCCESS if successful, non-zero if error. */
struct cec_drv {
	/* Initialise the CEC port */
	int (*init)(int port);

	/*
	 * Get/set enable state.
	 * enable = 0 means disabled, enable = 1 means enabled.
	 */
	int (*get_enable)(int port, uint8_t *enable);
	int (*set_enable)(int port, uint8_t enable);

	/* Get/set the logical address */
	int (*get_logical_addr)(int port, uint8_t *logical_addr);
	int (*set_logical_addr)(int port, uint8_t logical_addr);

	/* Send a CEC message */
	int (*send)(int port, const uint8_t *msg, uint8_t len);

	/*
	 * Get the received message. This should be called after the driver sets
	 * CEC_TASK_EVENT_RECEIVED_DATA to indicate data is ready.
	 */
	int (*get_received_message)(int port, uint8_t **msg, uint8_t *len);
};

/* CEC message during transfer */
struct cec_msg_transfer {
	/* Bit offset  */
	uint8_t bit;
	/* Byte offset */
	uint8_t byte;
	/* The CEC message */
	uint8_t buf[MAX_CEC_MSG_LEN];
};

/*
 * Queue of completed incoming CEC messages
 * ready to be read out by AP
 */
struct cec_rx_queue {
	/*
	 * Write offset. Updated from interrupt context when we
	 * have received a complete message.
	 */
	uint8_t write_offset;
	/* Read offset. Updated when AP sends CEC read command */
	uint8_t read_offset;
	/* Data buffer */
	uint8_t buf[CEC_RX_BUFFER_SIZE];
};

struct cec_header {
	uint8_t initiator : 4;
	uint8_t desitination : 4;
} __packed;

/* CEC commands */
#define CEC_MSG_IMAGE_VIEW_ON 0x04
#define CEC_MSG_TEXT_VIEW_ON 0x0d
#define CEC_MSG_REPORT_PHYSICAL_ADDRESS 0x84
#define CEC_MSG_DEVICE_VENDOR_ID 0x87
#define CEC_MSG_REQUEST_ACTIVE_SOURCE 0x85
#define CEC_MSG_SET_STREAM_PATH 0x86

enum cec_action {
	CEC_ACTION_NONE = 0,
	CEC_ACTION_POWER_BUTTON,
} __packed;

/**
 * Defines what actions to take for commands received from external devices
 * when the AP is off.
 */
struct cec_offline_policy {
	/* CEC command to act on */
	uint8_t command;
	/* Action taken when <command> is received */
	enum cec_action action;
};

/**
 * CEC configuration.
 */
struct cec_config_t {
	const struct cec_drv *drv;

	/* Optional driver-specific configuration data */
	const void *drv_config;

	/*
	 * Actions taken on message received when the system is off.
	 * Last entry must be null terminated.
	 */
	struct cec_offline_policy *offline_policy;
};

/* CEC config definition. */
extern test_overridable_const struct cec_config_t cec_config[];

/**
 * Default policy provided for convenience.
 */
extern struct cec_offline_policy cec_default_policy[];

/**
 * Get the current bit of a CEC message transfer
 *
 * @param queue		Queue to flush
 */
int cec_transfer_get_bit(const struct cec_msg_transfer *transfer);

/**
 * Set the current bit of a CEC message transfer
 *
 * @param transfer	Message transfer to set current bit of
 * @param val		New bit value
 */
void cec_transfer_set_bit(struct cec_msg_transfer *transfer, int val);

/**
 * Make the current bit the next bit in the transfer buffer
 *
 * @param transfer	Message transfer to change current bit of
 */
void cec_transfer_inc_bit(struct cec_msg_transfer *transfer);

/**
 * Check if current bit is an end-of-message bit and if it is set
 *
 * @param transfer	Message transfer to check for end-of-message
 */
int cec_transfer_is_eom(const struct cec_msg_transfer *transfer, int len);

/**
 * Flush all messages from a CEC receive queue
 *
 * @param queue		Queue to flush
 */
void cec_rx_queue_flush(struct cec_rx_queue *queue);

/**
 * Push a CEC message to a CEC receive queue
 *
 * @param queue		Queue to add message to
 */
int cec_rx_queue_push(struct cec_rx_queue *queue, const uint8_t *msg,
		      uint8_t msg_len);

/**
 * Pop a CEC message from a CEC receive queue
 *
 * @param queue		Queue to retrieve message from
 * @param msg		Buffer to store retrieved message in
 * @param msg_len	Number of data bytes in msg
 */
int cec_rx_queue_pop(struct cec_rx_queue *queue, uint8_t *msg,
		     uint8_t *msg_len);

/**
 * Process a CEC message when the AP is off.
 *
 * @param port		CEC port the message was received on
 * @param msg		Buffer containing the message to process
 * @param msg_len	Number of data bytes in msg
 * @return EC_SUCCESS if the message is consumed or EC_ERROR_* otherwise.
 */
int cec_process_offline_message(int port, const uint8_t *msg, uint8_t msg_len);

/**
 * Set a given task event for a given CEC port.
 *
 * @param port		Port the event occurred on
 * @param event		Event type (CEC_TASK_EVENT_*)
 */
void cec_task_set_event(int port, uint32_t event);

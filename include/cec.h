/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"

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

/* CEC broadcast address. Also the highest possible CEC address */
#define CEC_BROADCAST_ADDR 15

/* Address to indicate that no logical address has been set */
#define CEC_UNREGISTERED_ADDR 255

/*
 * The CEC specification requires at least one and a maximum of
 * five resends attempts
 */
#define CEC_MAX_RESENDS 5

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

extern const struct cec_drv bitbang_cec_drv;

/* Edge to trigger capture timer interrupt on */
enum cec_cap_edge {
	CEC_CAP_EDGE_NONE,
	CEC_CAP_EDGE_FALLING,
	CEC_CAP_EDGE_RISING
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
	/*
	 * Actions taken on message received when the system is off.
	 * Last entry must be null terminated.
	 */
	struct cec_offline_policy *offline_policy;
};

/* CEC config definition. */
extern const struct cec_config_t cec_config[];

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
 * @param queue		Queue to retrieve message from
 * @param msg		Buffer to store retrieved message in
 * @param msg_len	Number of data bytes in msg
 * @return EC_SUCCESS if the message is consumed or EC_ERROR_* otherwise.
 */
int cec_process_offline_message(struct cec_rx_queue *queue, const uint8_t *msg,
				uint8_t msg_len);

/**
 * Start the capture timer. An interrupt will be triggered when either a capture
 * edge or a timeout occurs.
 * If edge is NONE, disable the capture interrupt and wait for a timeout only.
 * If timeout is 0, disable the timeout interrupt and wait for a capture event
 * only.
 *
 * @param edge		Edge to trigger on
 * @param timeout	Timeout to program into the capture timer
 */
void cec_tmr_cap_start(enum cec_cap_edge edge, int timeout);

/**
 * Stop the capture timer.
 */
void cec_tmr_cap_stop(void);

/**
 * Return the time measured by the capture timer.
 */
int cec_tmr_cap_get(void);

/**
 * ITE-specific callback to record the interrupt time.
 */
__override_proto void cec_update_interrupt_time(void);

/**
 * Called when a transfer is initiated from the host. Should trigger an
 * interrupt which then calls cec_event_tx(). It must be called from interrupt
 * context since the CEC state machine relies on the fact that the state is only
 * modified from interrupt context for synchronisation.
 */
void cec_trigger_send(void);

/**
 * Enable timers used for CEC.
 */
void cec_enable_timer(void);

/**
 * Disable timers used for CEC.
 */
void cec_disable_timer(void);

/**
 * Initialise timers used for CEC.
 */
void cec_init_timer(void);

/**
 * Event for timeout.
 */
void cec_event_timeout(void);

/**
 * Event for capture edge.
 */
void cec_event_cap(void);

/**
 * Event for transfer from host.
 */
void cec_event_tx(void);

/**
 * Interrupt handler for rising and falling edges on the CEC line.
 */
void cec_gpio_interrupt(enum gpio_signal signal);

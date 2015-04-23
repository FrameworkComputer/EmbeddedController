/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */

#define TCPC_ALERT0              0
#define TCPC_ALERT0_I2C_WAKE     (1<<7)
#define TCPC_ALERT0_POWER_STATUS (1<<6)
#define TCPC_ALERT0_CC_STATUS    (1<<5)
#define TCPC_ALERT0_RX_STATUS    (1<<4)
#define TCPC_ALERT0_RX_HARD_RST  (1<<3)
#define TCPC_ALERT0_TX_SUCCESS   (1<<2)
#define TCPC_ALERT0_TX_DISCARDED (1<<1)
#define TCPC_ALERT0_TX_FAILED    (1<<0)
#define TCPC_ALERT0_TX_COMPLETE  (TCPC_ALERT0_TX_SUCCESS | \
				  TCPC_ALERT0_TX_FAILED | \
				  TCPC_ALERT0_TX_FAILED)

#define TCPC_ALERT1              1
#define TCPC_ALERT1_GPIO_CHANGE  (1<<3)

/* Default retry count for transmitting */
#define PD_RETRY_COUNT 3

/* Time to wait for TCPC to complete transmit */
#define PD_T_TCPC_TX_TIMEOUT  (100*MSEC)

/**
 * TCPC is asserting alert
 */
void tcpc_alert(void);

/**
 * Initialize TCPC.
 *
 * @param port Type-C port number
 */
void tcpc_init(int port);

/**
 * Run TCPC task once. This checks for incoming messages, processes
 * any outgoing messages, and reads CC lines.
 *
 * @param port Type-C port number
 * @param evt Event type that woke up this task
 */
int tcpc_run(int port, int evt);

/**
 * Read TCPC alert status
 *
 * @param port Type-C port number
 * @param alert_reg Alert register to read
 */
int tcpm_alert_status(int port, int alert_reg);


/**
 * Read the CC line status.
 *
 * @param port Type-C port number
 * @param polarity Polarity of the CC line to read
 *
 * @return CC status from enum tcpc_cc_status
 */
enum tcpc_cc_status {
/* CC status when we are a source (we expose Rp) */
	TYPEC_CC_SRC_RA,
	TYPEC_CC_SRC_RD,
	TYPEC_CC_SRC_OPEN,
/* CC status when we are a sink (we expose Rd) */
	TYPEC_CC_SNK_PWR_3_0,
	TYPEC_CC_SNK_PWR_1_5,
	TYPEC_CC_SNK_PWR_DEFAULT,
	TYPEC_CC_SNK_OPEN
};
int tcpm_get_cc(int port, int polarity);

/**
 * Set the CC pull resistor. This sets our role as either source or sink.
 *
 * @param port Type-C port number
 * @param pull One of enum tcpc_cc_pull
 */
enum tcpc_cc_pull {
	TYPEC_CC_RP,
	TYPEC_CC_RD,
	TYPEC_CC_OPEN
};
void tcpm_set_cc(int port, int pull);

/**
 * Set polarity
 *
 * @param port Type-C port number
 * @param polarity 0=> transmit on CC1, 1=> transmit on CC2
 */
void tcpm_set_polarity(int port, int polarity);

/**
 * Set Vconn.
 *
 * @param port Type-C port number
 * @param polarity Polarity of the CC line to read
 */
void tcpm_set_vconn(int port, int enable);

/**
 * Set PD message header to use for goodCRC
 *
 * @param port Type-C port number
 * @param power_role Power role to use in header
 * @param data_role Data role to use in header
 */
void tcpm_set_msg_header(int port, int power_role, int data_role);

/**
 * Read last received PD message.
 *
 * @param port Type-C port number
 * @param payload Pointer to location to copy payload of message
 *
 * @return header of message
 */
int tcpm_get_message(int port, uint32_t *payload);

/**
 * Transmit PD message
 *
 * @param port Type-C port number
 * @param type Transmit type
 * @param header Packet header
 * @param cnt Number of bytes in payload
 * @param data Payload
 */
enum tcpm_transmit_type {
	TRANSMIT_SOP,
	TRANSMIT_SOP_PRIME,
	TRANSMIT_SOP_PRIME_PRIME,
	TRANSMIT_SOP_DEBUG_PRIME,
	TRANSMIT_SOP_DEBUG_PRIME_PRIME,
	TRANSMIT_HARD_RESET,
	TRANSMIT_CABLE_RESET,
	TRANSMIT_BIST_MODE_2
};
void tcpm_transmit(int port, enum tcpm_transmit_type type, uint16_t header,
		   const uint32_t *data);


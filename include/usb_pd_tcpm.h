/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */

#ifndef __USB_PD_TCPM_H
#define __USB_PD_TCPM_H

/* Default retry count for transmitting */
#define PD_RETRY_COUNT 3

/* Time to wait for TCPC to complete transmit */
#define PD_T_TCPC_TX_TIMEOUT  (100*MSEC)

#define TCPC_REG_VENDOR_ID         0x0
#define TCPC_REG_PRODUCT_ID        0x2
#define TCPC_REG_BCD_DEV           0x4
#define TCPC_REG_TC_REV            0x6
#define TCPC_REG_PD_REV            0x8
#define TCPC_REG_PD_INT_REV        0xa
#define TCPC_REG_DEV_CAP_1         0xc
#define TCPC_REG_DEV_CAP_2         0xd
#define TCPC_REG_DEV_CAP_3         0xe
#define TCPC_REG_DEV_CAP_4         0xf
#define TCPC_REG_ALERT1            0x10
#define TCPC_REG_ALERT1_SLEEP_EXITED (1<<7)
#define TCPC_REG_ALERT1_POWER_STATUS (1<<6)
#define TCPC_REG_ALERT1_CC_STATUS    (1<<5)
#define TCPC_REG_ALERT1_RX_STATUS    (1<<4)
#define TCPC_REG_ALERT1_RX_HARD_RST  (1<<3)
#define TCPC_REG_ALERT1_TX_SUCCESS   (1<<2)
#define TCPC_REG_ALERT1_TX_DISCARDED (1<<1)
#define TCPC_REG_ALERT1_TX_FAILED    (1<<0)
#define TCPC_REG_ALERT1_TX_COMPLETE  (TCPC_REG_ALERT1_TX_SUCCESS | \
				      TCPC_REG_ALERT1_TX_DISCARDED | \
				      TCPC_REG_ALERT1_TX_FAILED)

#define TCPC_REG_ALERT2            0x11
#define TCPC_REG_ALERT3            0x12
#define TCPC_REG_ALERT4            0x13
#define TCPC_REG_ALERT_MASK_1      0x14
#define TCPC_REG_ALERT_MASK_2      0x15
#define TCPC_REG_POWER_STATUS_MASK 0x16
#define TCPC_REG_CC1_STATUS        0x18
#define TCPC_REG_CC2_STATUS        0x19
#define TCPC_REG_CC_STATUS_SET(term, volt) \
		((term) << 3 | volt)
#define TCPC_REG_CC_STATUS_TERM(reg) (((reg) & 0x38) >> 3)
#define TCPC_REG_CC_STATUS_VOLT(reg) ((reg) & 0x7)
enum tcpc_cc_termination_status {
	TYPEC_CC_TERM_RA = 0,
	TYPEC_CC_TERM_RP_DEF = 1,
	TYPEC_CC_TERM_RP_1_5 = 2,
	TYPEC_CC_TERM_RP_3_0 = 3,
	TYPEC_CC_TERM_RD = 4,
	TYPEC_CC_TERM_VCONN = 5,
	TYPEC_CC_TERM_OPEN = 6
};
enum tcpc_cc_voltage_status {
	TYPEC_CC_VOLT_RA = 0,
	TYPEC_CC_VOLT_SNK_DEF = 1,
	TYPEC_CC_VOLT_SNK_1_5 = 2,
	TYPEC_CC_VOLT_SNK_3_0 = 3,
	TYPEC_CC_VOLT_SRC_DEF = 4,
	TYPEC_CC_VOLT_SRC_1_5 = 5,
	TYPEC_CC_VOLT_SRC_3_0 = 6,
	TYPEC_CC_VOLT_OPEN = 7
};
/* Check if CC voltage is within Rd */
#define TYPEC_CC_IS_RD(cc) ((cc) >= TYPEC_CC_VOLT_SNK_DEF && \
			    (cc) <= TYPEC_CC_VOLT_SNK_3_0)

#define TCPC_REG_POWER_STATUS      0x1a
#define TCPC_REG_ROLE_CTRL         0x1b
#define TCPC_REG_ROLE_CTRL_SET(drp, rp, cc2, cc1) \
		((drp) << 6 | (rp) << 4 | (cc2) << 2 | (cc1))
#define TCPC_REG_ROLE_CTRL_CC2(reg) (((reg) & 0xc) >> 2)
#define TCPC_REG_ROLE_CTRL_CC1(reg) ((reg) & 0x3)
enum tcpc_cc_pull {
	TYPEC_CC_RA = 0,
	TYPEC_CC_RP = 1,
	TYPEC_CC_RD = 2,
	TYPEC_CC_OPEN = 3,
};

#define TCPC_REG_POWER_PATH_CTRL   0x1c
#define TCPC_REG_POWER_CTRL        0x1d
#define TCPC_REG_POWER_CTRL_SET(polarity, vconn) \
		((polarity) << 4 | (vconn))
#define TCPC_REG_POWER_CTRL_POLARITY(reg) (((reg) & 0x10) >> 4)
#define TCPC_REG_POWER_CTRL_VCONN(reg)    ((reg) & 0x1)

#define TCPC_REG_COMMAND           0x23
#define TCPC_REG_MSG_HDR_INFO      0x2e
#define TCPC_REG_MSG_HDR_INFO_SET(drole, prole) \
		((drole) << 3 | (PD_REV20 << 1) | (prole))
#define TCPC_REG_MSG_HDR_INFO_DROLE(reg) (((reg) & 0x8) >> 3)
#define TCPC_REG_MSG_HDR_INFO_PROLE(reg) ((reg) & 0x1)

#define TCPC_REG_RX_BYTE_CNT       0x2f
#define TCPC_REG_RX_STATUS         0x30
#define TCPC_REG_RX_DETECT         0x31
#define TCPC_REG_RX_DETECT_SOP_HRST_MASK 0x21

#define TCPC_REG_RX_HDR            0x32
#define TCPC_REG_RX_DATA           0x34 /* through 0x4f */

#define TCPC_REG_TRANSMIT          0x50
#define TCPC_REG_TRANSMIT_SET(type) \
		(PD_RETRY_COUNT << 4 | (type))
#define TCPC_REG_TRANSMIT_RETRY(reg) (((reg) & 0x30) >> 4)
#define TCPC_REG_TRANSMIT_TYPE(reg)  ((reg) & 0x7)
enum tcpm_transmit_type {
	TRANSMIT_SOP = 0,
	TRANSMIT_SOP_PRIME = 1,
	TRANSMIT_SOP_PRIME_PRIME = 2,
	TRANSMIT_SOP_DEBUG_PRIME = 3,
	TRANSMIT_SOP_DEBUG_PRIME_PRIME = 4,
	TRANSMIT_HARD_RESET = 5,
	TRANSMIT_CABLE_RESET = 6,
	TRANSMIT_BIST_MODE_2 = 7
};

#define TCPC_REG_TX_BYTE_CNT       0x51
#define TCPC_REG_TX_HDR            0x52
#define TCPC_REG_TX_DATA           0x54 /* through 0x6f */

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
 * @param alert Pointer to location to store alert status
 *
 * @return EC_SUCCESS or error
 */
int tcpm_alert_status(int port, int alert_reg, uint8_t *alert);


/**
 * Read the CC line status.
 *
 * @param port Type-C port number
 * @param cc1 pointer to CC status for CC1
 * @param cc2 pointer to CC status for CC2
 *
 * @return EC_SUCCESS or error
 */
int tcpm_get_cc(int port, int *cc1, int *cc2);

/**
 * Set the CC pull resistor. This sets our role as either source or sink.
 *
 * @param port Type-C port number
 * @param pull One of enum tcpc_cc_pull
 *
 * @return EC_SUCCESS or error
 */
int tcpm_set_cc(int port, int pull);

/**
 * Set polarity
 *
 * @param port Type-C port number
 * @param polarity 0=> transmit on CC1, 1=> transmit on CC2
 *
 * @return EC_SUCCESS or error
 */
int tcpm_set_polarity(int port, int polarity);

/**
 * Set Vconn.
 *
 * @param port Type-C port number
 * @param polarity Polarity of the CC line to read
 *
 * @return EC_SUCCESS or error
 */
int tcpm_set_vconn(int port, int enable);

/**
 * Set PD message header to use for goodCRC
 *
 * @param port Type-C port number
 * @param power_role Power role to use in header
 * @param data_role Data role to use in header
 *
 * @return EC_SUCCESS or error
 */
int tcpm_set_msg_header(int port, int power_role, int data_role);

/**
 * Set RX enable flag
 *
 * @param port Type-C port number
 * @enable true for enable, false for disable
 *
 * @return EC_SUCCESS or error
 */
int tcpm_set_rx_enable(int port, int enable);

/**
 * Read last received PD message.
 *
 * @param port Type-C port number
 * @param payload Pointer to location to copy payload of message
 * @param header of message
 *
 * @return EC_SUCCESS or error
 */
int tcpm_get_message(int port, uint32_t *payload, int *head);

/**
 * Transmit PD message
 *
 * @param port Type-C port number
 * @param type Transmit type
 * @param header Packet header
 * @param cnt Number of bytes in payload
 * @param data Payload
 *
 * @return EC_SUCCESS or error
 */
int tcpm_transmit(int port, enum tcpm_transmit_type type, uint16_t header,
		   const uint32_t *data);

#endif /* __USB_PD_TCPM_H */

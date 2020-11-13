/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PD chip Crypress 5525 driver
 */

#ifndef __CROS_EC_CYPRESS5525_H
#define __CROS_EC_CYPRESS5525_H

/************************************************/
/*	REGISTER ADDRESS DEFINITION                 */
/************************************************/
#define CYP5525_DEVICE_MODE             0x0000
#define CYP5525_BOOT_MODE_REASON        0x0001
#define CYP5525_SILICON_ID              0x0002
#define CYP5525_INTR_REG                0x0006
#define CYP5525_RESET_REG               0x0008
#define CYP5525_READ_ALL_VERSION_REG    0x0010
#define CYP5525_FW2_VERSION_REG         0x0020
#define CYP5525_PDPORT_ENABLE_REG       0x002C
#define CYP5525_POWER_STAT              0x002E

#define CYP5525_UCSI_STATUS_REG         0x0038
#define CYP5525_UCSI_CONTROL_REG        0x0039
#define CYP5525_SYS_PWR_STATE           0x003B
#define CYP5525_RESPONSE_REG            0x007E
#define CYP5525_DATA_MEM_REG            0x1404
#define CYP5525_VERSION_REG             0xF000
#define CYP5525_CCI_REG                 0xF004
#define CYP5525_CONTROL_REG             0xF008
#define CYP5525_MESSAGE_IN_REG          0xF010
#define CYP5525_MESSAGE_OUT_REG         0xF020

#define CYP5525_SELECT_SINK_PDO_REG(x) \
	(0x1005 + (x * 0x1000))
#define CYP5525_PD_CONTROL_REG(x) \
	(0x1006 + (x * 0x1000))
#define CYP5525_PD_STATUS_REG(x) \
	(0x1008 + (x * 0x1000))
#define CYP5525_TYPE_C_STATUS_REG(x) \
	(0x100C + (x * 0x1000))
#define CYP5525_TYPE_C_VOLTAGE_REG(x) \
	(0x100D + (x * 0x1000))
#define CYP5525_CURRENT_PDO_REG(x) \
	(0x1010 + (x * 0x1000))
#define CYP5525_CURRENT_RDO_REG(x) \
	(0x1014 + (x * 0x1000))
#define CYP5525_EVENT_MASK_REG(x) \
	(0x1024 + (x * 0x1000))
#define CYP5525_DP_ALT_MODE_CONFIG_REG(x) \
	(0x102B + (x * 0x1000))
#define CYP5525_PORT_INTR_STATUS_REG(x) \
	(0x1034 + (x * 0x1000))

#define CYP5525_PORT_PD_RESPONSE_REG(x) \
	(0x1400 + (x * 0x1000))

#define CYP5525_SELECT_SINK_PDO_P1_REG      0x2005
#define CYP5525_PD_CONTROL_P1_REG           0x2006
#define CYP5525_PD_STATUS_P1_REG            0x2008
#define CYP5525_TYPE_C_STATUS_P1_REG        0x200C
#define CYP5525_CURRENT_PDO_P1_REG          0x2010
#define CYP5525_CURRENT_RDO_P1_REG          0x2014
#define CYP5525_EVENT_MASK_P1_REG           0x2024
#define CYP5525_DP_ALT_MODE_CONFIG_P1_REG   0x202B
#define CYP5525_PORT_INTR_STATUS_P1_REG     0x2034

/************************************************/
/*	DEVICE MODE DEFINITION                      */
/************************************************/
#define CYP5525_BOOT_MODE   0x00
#define CYP5525_FW1_MODE	0x01
#define CYP5525_FW2_MODE	0x02

/************************************************/
/*	DEVICE INTERRUPT DEFINITION                 */
/************************************************/
#define CYP5525_DEV_INTR	0x01
#define CYP5525_PORT0_INTR  0x02
#define CYP5525_PORT1_INTR  0x04
#define CYP5525_UCSI_INTR   0x80

/************************************************/
/*	PORT INTERRUPT DEFINITION                   */
/************************************************/
#define CYP5525_STATUS_TYPEC_ATTACH     0x00000001 /*bit 0 */
#define CYP5525_STATUS_TYPEC_DETACH     0x00000002 /*bit 1 */
#define CYP5525_STATUS_CONTRACT_DONE    0x00000004 /*bit 2 */
#define CYP5525_STATUS_PRSWAP_DONE      0x00000008 /*bit 3 */
#define CYP5525_STATUS_DRSWAP_DONE      0x00000010 /*bit 4 */
#define CYP5525_STATUS_VCONNSWAP_DONE   0x00000020 /*bit 5 */
#define CYP5525_STATUS_RESPONSE_READY   0x00200000 /*bit 21*/
#define CYP5525_STATUS_OVP_EVT          0x40000000 /*bit 30*/

/************************************************/
/*	PD PORT DEFINITION                          */
/************************************************/
#define CYP5525_PDPORT_DISABLE  0x00
#define CYP5525_PDPORT_ENABLE   0x01


/************************************************
 *	PD COMMAND DEFINITION
 * See 001-97863_0N_V.pdf from cypress for the HPI
 * Definition. Specifically Pages around 102
 * Chapter 4.3.3.6 PD_CONTROL register
 ************************************************/
enum cypd_pd_command {
	CYPD_PD_CMD_SET_TYPEC_DEFAULT = 0x00,
	CYPD_PD_CMD_SET_TYPEC_1_5A,
	CYPD_PD_CMD_SET_TYPEC_3A,
	CYPD_PD_CMD_TRG_DATA_ROLE_SWAP = 0x05,
	CYPD_PD_CMD_TRG_POWER_ROLE_SWAP = 0x06,
	CYPD_PD_CMD_VCONN_EN = 0x07,
	CYPD_PD_CMD_VCONN_DIS = 0x08,
	CYPD_PD_CMD_TRG_VCONN_SWAP = 0x09,
	CYPD_PD_CMD_EC_INIT_COMPLETE = 0x10,
	CYPD_PD_CMD_PORT_DISABLE = 0x11,
};

/************************************************
 *	RESPONSE DEFINITION
 * See 001-97863_0N_V.pdf from cypress for the HPI
 * Definition. Specifically Pages around 22
 * Chapter 4.1.1 HPI Interfaces response codes
 ************************************************/
enum cypd_response {
	CYPD_RESPONSE_NONE,
	CYPD_RESPONSE_SUCCESS = 0x02,
	CYPD_RESPONSE_FLASH_DATA_AVALIABLE = 0x03, /* RESPONSE register only */
	CYPD_RESPONSE_INVALID_COMMAND = 0x05,
	CYPD_RESPONSE_INVALID_STATE = 0x06,
	CYPD_RESPONSE_FLASH_UPDATE_FAILED = 0x07, /* RESPONSE register only */
	CYPD_RESPONSE_INVALID_FW = 0x08, /* RESPONSE register only */
	CYPD_RESPONSE_INVALID_ARGUMENTS = 0x09,
	CYPD_RESPONSE_NOT_SUPPORTED = 0x0A,
	CYPD_RESPONSE_TRANSACTION_FAILED = 0x0C,
	CYPD_RESPONSE_PD_COMMAND_FAILED = 0x0D,
	CYPD_RESPONSE_UNDEFINED_ERROR = 0x0F,
	CYPD_RESPONSE_READ_PDO_DATA = 0x10,
	CYPD_RESPONSE_CMD_ABORTED = 0x11,
	CYPD_RESPONSE_PORT_BUSY = 0x12,
	CYPD_RESPONSE_MINMAX_CURRENT = 0x13,
	CYPD_RESPONSE_EXT_SRC_CAP = 0x14,
	CYPD_RESPONSE_DID_RESPONSE = 0x18,
	CYPD_RESPONSE_SVID_RESPONSE = 0x19,
	CYPD_RESPONSE_DISCOVER_MODE_RESPONSE = 0x1A,
	CYPD_RESPONSE_CABLE_COMM_NOT_ALLOWED = 0x1B,
	CYPD_RESPONSE_EXT_SNK_CAP = 0x1C,
	CYPD_RESPONSE_FWCT_IDENT_INVALID = 0x40,
	CYPD_RESPONSE_FWCT_INVALID_GUID = 0x41,
	CYPD_RESPONSE_FWCT_INVALID_VERSION = 0x42,
	CYPD_RESPONSE_HPI_CMD_INVALID_SEQ = 0x43,
	CYPD_RESPONSE_FWCT_AUTH_FAILED = 0x44,
	CYPD_RESPONSE_HASH_FAILED = 0x45,
	/* Event and Asynchronous Message Codes */
	CYPD_RESPONSE_RESET_COMPLETE = 0x80,
	CYPD_RESPONSE_MESSAGE_QUEUE_OVERFLOW = 0x81,
	/* Type C Event and Asynchronous Message Codes */
	CYPD_RESPONSE_OVER_CURRENT = 0x82,
	CYPD_RESPONSE_OVER_VOLT = 0x83,
	CYPD_RESPONSE_PORT_CONNECT = 0x84,
	CYPD_RESPONSE_PORT_DISCONNECT = 0x85,
	CYPD_RESPONSE_PD_CONTRACT_NEGOTIATION_COMPLETE = 0x86,
	CYPD_RESPONSE_SWAP_COMPLETE = 0x87,
	CYPD_RESPONSE_PS_RDY_MSG_PENDING = 0x8A,
	CYPD_RESPONSE_GOTO_MIN_PENDING = 0x8B,
	CYPD_RESPONSE_ACCEPT_MSG_RX = 0x8C,
	CYPD_RESPONSE_REJECT_MSG_RX = 0x8D,
	CYPD_RESPONSE_WAIT_MSG_RX = 0x8E,
	CYPD_RESPONSE_HARD_RESET_RX = 0x8F,
	/* PD Data Message Specific Events */
	CYPD_RESPONSE_VDM_RX = 0x90,
	/* Capability Message Specific Events */
	CYPD_RESPONSE_SOURCE_CAP_MSG_RX = 0x91,
	CYPD_RESPONSE_SINK_CAP_MSG_RX = 0x92,
	/* USB4 Events */
	CYPD_RESPONSE_USB4_DATA_RESET_RX = 0x93,
	CYPD_RESPONSE_USB4_DATA_RESET_COMPLETE = 0x94,
	CYPD_RESPONSE_USB4_ENTRY_COMPLETE = 0x95,
	/* Resets and Errors */
	CYPD_RESPONSE_HARD_RESET_SENT = 0x9A,
	CYPD_RESPONSE_SOFT_RESET_SENT = 0x9B,
	CYPD_RESPONSE_CABLE_RESET_SENT = 0x9C,
	CYPD_RESPONSE_SOURCEDISABLED = 0x9D,
	CYPD_RESPONSE_SENDER_RESPONSE_TIMEOUT = 0x9E,
	CYPD_RESPONSE_NO_VDM_RESPONSE_RX = 0x9F,
	CYPD_RESPONSE_UNEXPECTED_VOLTAGE = 0xA0,
	CYPD_RESPONSE_TYPE_C_ERROR_RECOVERY = 0xA1,
	CYPD_RESPONSE_BATTERY_STATUS_RX = 0xA2,
	CYPD_RESPONSE_ALERT_RX = 0xA3,
	CYPD_RESPONSE_UNSUPPORTED_MSG_RX = 0xA4,
	CYPD_RESPONSE_EMCA_DETECTED = 0xA6,
	CYPD_RESPONSE_CABLE_DISCOVERY_FAILED = 0xA7,
	CYPD_RESPONSE_RP_CHANGE_DETECTED = 0xAA,
	CYPD_RESPONSE_EXT_MSG_SOP_RX = 0xAC,
	CYPD_RESPONSE_ALT_MODE_EVENT = 0xB0,
	CYPD_RESPONSE_ALT_MODE_HW_EVENT = 0xB1,
	CYPD_RESPONSE_EXT_SOP1_RX = 0xB4,
	CYPD_RESPONSE_EXT_SOP2_RX = 0xB5,
	CYPD_RESPONSE_OVER_TEMP = 0xB6,
	CYPD_RESPONSE_HARDWARE_ERROR = 0xB8,
	CYPD_RESPONSE_VCONN_OCP_ERROR = 0xB9,
	CYPD_RESPONSE_CC_OVP_ERROR = 0xBA,
	CYPD_RESPONSE_SBU_OVP_ERROR = 0xBB,
	CYPD_RESPONSE_VBUS_SHORT_ERROR = 0xBC,
	CYPD_RESPONSE_REVERSE_CURRENT_ERROR = 0xBD,
	CYPD_RESPONSE_SINK_STANDBY = 0xBE
};

/************************************************/
/*	TYPE-C STATUS DEFINITION                    */
/************************************************/
#define CYP5525_PORT_CONNECTION         0x01 /* bit 0 */
#define CYP5525_CC_POLARITY             0x02 /* bit 1 */
#define CYP5525_DEVICE_TYPE             0x1C /* bit 2-4 */
#define CYP5525_CURRENT_LEVEL           0xC0 /* bit 6-7 */

/************************************************/
/*	PD STATUS DEFINITION                        */
/************************************************/
#define CYP5525_PD_CONTRACT_STATE       0x04 /* bit 10 */


#define CYP5525_PD_SET_3A_PROF          0x02

/* 7 bit address  */
#define CYP5525_I2C_CHIP0              0x08
#define CYP5525_I2C_CHIP1              0x40

/*
 * commands
 */
#define CYP5225_RESET_CMD				0x0152 /* Byte[0]:'R', Byte[1]:0x01 */
#define CYP5225_RESET_CMD_I2C			0x0052 /* Byte[0]:'R', Byte[1]:0x00 */

enum cyp5525_state {
	CYP5525_STATE_ERROR,
	CYP5525_STATE_POWER_ON,
	CYP5525_STATE_BOOTING,
	CYP5525_STATE_RESET,
	CYP5525_STATE_SETUP,
	CYP5525_STATE_READY,
	CYP5525_STATE_BOOTLOADER,
	CYP5525_STATE_COUNT,
};

enum cyp5525_port_state {
	CYP5525_DEVICE_DETACH,
	CYP5525_DEVICE_ATTACH,
	CYP5525_DEVICE_ATTACH_WITH_CONTRACT,
	CYP5525_DEVICE_COUNT,
};


struct pd_chip_config_t {
	uint16_t i2c_port;
	uint16_t addr_flags;
	enum cyp5525_state state;
	int gpio;
};

/* PD CHIP */
void pd_chip_interrupt(enum gpio_signal signal);

void pd_extpower_is_present_interrupt(enum gpio_signal signal);

#endif	/* __CROS_EC_CYPRESS5525_H */
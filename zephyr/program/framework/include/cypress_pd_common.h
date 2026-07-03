/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cypress PD chip's driver
 */

#ifndef __CROS_EC_CYPRESS_PD_COMMON_H
#define __CROS_EC_CYPRESS_PD_COMMON_H

#include "usb_pd.h"
#include "raa489300.h"

/* 7 bit address  */
#define PRODUCT_ID	CONFIG_PD_USB_PID
#define VENDOR_ID	0x32ac

#define BB_PWR_DOWN_TIMEOUT (4000*MSEC)
/* The boot wait time is configurable in PD firmware to a value between
 * 10 ms and 200 ms (see CCG HPI spec).
 * To avoid communication issues, use the highest possible.
 */
#define CCG_MAX_TBOOTWAIT_VALUE (200 * MSEC)

/*
 * commands
 */
#define CCG_RESET_CMD		0x0152 /* Byte[0]:'R', Byte[1]:0x01 */
#define CCG_RESET_CMD_I2C	0x0052 /* Byte[0]:'R', Byte[1]:0x00 */

/************************************************/
/*    CCG DEVICE REGISTER ADDRESS DEFINITION    */
/************************************************/
#define CCG_DEVICE_MODE			0x0000
#define CCG_BOOT_MODE_REASON		0x0001
#define CCG_SILICON_ID			0x0002
#define CCG_INTR_REG			0x0006
#define CCG_RESET_REG			0x0008
#define CCG_READ_ALL_VERSION_REG	0x0010
#define CCG_FW2_VERSION_REG		0x0020
#define CCG_PDPORT_ENABLE_REG		0x002C
#define CCG_POWER_STAT			0x002E
#define CCG_BATTERY_STAT		0x0031

#define CCG_UCSI_STATUS_REG		0x0038
#define CCG_UCSI_CONTROL_REG		0x0039
#define CCG_SYS_PWR_STATE		0x003B
/* CYPRESS vender add cmd, not common */
#define CCG_CUST_C_CTRL_CONTROL_REG	0x003B
#define CCG_HPI_VERSION			0x003C
/*User registers from 0x40 to 0x48 are used for BB retimer */
#define CCG_DPM_CMD_REG			CONFIG_CCG_DPM_CMD_REG
#define CCG_MUX_CFG_REG			CONFIG_CCG_MUX_CFG_REG
#define CCG_DEINIT_PORT_REG		CONFIG_CCG_DEINIT_PORT_REG
#ifdef CONFIG_PD_CHIP_CCG8
#define CCG_RETIMER_UPDATE_MODE_CONTROL 0x0043
#elif defined(CONFIG_PD_CHIP_CCG6)
#ifdef CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE
#define CCG_BATTERT_STATE		0x004F
#endif /* CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE */
#endif
#define CCG_ICL_STS_REG			0x0042
#define CCG_ICL_BB_RETIMER_CMD_REG	0x0046
#define CCG_ICL_BB_RETIMER_DAT_REG	0x0048
#define CCG_USER_MAINBOARD_VERSION	0x004F
#define CCG_USER_BB_POWER_EVT		0x004E
#define CCG_USER_DISABLE_LOCKOUT	0x004D

#define CCG_RESPONSE_REG		0x007E
#define CCG_DATA_MEM_REG		0x1404
#define CCG_VERSION_REG			0xF000
#define CCG_CCI_REG			0xF004
#define CCG_CONTROL_REG			0xF008
#define CCG_MESSAGE_IN_REG		0xF010
#define CCG_MESSAGE_OUT_REG		0xF020
#define CCG_MESSAGE_OUT_REG_V2	0xF110


/************************************************/
/*    CCG PORT REGISTER ADDRESS DEFINITION      */
/************************************************/
#define CCG_DM_CONTROL_REG(x) \
	(0x1000 + ((x) * 0x1000))
#define CCG_SELECT_SOURCE_PDO_MASK_REG(x) \
	(0x1002 + ((x) * 0x1000))
#define CCG_SELECT_SOURCE_PDO_REG(x) \
	(0x1004 + ((x) * 0x1000))
#define CCG_SELECT_SINK_PDO_REG(x) \
	(0x1005 + ((x) * 0x1000))
#define CCG_PD_CONTROL_REG(x) \
	(0x1006 + ((x) * 0x1000))
#define CCG_PD_STATUS_REG(x) \
	(0x1008 + ((x) * 0x1000))
#define CCG_TYPE_C_STATUS_REG(x) \
	(0x100C + ((x) * 0x1000))
#define CCG_TYPE_C_VOLTAGE_REG(x) \
	(0x100D + ((x) * 0x1000))
#define CCG_CURRENT_PDO_REG(x) \
	(0x1010 + ((x) * 0x1000))
#define CCG_CURRENT_RDO_REG(x) \
	(0x1014 + ((x) * 0x1000))
#define CCG_EVENT_MASK_REG(x) \
	(0x1024 + ((x) * 0x1000))
#define CCG_VDM_EC_CONTROL_REG(x) \
	(0x102A + ((x) * 0x1000))
#define CCG_DP_ALT_MODE_CONFIG_REG(x) \
	(0x102B + ((x) * 0x1000))
#define CCG_PORT_VBUS_FET_CONTROL(x) \
	(0x1032 + ((x) * 0x1000))
#define CCG_PORT_INTR_STATUS_REG(x) \
	(0x1034 + ((x) * 0x1000))
#define CCG_PORT_CURRENT_REG(x) \
	(0x1058 + ((x) * 0x1000))
#define CCG_PORT_HOST_CAP_REG(x) \
	(0x105C + ((x) * 0x1000))
#define CCG_ALT_MODE_MASK_REG(x) \
	(0x1060 + ((x) * 0x1000))
#define SELECT_SINK_PDO_EPR_MASK(x) \
	(0x1065 + ((x) * 0x1000))
#define CCG_SINK_PPS_AVS_CTRL_REG(x) \
	(0x1066 + ((x) * 0x1000))
#define CCG_PORT_PD_RESPONSE_REG(x) \
	(0x1400 + ((x) * 0x1000))
#define CCG_READ_DATA_MEMORY_REG(x, offset) \
	((0x1404 + (offset)) + ((x) * 0x1000))
#define CCG_WRITE_DATA_MEMORY_REG(x, offset) \
	((0x1800 + (offset)) + ((x) * 0x1000))


/************************************************/
/*          DEVICE MODE DEFINITION              */
/************************************************/
#define CCG_BOOT_MODE	0x00
#define CCG_FW1_MODE	0x01
#define CCG_FW2_MODE	0x02

/************************************************/
/*          DEVICE INTERRUPT DEFINITION         */
/************************************************/
#define CCG_DEV_INTR	0x01
#define CCG_PORT0_INTR	0x02
#define CCG_PORT1_INTR	0x04
#define CCG_ICLR_INTR	0x08
#define CCG_UCSI_INTR	0x80

/************************************************/
/*          PORT INTERRUPT DEFINITION           */
/************************************************/
#define CCG_STATUS_TYPEC_ATTACH		0x00000001 /*bit 0 */
#define CCG_STATUS_TYPEC_DETACH		0x00000002 /*bit 1 */
#define CCG_STATUS_CONTRACT_DONE	0x00000004 /*bit 2 */
#define CCG_STATUS_PRSWAP_DONE		0x00000008 /*bit 3 */
#define CCG_STATUS_DRSWAP_DONE		0x00000010 /*bit 4 */
#define CCG_STATUS_VCONNSWAP_DONE	0x00000020 /*bit 5 */
#define CCG_STATUS_RESPONSE_READY	0x00200000 /*bit 21*/
#define CCG_STATUS_OVP_EVT		0x40000000 /*bit 30*/

/************************************************/
/*          PD PORT DEFINITION                  */
/************************************************/
#define CCG_PDPORT_DISABLE	0x00
#define CCG_PDPORT_ENABLE	0x01

/************************************************/
/*          POWER STATE DEFINITION              */
/************************************************/
#define CCG_POWERSTAT_EXT_POWER_PRESENT	BIT(1)
#define CCG_POWERSTAT_EXT_POWER_TYPE	BIT(2)
#define CCG_POWERSTAT_BATT_PRESENT		BIT(3)
#define CCG_POWERSTAT_INTERNAL_POWER	BIT(4)

/************************************************/
/*        SYSTEM POWER STATE DEFINITION         */
/************************************************/
#define CCG_POWERSTATE_S0	0x00
#define CCG_POWERSTATE_S3	0x01
#define CCG_POWERSTATE_S4	0x02
#define CCG_POWERSTATE_S5	0x03
#define CCG_POWERSTATE_S0ix	0x04
#define CCG_POWERSTATE_G3	0x05

/************************************************/
/*  CCG_CUST_C_CTRL_CONTROL_REG DEFINITION      */
/************************************************/
#define CCG_P0P1_CONTROL_BY_CY		0xA0
#define CCG_P0_OFF_P1_CY		0xA1
#define CCG_P0_CY_P1_OFF		0xA2
#define CCG_P0P1_TURN_OFF_C_CTRL	0xA3


/************************************************/
/*  DM CONTROL DEFINATION                       */
/************************************************/
#define CCG_DM_CTRL_SOP			0x00
#define CCG_DM_CTRL_SPO_PRIM		0x01
#define CCG_DM_CTRL_SPO_PRIM_PRIM	0x02

#define CCG_DM_CTRL_PD3_DATA_REQUEST			BIT(2)
#define CCG_DM_CTRL_EXTENDED_DATA_REQUEST		BIT(3)
#define CCG_DM_CTRL_SENDER_RESPONSE_TIMER_DISABLE	BIT(4)

#define CCG_EXTEND_MSG_CTRL_EN	BIT(1)

/*
 * Retimer control register commands
 */
#define RT_EVT_VSYS_REMOVED	0
#define RT_EVT_VSYS_ADDED	1
#define RT_EVT_RETRY_STATUS	2
#define RT_EVT_UPDATE_STATUS	3


/************************************************/
/*  EPR EVENT Response                          */
/************************************************/
#define EPR_EVENT_TYPE_MASK 0x7F
#define EPR_EVENT_POWER_ROLE_MASK 0x80
#define EPR_EVENT_POWER_ROLE_SINK 0x80

/************************************************/
/*  VBUS CONSUMER FET CONTROL                   */
/************************************************/
#define CCG_EC_VBUS_CTRL_EN BIT(0)
#define CCG_EC_VBUS_CTRL_ON BIT(1)

/************************************************/
/*  HELPER FUNCTIONS                            */
/************************************************/
#define PORT_TO_CONTROLLER(x) ((x) >> 1)
#define PORT_TO_CONTROLLER_PORT(x) ((x) & 0x01)
#define PDPORT(controller, port) ((controller * 2) + port)

/************************************************/
/*  CCG6 special setting                        */
/************************************************/
#ifdef CONFIG_PD_CHIP_CCG6
#define CCG6_AC_AT_PORT				0xC4
#endif
#define CCG_ICL_CTRL_REG	0x0040

#define BSDO_BATT_IS_PRESENT		BIT(1)
#define BSDO_BATT_IS_DISCHARGING	BIT(2)
#define BSDO_BATT_IS_IDLE		BIT(3)

/************************************************/
/*  CCG8 special setting                        */
/************************************************/
#ifdef CONFIG_PD_CCG8_EPR
#define EXIT_EPR BIT(6)
#define ENTER_EPR BIT(7)
#define EPR_PROCESS_MASK (EXIT_EPR + ENTER_EPR)
#endif

enum epr_event_type {
	EPR_MODE_ENTERED = 1,
	EPR_MODE_EXITED,
	EPR_MODE_ENTER_FAILED,
};

enum epr_event_failure_type {
	EPR_FAILURE_UNKNOWN,
	EPR_FAILURE_EPR_CABLE,
	EPR_FAILURE_EPR_VCONN,
	EPR_FAILURE_RDO,
	EPR_FAILURE_UNABLE_NOW,
	EPR_FAILURE_PDO
};

/************************************************/
/*  CCG Task Events                            */
/************************************************/
enum pd_task_evt {
	/*
	 * The controller index must be in order here for INT as we assume
	 * we can map controller number to the first one + id
	 */
	CCG_EVT_INT_CTRL_0 = BIT(0),
	CCG_EVT_INT_CTRL_1 = BIT(1),
	CCG_EVT_INT_CTRL_GPU = BIT(2),
	/*
	 * The controller index must be in order here for STATE as we assume
	 * we can map controller number to the first one + id
	 */
	CCG_EVT_STATE_CTRL_0 = BIT(3),
	CCG_EVT_STATE_CTRL_1 = BIT(4),
	CCG_EVT_STATE_CTRL_GPU = BIT(5),
	CCG_EVT_AC_PRESENT =  BIT(6),
	CCG_EVT_S_CHANGE = BIT(7),
	CCG_EVT_PLT_RESET = BIT(8),
	CCG_EVT_UCSI_POLL_CTRL_0 = BIT(9),
	CCG_EVT_UCSI_POLL_CTRL_1 = BIT(10),
	CCG_EVT_UCSI_POLL_CTRL_GPU = BIT(11),
	CCG_EVT_RETIMER_PWR = BIT(12),
	CCG_EVT_UPDATE_PWRSTAT = BIT(13),
	CCG_EVT_PORT_ENABLE = BIT(14),
	CCG_EVT_PORT_DISABLE = BIT(15),
	CCG_EVT_UCSI_PPM_RESET = BIT(16),
	CCG_EVT_CFET_VBUS_OFF = BIT(17),
	CCG_EVT_CFET_VBUS_ON = BIT(18),
	CCG_EVT_DPALT_DISABLE = BIT(19),
	CCG_EVT_CHANGE_P0_PDO_LIST = BIT(20),
	CCG_EVT_CHANGE_P1_PDO_LIST = BIT(21),
	CCG_EVT_PDO_C0P0 = BIT(22),
	CCG_EVT_PDO_C0P1 = BIT(23),
	CCG_EVT_PDO_C1P0 = BIT(24),
	CCG_EVT_PDO_C1P1 = BIT(25),
	CCG_EVT_PDO_C2P0 = BIT(26),
	CCG_EVT_PERFORM_ERROR_RECOVERY = BIT(27),
	CCG_EVT_PDO_RESET = BIT(28),
	CCG_EVT_BATT_UPDATE = BIT(29),
	CCG_EVT_UPDATE_PORTSTATE = BIT(30),
};

/************************************************
 *	PD COMMAND DEFINITION
 * See 001-97863_0N_V.pdf from cypress for the HPI
 * Definition. Specifically Pages around 102
 * Chapter 4.3.3.6 PD_CONTROL register
 ************************************************/
enum ccg_pd_command {
	CCG_PD_CMD_SET_TYPEC_DEFAULT = 0x00,
	CCG_PD_CMD_SET_TYPEC_1_5A,
	CCG_PD_CMD_SET_TYPEC_3A,
	CCG_PD_CMD_TRG_DATA_ROLE_SWAP = 0x05,
	CCG_PD_CMD_TRG_POWER_ROLE_SWAP = 0x06,
	CCG_PD_CMD_VCONN_EN = 0x07,
	CCG_PD_CMD_VCONN_DIS = 0x08,
	CCG_PD_CMD_TRG_VCONN_SWAP = 0x09,
	CCG_PD_CMD_HARD_RESET = 0x0D,
	CCG_PD_CMD_SOFT_RESET = 0x0E,
	CCG_PD_CMD_CABLE_RESET = 0x0F,
	CCG_PD_CMD_EC_INIT_COMPLETE = 0x10,
	CCG_PD_CMD_PORT_DISABLE = 0x11,
	CCG_PD_CMD_CHANGE_PD_PORT_PARAMS = 0x14,
	CCG_PD_CMD_READ_SRC_PDO = 0x20,
	CCG_PD_CMD_VSYS_TO_VBUS = 0x36,
	CCG_PD_CMD_RW_PD_RESPONSE_DATA = 0x3D,
	CCG_PD_CMD_INITIATE_EPR_ENTRY = 0x47,
	CCG_PD_CMD_INITIATE_EPR_EXIT = 0x48,
};

/************************************************
 * USER COMMANDS for register 0x0040
 ************************************************/
enum ccg_userreg_command {
	CCG_PD_USER_CMD_TYPEC_ERR_RECOVERY = 0x04,
	CCG_PD_USER_CMD_PD_SEND_HARD_RESET = 0x85,
	CCG_PD_USER_CMD_PD_SEND_SOFT_RESET = 0x86,
	CCG_PD_USER_CMD_DATA_RECOVERY      = 0xFF
};

/************************************************
 * USER MUXCFG for register 0x0041
 * This allows us to override the PD mux
 * configuration for a specific port.
 ************************************************/
enum ccg_usermux_configuration {
	CCG_PD_USER_MUX_CONFIG_ISOLATE = 0,
	CCG_PD_USER_MUX_CONFIG_SAFE,
	CCG_PD_USER_MUX_CONFIG_SS_ONLY,
	CCG_PD_USER_MUX_CONFIG_DEBUG_ACCESSORY = 0x0A
};



/************************************************
 *	RESPONSE DEFINITION
 * See HPISpecRevT.pdf from cypress for the HPI
 * Definition. Specifically Pages around 22
 * Chapter 4.1.1 HPI Interfaces response codes
 ************************************************/
enum ccg_response {
	CCG_RESPONSE_NONE,
	CCG_RESPONSE_SUCCESS = 0x02,
	CCG_RESPONSE_FLASH_DATA_AVALIABLE = 0x03, /* RESPONSE register only */
	CCG_RESPONSE_INVALID_COMMAND = 0x05,
	CCG_RESPONSE_INVALID_STATE = 0x06,
	CCG_RESPONSE_FLASH_UPDATE_FAILED = 0x07, /* RESPONSE register only */
	CCG_RESPONSE_INVALID_FW = 0x08, /* RESPONSE register only */
	CCG_RESPONSE_INVALID_ARGUMENTS = 0x09,
	CCG_RESPONSE_NOT_SUPPORTED = 0x0A,
	CCG_RESPONSE_TRANSACTION_FAILED = 0x0C,
	CCG_RESPONSE_PD_COMMAND_FAILED = 0x0D,
	CCG_RESPONSE_UNDEFINED_ERROR = 0x0F,
	CCG_RESPONSE_READ_PDO_DATA = 0x10,
	CCG_RESPONSE_CMD_ABORTED = 0x11,
	CCG_RESPONSE_PORT_BUSY = 0x12,
	CCG_RESPONSE_MINMAX_CURRENT = 0x13,
	CCG_RESPONSE_EXT_SRC_CAP = 0x14,
	CCG_RESPONSE_DID_RESPONSE = 0x18,
	CCG_RESPONSE_SVID_RESPONSE = 0x19,
	CCG_RESPONSE_DISCOVER_MODE_RESPONSE = 0x1A,
	CCG_RESPONSE_CABLE_COMM_NOT_ALLOWED = 0x1B,
	CCG_RESPONSE_EXT_SNK_CAP = 0x1C,
	CCG_RESPONSE_I2C_REG = 0x1D,
	CCG_RESPONSE_GENERIC_DATA_READ = 0x1E,
	CCG_RESPONSE_EPR_MIN_MAX_CURRENT = 0x1F,
	/* 0x20 to 0x2F reserved for user customized commands */
#ifdef CONFIG_PD_CHIP_CCG6
	CCG6_RESPONSE_AC_AT_P0 = 0x33,
	CCG6_RESPONSE_AC_AT_P1 = 0x34,
	CCG6_RESPONSE_NO_AC = 0x35,
	CCG6_RESPONSE_EC_MODE = 0x36,
#endif
#ifdef CONFIG_CHIPSET_AMD
	CCG_RESPONSE_AMD_CROSSBAR_READY = 0x38,
#endif
	CCG_RESPONSE_FWCT_IDENT_INVALID = 0x40,
	CCG_RESPONSE_FWCT_INVALID_GUID = 0x41,
	CCG_RESPONSE_FWCT_INVALID_VERSION = 0x42,
	CCG_RESPONSE_HPI_CMD_INVALID_SEQ = 0x43,
	CCG_RESPONSE_FWCT_AUTH_FAILED = 0x44,
	CCG_RESPONSE_HASH_FAILED = 0x45,
	/* Event and Asynchronous Message Codes */
	CCG_RESPONSE_RESET_COMPLETE = 0x80,
	CCG_RESPONSE_MESSAGE_QUEUE_OVERFLOW = 0x81,
	/* Type C Event and Asynchronous Message Codes */
	CCG_RESPONSE_OVER_CURRENT = 0x82,
	CCG_RESPONSE_OVER_VOLT = 0x83,
	CCG_RESPONSE_PORT_CONNECT = 0x84,
	CCG_RESPONSE_PORT_DISCONNECT = 0x85,
	CCG_RESPONSE_PD_CONTRACT_NEGOTIATION_COMPLETE = 0x86,
	CCG_RESPONSE_SWAP_COMPLETE = 0x87,
	CCG_RESPONSE_PS_RDY_MSG_PENDING = 0x8A,
	CCG_RESPONSE_GOTO_MIN_PENDING = 0x8B,
	CCG_RESPONSE_ACCEPT_MSG_RX = 0x8C,
	CCG_RESPONSE_REJECT_MSG_RX = 0x8D,
	CCG_RESPONSE_WAIT_MSG_RX = 0x8E,
	CCG_RESPONSE_HARD_RESET_RX = 0x8F,
	/* PD Data Message Specific Events */
	CCG_RESPONSE_VDM_RX = 0x90,
	/* Capability Message Specific Events */
	CCG_RESPONSE_SOURCE_CAP_MSG_RX = 0x91,
	CCG_RESPONSE_SINK_CAP_MSG_RX = 0x92,
	/* USB4 Events */
	CCG_RESPONSE_USB4_DATA_RESET_RX = 0x93,
	CCG_RESPONSE_USB4_DATA_RESET_COMPLETE = 0x94,
	CCG_RESPONSE_USB4_ENTRY_COMPLETE = 0x95,
	/* Resets and Errors */
	CCG_RESPONSE_HARD_RESET_SENT = 0x9A,
	CCG_RESPONSE_SOFT_RESET_SENT = 0x9B,
	CCG_RESPONSE_CABLE_RESET_SENT = 0x9C,
	CCG_RESPONSE_SOURCEDISABLED = 0x9D,
	CCG_RESPONSE_SENDER_RESPONSE_TIMEOUT = 0x9E,
	CCG_RESPONSE_NO_VDM_RESPONSE_RX = 0x9F,
	CCG_RESPONSE_UNEXPECTED_VOLTAGE = 0xA0,
	CCG_RESPONSE_TYPE_C_ERROR_RECOVERY = 0xA1,
	CCG_RESPONSE_BATTERY_STATUS_RX = 0xA2,
	CCG_RESPONSE_ALERT_RX = 0xA3,
	CCG_RESPONSE_UNSUPPORTED_MSG_RX = 0xA4,
	CCG_RESPONSE_EMCA_DETECTED = 0xA6,
	CCG_RESPONSE_CABLE_DISCOVERY_FAILED = 0xA7,
	CCG_RESPONSE_RP_CHANGE_DETECTED = 0xAA,
	CCG_RESPONSE_EXT_MSG_SOP_RX = 0xAC,
	CCG_RESPONSE_ALT_MODE_EVENT = 0xB0,
	CCG_RESPONSE_ALT_MODE_HW_EVENT = 0xB1,
	CCG_RESPONSE_EXT_SOP1_RX = 0xB4,
	CCG_RESPONSE_EXT_SOP2_RX = 0xB5,
	CCG_RESPONSE_OVER_TEMP = 0xB6,
	CCG_RESPONSE_HARDWARE_ERROR = 0xB8,
	CCG_RESPONSE_VCONN_OCP_ERROR = 0xB9,
	CCG_RESPONSE_CC_OVP_ERROR = 0xBA,
	CCG_RESPONSE_SBU_OVP_ERROR = 0xBB,
	CCG_RESPONSE_VBUS_SHORT_ERROR = 0xBC,
	CCG_RESPONSE_REVERSE_CURRENT_ERROR = 0xBD,
	CCG_RESPONSE_SINK_STANDBY = 0xBE,
	CCG_RESPONSE_ACK_TIMEOUT_EVENT = 0xC0,
	CCG_RESPONSE_SRC_RP_REMOVED = 0xC1,
	CCG_RESPONSE_PR_SWAP_ACCEPTED = 0xC2,
	CCG_RESPONSE_SNK_POWER_REDUCED = 0xC3,
	CCG_RESPONSE_BC12_EVENT = 0xC4,
	CCG_RESPONSE_COLLECTED_CHUNKED_MSG = 0xD4,
	CCG_RESPONSE_SRC_INFO_RECEIVED = 0xD7,
	CCG_RESPONSE_REVISION_RECEIVED = 0xD8,
	CCG_RESPONSE_EPR_EVENT = 0xD9,
	CCG_RESPONSE_PORT_DISABLED_EVENT = 0xDA
};

enum ccg_pd_state {
	CCG_STATE_ERROR,
	CCG_STATE_NO_POWER,
	CCG_STATE_WAIT_STABLE,
	CCG_STATE_POWER_ON,
	CCG_STATE_APP_SETUP,
	CCG_STATE_READY,
	CCG_STATE_BOOTLOADER,
	CCG_STATE_COUNT,
};

enum ccg_port_state {
	CCG_DEVICE_DETACH,
	CCG_DEVICE_ATTACH,
	CCG_DEVICE_ATTACH_WITH_CONTRACT,
	CCG_DEVICE_COUNT,
};

enum pd_epr_status {
	EPR_INACTIVE,
	EPR_ACTIVE,
	/* Tried to enable, but was unable to */
	EPR_DISABLED,
};

/*TYPE_C_STATUS_DEVICE*/
enum ccg_c_state {
	CCG_STATUS_NOTHING,
	CCG_STATUS_SINK,
	CCG_STATUS_SOURCE,
	CCG_STATUS_DEBUG,
	CCG_STATUS_AUDIO,
	CCG_STATUS_POWERED_ACC,
	CCG_STATUS_UNSUPPORTED,
	CCG_STATUS_INVALID,
};

enum pd_port_role {
	PORT_SINK,
	PORT_SOURCE,
	PORT_DUALROLE
};

#define PD_CHIP_COUNT CONFIG_PLATFORM_EC_PD_CHIP_MAX_COUNT
enum pd_chip {
	PD_CHIP_0,
	PD_CHIP_1,
	PD_CHIP_GPU,
};

#define PD_PORT_COUNT CONFIG_USB_PD_PORT_MAX_COUNT
enum pd_port {
	PD_PORT_0,
	PD_PORT_1,
	PD_PORT_2,
	PD_PORT_3,
	PD_PORT_4,
};

enum pd_progress {
	PD_PROGRESS_IDLE = 0,
	PD_PROGRESS_DISCONNECTED,
	PD_PROGRESS_ENTER_EPR_MODE,
	PD_PROGRESS_EXIT_EPR_MODE,
};

enum typec_safety_level {
	TYPEC_SAFETY_LEVEL_0,
	TYPEC_SAFETY_LEVEL_1,
	TYPEC_SAFETY_LEVEL_2,
	TYPEC_SAFETY_LEVEL_3,
	TYPEC_SAFETY_LEVEL_COUNT,
};

struct pd_chip_config_t {
	uint16_t i2c_port;
	uint16_t addr_flags;
	uint8_t support_max_port;
	enum ccg_pd_state state;
	int gpio;
	uint8_t version[8];
};

struct pd_port_map_t {
	uint8_t controller;
	uint8_t controller_port;
	uint16_t flags;
};

struct pd_port_current_state_t {
	enum ccg_port_state port_state;
	int voltage;
	int current;
	int ac_port;
	enum ccg_c_state c_state; /* What device is attached on the other side */
	uint8_t pd_state;
	uint8_t cc;
	enum pd_epr_status epr_status;
	uint8_t epr_support;
	uint8_t epr_retry_count;
	int safety_table[TYPEC_SAFETY_LEVEL_COUNT];
	bool rdo_mismatch;
	int max_operating_current;

	enum pd_power_role power_role;
	enum pd_data_role data_role;
	enum pd_vconn_role vconn;
};

struct pd_chip_ucsi_info_t {
	uint16_t version;
	uint16_t reserved;
	uint32_t cci;
	struct ucsi_control_t {
		uint8_t command;
		uint8_t data_len;
		uint8_t data[6];
	} control;
	uint8_t message_in[34];
	uint8_t message_out[16];
	int message_in_length;
	int read_tunnel_complete;
	int write_tunnel_complete;
	int wait_ack;
};

struct alert_msg_t {
	uint16_t header;	/* message header */
	uint16_t reserved;
	uint32_t ado;		/* alert data object */
};

#ifdef CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE

/**
 * follow CCG6 vendor Format
 * byte[0] - reg, 0x0 = batt_cap, 0x01 = batt_status.
 * ohters byte follow PD Spec format
 */
struct pd_battery_cap_t {
	uint8_t  reg;
	/* USB BCDO */
	uint16_t vid;
	uint16_t pid;
	uint16_t design_cap;
	uint16_t last_full_cap;
	uint8_t	 battery_type;
} __packed;

struct pd_battery_status_t {
	uint8_t reg;
	/* USB BSDO */
	uint8_t reserved;
	uint8_t battery_info;
	uint16_t batt_present_cap;
} __packed;
#endif /* CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE */

#ifdef CONFIG_PD_CCG8_CUSTOMIZE_BATT_MESSAGE

/**
 * follow CCG6 vendor Format
 * byte[0] - reg, 0x0 = batt_cap, 0x01 = batt_status.
 * ohters byte follow PD Spec format
 */
struct pd_battery_cap_t {
	/* Header */
	uint8_t  type; /* set to 0x02 */
	uint8_t  command; /* set to 0x04 for all ports B0:1 command = 0 for write */
	uint8_t  size; /* set to 13 */
	uint8_t  reserved;
	/* payload */
	uint8_t battery_slot_id; /* should always be set to 0 for first battery */
	uint8_t invalid_ref_flag; /* should always be set to 0 for first battery */
	uint16_t reserved_hdr;
	/* USB BCDO */
	uint16_t vid;
	uint16_t pid;
	uint16_t design_cap;
	uint16_t last_full_cap;
	uint8_t	 battery_type;
} __packed;

struct pd_battery_status_t {
	/* Header */
	uint8_t  type; /* set to 0x01 */
	uint8_t  command; /* set to 0x04 for all ports B0:1 command = 0 for write */
	uint8_t  size; /* set to 8 */
	uint8_t  reserved;
	/* payload */
	uint8_t battery_slot_id; /* should always be set to 0 for first battery */
	uint8_t invalid_ref_flag; /* should always be set to 0 for first battery */
	uint16_t reserved_hdr;
	/* USB BSDO */
	uint8_t reserved_bsdo;
	uint8_t battery_info;
	uint16_t batt_present_cap;
} __packed;
#endif /* CONFIG_PD_CCG8_CUSTOMIZE_BATT_MESSAGE */

/**
 * extern struct for ccg6 or ccg8 use.
 */
extern struct pd_chip_config_t pd_chip_config[];
extern struct pd_port_current_state_t pd_port_states[];
extern struct pd_chip_ucsi_info_t pd_chip_ucsi_info[];

/**
 * cypress i2c write functions
 *
 * @param controller	PD chip controller
 * @param reg			PD i2c register
 * @param data			PD i2c data
 * @param len			PD i2c data size
 * @return int
 */
int cypd_write_reg8(int controller, int reg, int data);
int cypd_write_reg16(int controller, int reg, int data);
int cypd_write_reg_block(int controller, int reg, void *data, int len);

/**
 * cypress i2c read functions
 *
 * @param controller	PD chip controller
 * @param reg			PD i2c register
 * @param data			PD i2c data
 * @param len			PD i2c data size
 * @return int
 */
int cypd_read_reg8(int controller, int reg, int *data);
int cypd_read_reg16(int controller, int reg, int *data);
int cypd_read_reg_block(int controller, int reg, void *data, int len);

/**
 * Clear PD interrupt event mask
 *
 * @param controller	PD chip controller
 * @param mask			interrupt event mask
 * @return int
 */
int cypd_clear_int(int controller, int mask);

/**
 * Get PD interrupt event
 *
 * @param controller	PD chip controller
 * @param mask			interrupt event
 * @return int
 */
int cypd_get_int(int controller, int *intreg);

/**
 * Function will execute when ucsi ppm reset
 */
void cypd_usci_ppm_reset(void);

/**
 * Writes data to a PD controller register and retrieves the hardware response code.
 * This function sends a write command to a specific register on the target PD (Power Delivery)
 * controller. It provides a synchronous mechanism to capture the immediate response code
 * returned by the PD firmware.
 *
 * @param controller	The index of the PD chip controller.
 * @param reg			The specific register address to write to.
 * @param data			The data value to be written into the register.
 * @param respond_code	The HPI cmd response code.
 * @return int Returns 0 on successful communication, or a non-zero error code if the
 *		   I2C bus transaction fails.
 */
int cypd_write_reg_with_respond(int controller, int reg, int data, int *respond_code);

/**
 * Function for wait PD response
 *
 * @param controller	PD chip controller
 * @param timeout_us	wait delay time
 * @return int
 */
int cypd_wait_for_ack(int controller, int timeout_us);

/**
 * Project customize PD response event when write cmd
 *
 * @param controller	PD chip controller
 * @param reg			write register address
 * @param data			write register data
 * @return int
 */
int cypd_write_reg8_wait_ack(int controller, int reg, int data);

/**
 * Customize EC console log print
 *
 * @param msg			Print message title
 * @param buff			Print i2c read data
 * @param len			i2c read data size
 */
void cypd_print_buff(const char *msg, void *buff, int len);

/**
 * Set PD power state for sync system status
 *
 * @param power_state	Set PD power state
 * @param controller	PD chip controller
 */
void cypd_set_power_state(int power_state, int controller);

#ifdef CONFIG_PD_CHIP_CCG6
/**
 * command PD let retimer into compliance
 * and fw update mode
 *
 * @param controller	PD chip controller
 */
void enable_compliance_mode(int controller);

/**
 * command PD let retimer leave compliance
 * and fw update mode
 *
 * @param controller	PD chip controller
 */
void disable_compliance_mode(int controller);

/**
 * command PD let retimer force in TBT mode
 *
 * @param controller	PD chip controller
 */
void entry_tbt_mode(int controller);

/**
 * command PD let retimer leave TBT mode
 *
 * @param controller	PD chip controller
 */
void exit_tbt_mode(int controller);

/**
 * check retimer TBT mode
 *
 * @param controller	PD chip controller
 * @return int
 */
int check_tbt_mode(int controller);
#endif /* CONFIG_PD_CHIP_CCG6 */

#if defined(CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE) || defined(CONFIG_PD_CCG8_CUSTOMIZE_BATT_MESSAGE)
/**
 * Set battery_cap info to PD
 */
void cypd_customize_battery_cap(void);

/**
 * Set battery_status info to PD
 */
void cypd_customize_battery_status(void);
#endif /* CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE */

/**
 * Project can customize app_setup behavior
 *
 * @param controller	PD chip controller
 */
__override_proto void cypd_customize_app_setup(int controller);

/**
 * Project can set the PD setup(init) status
 *
 * @param controller	PD chip controller
 * @return int
 */
int cypd_setup(int controller);

/**
 * Project can set the PD action when system change.
 *
 * @param controller	PD chip controller
 */
void update_system_power_state(int controller);

/**
 * If PD chip is doing the firmware update, we should disable the PD task
 *
 * @param is_update	Firmware update flag.
 */
void set_pd_fw_update(bool is_update);

/**
 * @param updating return true when firmware is updating
 */
bool cypd_fw_update_in_progress(void);

/**
 * After PD chip firmware update complete, need to reinitialize the PD chip
 */
void cypd_reinitialize(void);

/**
 * Get the pd version object
 *
 * @param controller	PD chip controller
 * @return uint8_t*
 */
uint8_t *get_pd_version(int controller);

/**
 * Get the PDO current
 *
 * @param port		The activer charge port
 * @return int
 */
int pd_get_active_current(int port);

/**
 * Set system power state
 *
 */
void cypd_set_power_active(void);

/**
 * Get the active charge pd chip
 *
 * @return int
 */
int active_charge_pd_chip(void);

/**
 * Get the active charge port
 *
 * @return int
 */
int get_active_charge_pd_port(void);

/**
 * Get the active charge port
 *
 * @param update_charger_port	update prev charger port
 */
void update_active_charge_pd_port(int update_charger_port);

/**
 * Return Power source port state
 *
 * @return int
 */
int cypd_vbus_state_check(void);

/**
 * Return ac power, return by mW.
 *
 * @return int
 */
int cypd_get_ac_power(void);

/**
 * Return active port voltage, return by mV.
 *
 * @return int
 */
int cypd_get_active_port_voltage(void);

/**
 * Set Pdo profile for safety action
 *
 * @param controller	PD chip controller
 * @param port			PD controller port
 * @param profile		Pdo profile
 * @return int
 */
int cypd_modify_safety_power(int controller, int port, int profile);

/**
 * return type-c port is 3A or not
 *
 * @param controller	PD chip controller
 * @param port			PD controller port
 * @return int
 */
int cypd_port_3a_status(int controller, int port);

/**
 * Update Port status set pdo and power limit
 *
 * @param controller	PD chip controller
 * @param port			PD controller port
 */
void cypd_update_port_state(int controller, int port);

/**
 * return active port cfet status
 *
 * @return uint8_t
 */
uint8_t cypd_get_cfet_status(void);

/**
 * Set PD task event CCG_EVT_UPDATE_PWRSTAT
 */
void update_power_state_deferred(void);

#ifdef CONFIG_PD_CCG8_EPR

#ifdef CONFIG_PLATFORM_EC_CHARGER_RAA489300
enum buck_transition_phase {
	BUCK_PHASE_IDLE = 0,
	BUCK_PHASE_SET_MODE,
	BUCK_PHASE_CHECK_READY,
};

struct epr_buck_transition_ctx {
	int retry_count;
	enum pd_progress progress;
	enum buck_transition_phase phase;
};

/**
 * Board-specific callback to confirm if the 3-Level Buck transition is ready.
 *
 * This function checks whether the system is prepared to transition
 * to Buck mode, with additional handling based on EPR mode support.
 *
 * @param mode	Select the mode you want to set.
 * @return EC_SUCCESS if set mode or transition is ready, error code otherwise.
 */
__override_proto int board_set_buck_mode(enum level_buck_mode mode);
__override_proto int board_confirm_buck_transition_ready(enum level_buck_mode mode);
#endif

/**
 * Command PD exit EPR mode
 */
void exit_epr_mode(void);

/**
 * Command PD into EPR mode
 */
void enter_epr_mode(void);

/**
 * Delay enter EPR mode
 *
 * @param delay		Delay MSEC
 */
void cypd_enter_epr_mode(int delay);

/**
 * return PD EPR status
 *
 * @return int
 */
int epr_progress_status(void);

/**
 * Clear EPR statue by &= ~EPR_PROCESS_MASK
 */
void clear_epr_progress_mask(void);

/**
 * fully clear EPR Status
 */
void clear_epr_progress(void);

/**
 * Update EPR Progress status when epr event trigger
 *
 * @param controller	PD chip controller
 * @param port			PD controller port
 * @param response_len	EPR event response len
 */
void cypd_update_epr_state(int controller, int port, int response_len);

#endif /* CONFIG_PD_CCG8_EPR */

#ifdef CONFIG_PD_COMMON_VBUS_CONTROL
int cypd_cfet_vbus_control(int port, bool enable, bool ec_control);
#endif /* CONFIG_PD_CCG8_EPR */

/**
 * Get the current state of the PD port.
 *
 * @return A pointer to the pd_port_current_state_t structure representing the current state
 * of the port.
 */
struct pd_port_current_state_t *get_pd_port_states_array(void);

/**
 * Retrieves the register of the PD alternate mode .
 *
 * @param port The port number for which to retrieve the state.
 * @return pd alt mode register value.
 */
int get_pd_alt_mode_status(int port);

/**
 * perform error recovery
 *
 * @param controller The PD chip for which to perform the error recovery
 */
void perform_error_recovery(int controller);

/**
 * CYPD interrupt pin control.
 *
 * @param controller The port number for which to retrieve the state.
 * @param enable_ndisable enable/disable interrupt.
 */
void cypd_enable_interrupt(int controller, int enable_ndisable);

/**
 * Update the PD chips state
 *
 * @param controller The PD chip for which to update the chip state
 * @param state The PD chip state that wants to change
 */
void cypd_update_chips_state(int controller, enum ccg_pd_state state);

/**
 * CCD mode control
 *
 * Check the type-c port status and control the ccd mode, called by projects
 */
void cypd_ccd_mode_control(void);

/**
 * Check the PD chip is ready to communicate
 *
 * @param controller The PD chip for which to check the status
 * @return false if the PD chips is not powered or waiting it to be stable
 */
bool cypd_contoller_is_powered(int controller);

/**
 * Initialize the optional ccg8s on 16in projects.
 * 
 * @param address The i2c address the PD controller is connected to.
 * @param interrupt The interrupt pin that is connected to the pd controller to signal
 * 					the ec
 * @param flags		Additional flags needed to be passed to the PD controller.
 */
bool ccg8s_init(uint8_t address, uint32_t flags);


/**
 * Trigger GPU interrupt for PD
 * 
 */
void ccg8s_interrupt(enum gpio_signal signal);

/**
 * return active pd chip count
 *
 */
int cypd_get_active_pd_chip_count(void);

/**
 * Call this function to update the safety table with the different safety level.
 *
 * @param safety_level The safety level. Refer enum typec_safety_level
 */
void cypd_update_safety_table(int safety_level);

/**
 * This function allow the project to change the pd port's order to
 * perform the error recovery.
 */
__override_proto int board_perform_error_recovery_port(int port);

/*
 * EC send PD_CONTROL with 0x36 to CCGx to switch over from VSYS switch to VBUS
 * regulator before turning off VSYS power.
 * VDDD keeps 3.3V (No Reboot) during VSYS to VBUS transition after applying resolution.
 *
 * @param port which type-c port should switch the VSYS to VBUS.
 * @return EC_SUCCESS, or non-zero if error.
 */
int cypd_vsys_to_vbus_transition(int port);


#endif /* __CROS_EC_CYPRESS_PD_COMMON_H */

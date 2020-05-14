/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery module */

#ifndef __CROS_EC_USB_PD_H
#define __CROS_EC_USB_PD_H

#include <stdbool.h>
#include "common.h"
#include "ec_commands.h"
#include "usb_pd_tbt.h"
#include "usb_pd_tcpm.h"
#include "usb_pd_vdo.h"

/* PD Host command timeout */
#define PD_HOST_COMMAND_TIMEOUT_US SECOND

#ifdef CONFIG_USB_PD_PORT_MAX_COUNT
/*
 * Define PD_PORT_TO_TASK_ID() and TASK_ID_TO_PD_PORT() macros to
 * go between PD port number and task ID. Assume that TASK_ID_PD_C0 is the
 * lowest task ID and IDs are on a continuous range.
 */
#ifdef HAS_TASK_PD_C0
#define PD_PORT_TO_TASK_ID(port) (TASK_ID_PD_C0 + (port))
#define TASK_ID_TO_PD_PORT(id) ((id) - TASK_ID_PD_C0)
#else
#define PD_PORT_TO_TASK_ID(port) -1 /* dummy task ID */
#define TASK_ID_TO_PD_PORT(id) 0
#endif /* HAS_TASK_PD_C0 */
#endif /* CONFIG_USB_PD_PORT_MAX_COUNT */

enum pd_rx_errors {
	PD_RX_ERR_INVAL = -1,           /* Invalid packet */
	PD_RX_ERR_HARD_RESET = -2,      /* Got a Hard-Reset packet */
	PD_RX_ERR_CRC = -3,             /* CRC mismatch */
	PD_RX_ERR_ID = -4,              /* Invalid ID number */
	PD_RX_ERR_UNSUPPORTED_SOP = -5, /* Unsupported SOP */
	PD_RX_ERR_CABLE_RESET = -6      /* Got a Cable-Reset packet */
};

/* Events for USB PD task */

/* Outgoing packet event */
#define PD_EVENT_TX			TASK_EVENT_CUSTOM_BIT(3)
/* CC line change event */
#define PD_EVENT_CC			TASK_EVENT_CUSTOM_BIT(4)
/* TCPC has reset */
#define PD_EVENT_TCPC_RESET		TASK_EVENT_CUSTOM_BIT(5)
/* DRP state has changed */
#define PD_EVENT_UPDATE_DUAL_ROLE	TASK_EVENT_CUSTOM_BIT(6)
/*
 * A task, other than the task owning the PD port, accessed the TCPC. The task
 * that owns the port does not send itself this event.
 */
#define PD_EVENT_DEVICE_ACCESSED	TASK_EVENT_CUSTOM_BIT(7)
/* Chipset power state changed */
#define PD_EVENT_POWER_STATE_CHANGE	TASK_EVENT_CUSTOM_BIT(8)
/* Issue a Hard Reset. */
#define PD_EVENT_SEND_HARD_RESET	TASK_EVENT_CUSTOM_BIT(9)
/* PD State machine event */
#define PD_EVENT_SM			TASK_EVENT_CUSTOM_BIT(10)
/* Prepare for sysjump */
#define PD_EVENT_SYSJUMP		TASK_EVENT_CUSTOM_BIT(11)
/* First free event on PD task */
#define PD_EVENT_FIRST_FREE_BIT		12

/* Ensure TCPC is out of low power mode before handling these events. */
#define PD_EXIT_LOW_POWER_EVENT_MASK \
	(PD_EVENT_CC | \
	 PD_EVENT_UPDATE_DUAL_ROLE | \
	 PD_EVENT_POWER_STATE_CHANGE | \
	 TASK_EVENT_WAKE)

/* --- PD data message helpers --- */
#define PDO_MAX_OBJECTS   7
#define PDO_MODES (PDO_MAX_OBJECTS - 1)

/* PDO : Power Data Object */
/*
 * 1. The vSafe5V Fixed Supply Object shall always be the first object.
 * 2. The remaining Fixed Supply Objects,
 *    if present, shall be sent in voltage order; lowest to highest.
 * 3. The Battery Supply Objects,
 *    if present shall be sent in Minimum Voltage order; lowest to highest.
 * 4. The Variable Supply (non battery) Objects,
 *    if present, shall be sent in Minimum Voltage order; lowest to highest.
 * 5. (PD3.0) The Augmented PDO is defined to allow extension beyond the 4 PDOs
 *     above by examining bits <29:28> to determine the additional PDO function.
 */
#define PDO_TYPE_FIXED     (0 << 30)
#define PDO_TYPE_BATTERY   BIT(30)
#define PDO_TYPE_VARIABLE  (2 << 30)
#define PDO_TYPE_AUGMENTED (3 << 30)
#define PDO_TYPE_MASK      (3 << 30)

#define PDO_FIXED_DUAL_ROLE	BIT(29) /* Dual role device */
#define PDO_FIXED_SUSPEND	BIT(28) /* USB Suspend supported */
#define PDO_FIXED_UNCONSTRAINED	BIT(27) /* Unconstrained Power */
#define PDO_FIXED_COMM_CAP	BIT(26) /* USB Communications Capable */
#define PDO_FIXED_DATA_SWAP	BIT(25) /* Data role swap command supported */
#define PDO_FIXED_FRS_CURR_MASK (3 << 23) /* [23..24] FRS current */
#define PDO_FIXED_FRS_CURR_NOT_SUPPORTED  (0 << 23)
#define PDO_FIXED_FRS_CURR_DFLT_USB_POWER (1 << 23)
#define PDO_FIXED_FRS_CURR_1A5_AT_5V      (2 << 23)
#define PDO_FIXED_FRS_CURR_3A0_AT_5V      (3 << 23)
#define PDO_FIXED_PEAK_CURR () /* [21..20] Peak current */
#define PDO_FIXED_VOLT(mv)  (((mv)/50) << 10) /* Voltage in 50mV units */
#define PDO_FIXED_CURR(ma)  (((ma)/10) << 0)  /* Max current in 10mA units */

#define PDO_FIXED(mv, ma, flags) (PDO_FIXED_VOLT(mv) |\
				  PDO_FIXED_CURR(ma) | (flags))

#define PDO_VAR_MAX_VOLT(mv) ((((mv) / 50) & 0x3FF) << 20)
#define PDO_VAR_MIN_VOLT(mv) ((((mv) / 50) & 0x3FF) << 10)
#define PDO_VAR_OP_CURR(ma)  ((((ma) / 10) & 0x3FF) << 0)

#define PDO_VAR(min_mv, max_mv, op_ma) \
				(PDO_VAR_MIN_VOLT(min_mv) | \
				 PDO_VAR_MAX_VOLT(max_mv) | \
				 PDO_VAR_OP_CURR(op_ma)   | \
				 PDO_TYPE_VARIABLE)

#define PDO_BATT_MAX_VOLT(mv) ((((mv) / 50) & 0x3FF) << 20)
#define PDO_BATT_MIN_VOLT(mv) ((((mv) / 50) & 0x3FF) << 10)
#define PDO_BATT_OP_POWER(mw) ((((mw) / 250) & 0x3FF) << 0)

#define PDO_BATT(min_mv, max_mv, op_mw) \
				(PDO_BATT_MIN_VOLT(min_mv) | \
				 PDO_BATT_MAX_VOLT(max_mv) | \
				 PDO_BATT_OP_POWER(op_mw) | \
				 PDO_TYPE_BATTERY)

/* RDO : Request Data Object */
#define RDO_OBJ_POS(n)             (((n) & 0x7) << 28)
#define RDO_POS(rdo)               (((rdo) >> 28) & 0x7)
#define RDO_GIVE_BACK              BIT(27)
#define RDO_CAP_MISMATCH           BIT(26)
#define RDO_COMM_CAP               BIT(25)
#define RDO_NO_SUSPEND             BIT(24)
#define RDO_FIXED_VAR_OP_CURR(ma)  ((((ma) / 10) & 0x3FF) << 10)
#define RDO_FIXED_VAR_MAX_CURR(ma) ((((ma) / 10) & 0x3FF) << 0)

#define RDO_BATT_OP_POWER(mw)      ((((mw) / 250) & 0x3FF) << 10)
#define RDO_BATT_MAX_POWER(mw)     ((((mw) / 250) & 0x3FF) << 10)

#define RDO_FIXED(n, op_ma, max_ma, flags) \
				(RDO_OBJ_POS(n) | (flags) | \
				RDO_FIXED_VAR_OP_CURR(op_ma) | \
				RDO_FIXED_VAR_MAX_CURR(max_ma))


#define RDO_BATT(n, op_mw, max_mw, flags) \
				(RDO_OBJ_POS(n) | (flags) | \
				RDO_BATT_OP_POWER(op_mw) | \
				RDO_BATT_MAX_POWER(max_mw))

/* BDO : BIST Data Object */
#define BDO_MODE_RECV       (0 << 28)
#define BDO_MODE_TRANSMIT   BIT(28)
#define BDO_MODE_COUNTERS   (2 << 28)
#define BDO_MODE_CARRIER0   (3 << 28)
#define BDO_MODE_CARRIER1   (4 << 28)
#define BDO_MODE_CARRIER2   (5 << 28)
#define BDO_MODE_CARRIER3   (6 << 28)
#define BDO_MODE_EYE        (7 << 28)

#define BDO(mode, cnt)      ((mode) | ((cnt) & 0xFFFF))

#define BIST_MODE(n)          ((n) >> 28)
#define BIST_ERROR_COUNTER(n) ((n) & 0xffff)
#define BIST_RECEIVER_MODE    0
#define BIST_TRANSMIT_MODE    1
#define BIST_RETURNED_COUNTER 2
#define BIST_CARRIER_MODE_0   3
#define BIST_CARRIER_MODE_1   4
#define BIST_CARRIER_MODE_2   5
#define BIST_CARRIER_MODE_3   6
#define BIST_EYE_PATTERN      7
#define BIST_TEST_DATA        8

#define SVID_DISCOVERY_MAX 16

/* Timers */
#define PD_T_SINK_TX            (18*MSEC) /* between 16ms and 20 */
#define PD_T_CHUNK_SENDER_RSP   (24*MSEC) /* between 24ms and 30ms */
#define PD_T_CHUNK_SENDER_REQ   (24*MSEC) /* between 24ms and 30ms */
#define PD_T_HARD_RESET_COMPLETE (5*MSEC) /* between 4ms and 5ms*/
#define PD_T_HARD_RESET_RETRY    (1*MSEC) /* 1ms */
#define PD_T_SEND_SOURCE_CAP   (100*MSEC) /* between 100ms and 200ms */
#define PD_T_SINK_WAIT_CAP     (600*MSEC) /* between 310ms and 620ms */
#define PD_T_SINK_TRANSITION    (35*MSEC) /* between 20ms and 35ms */
#define PD_T_SOURCE_ACTIVITY    (45*MSEC) /* between 40ms and 50ms */
#define PD_T_SENDER_RESPONSE    (30*MSEC) /* between 24ms and 30ms */
#define PD_T_PS_TRANSITION     (500*MSEC) /* between 450ms and 550ms */
#define PD_T_PS_SOURCE_ON      (480*MSEC) /* between 390ms and 480ms */
#define PD_T_PS_SOURCE_OFF     (920*MSEC) /* between 750ms and 920ms */
#define PD_T_PS_HARD_RESET      (25*MSEC) /* between 25ms and 35ms */
#define PD_T_ERROR_RECOVERY     (25*MSEC) /* 25ms */
#define PD_T_CC_DEBOUNCE       (100*MSEC) /* between 100ms and 200ms */
/* DRP_SNK + DRP_SRC must be between 50ms and 100ms with 30%-70% duty cycle */
#define PD_T_DRP_SNK           (40*MSEC) /* toggle time for sink DRP */
#define PD_T_DRP_SRC           (30*MSEC) /* toggle time for source DRP */
#define PD_T_DEBOUNCE          (15*MSEC) /* between 10ms and 20ms */
#define PD_T_TRY_CC_DEBOUNCE   (15*MSEC) /* between 10ms and 20ms */
#define PD_T_SINK_ADJ          (55*MSEC) /* between PD_T_DEBOUNCE and 60ms */
#define PD_T_SRC_RECOVER      (760*MSEC) /* between 660ms and 1000ms */
#define PD_T_SRC_RECOVER_MAX (1000*MSEC) /* 1000ms */
#define PD_T_SRC_TURN_ON      (275*MSEC) /* 275ms */
#define PD_T_SAFE_0V          (650*MSEC) /* 650ms */
#define PD_T_NO_RESPONSE     (5500*MSEC) /* between 4.5s and 5.5s */
#define PD_T_BIST_TRANSMIT     (50*MSEC) /* 50ms (used for task_wait arg) */
#define PD_T_BIST_RECEIVE      (60*MSEC) /* 60ms (max time to process bist) */
#define PD_T_BIST_CONT_MODE    (60*MSEC) /* 30ms to 60ms */
#define PD_T_VCONN_SOURCE_ON  (100*MSEC) /* 100ms */
#define PD_T_DRP_TRY          (125*MSEC) /* btween 75 and 150ms(monitor Vbus) */
#define PD_T_TRY_TIMEOUT      (550*MSEC) /* between 550ms and 1100ms */
#define PD_T_TRY_WAIT         (600*MSEC) /* Max time for TryWait.SNK state */
#define PD_T_SINK_REQUEST     (100*MSEC) /* Wait 100ms before next request */
#define PD_T_PD_DEBOUNCE      (15*MSEC)  /* between 10ms and 20ms */
#define PD_T_CHUNK_SENDER_RESPONSE (25*MSEC) /* 25ms */
#define PD_T_CHUNK_SENDER_REQUEST  (25*MSEC) /* 25ms */
#define PD_T_SWAP_SOURCE_START     (25*MSEC) /* Min of 20ms */
#define PD_T_RP_VALUE_CHANGE       (20*MSEC) /* 20ms */
#define PD_T_SRC_DISCONNECT        (15*MSEC) /* 15ms */
#define PD_T_VCONN_STABLE          (50*MSEC) /* 50ms */
#define PD_T_DISCOVER_IDENTITY     (45*MSEC) /* between 40ms and 50ms */
#define PD_T_SYSJUMP               (1000*MSEC) /* 1s */

/* number of edges and time window to detect CC line is not idle */
#define PD_RX_TRANSITION_COUNT  3
#define PD_RX_TRANSITION_WINDOW 20 /* between 12us and 20us */

/* from USB Type-C Specification Table 5-1 */
#define PD_T_AME (1*SECOND) /* timeout from UFP attach to Alt Mode Entry */

/* VDM Timers ( USB PD Spec Rev2.0 Table 6-30 )*/
#define PD_T_VDM_BUSY           (50*MSEC) /* at least 50ms */
#define PD_T_VDM_E_MODE         (25*MSEC) /* enter/exit the same max */
#define PD_T_VDM_RCVR_RSP       (15*MSEC) /* max of 15ms */
#define PD_T_VDM_SNDR_RSP       (30*MSEC) /* max of 30ms */
#define PD_T_VDM_WAIT_MODE_E   (100*MSEC) /* enter/exit the same max */

/* CTVPD Timers ( USB Type-C ECN Table 4-27 ) */
#define PD_T_VPDDETACH        (20*MSEC) /* max of 20*MSEC */
#define PD_T_VPDCTDD           (4*MSEC) /* max of 4ms */
#define PD_T_VPDDISABLE       (25*MSEC) /* min of 25ms */

/* function table for entered mode */
struct amode_fx {
	int (*status)(int port, uint32_t *payload);
	int (*config)(int port, uint32_t *payload);
};

/* function table for alternate mode capable responders */
struct svdm_response {
	int (*identity)(int port, uint32_t *payload);
	int (*svids)(int port, uint32_t *payload);
	int (*modes)(int port, uint32_t *payload);
	int (*enter_mode)(int port, uint32_t *payload);
	int (*exit_mode)(int port, uint32_t *payload);
	struct amode_fx *amode;
};

/*
 * State of discovery
 *
 * Note: Discovery needed must be 0 to meet expectations that it be the default
 * value after resetting connection information via memset.
 */
enum pd_discovery_state {
	PD_DISC_NEEDED = 0,	/* Cable or partner still needs to be probed */
	PD_DISC_COMPLETE,	/* Successfully probed, valid to read VDO */
	PD_DISC_FAIL,		/* Cable did not respond, or Discover* NAK */
};

/* Mode discovery state for a particular SVID with a particular transmit type */
struct svid_mode_data {
	/* The SVID for which modes are discovered */
	uint16_t svid;
	/* The number of modes discovered for this SVID */
	int mode_cnt;
	/* The discovered mode VDOs */
	uint32_t mode_vdo[PDO_MODES];
	/* State of mode discovery for this SVID */
	enum pd_discovery_state discovery;
};

struct svdm_amode_fx {
	uint16_t svid;
	int (*enter)(int port, uint32_t mode_caps);
	int (*status)(int port, uint32_t *payload);
	int (*config)(int port, uint32_t *payload);
	void (*post_config)(int port);
	int (*attention)(int port, uint32_t *payload);
	void (*exit)(int port);
};

/* defined in <board>/usb_pd_policy.c */
/* All UFP_U should have */
extern const struct svdm_response svdm_rsp;
/* All DFP_U should have */
extern const struct svdm_amode_fx supported_modes[];
extern const int supported_modes_cnt;

/* 4 entry rw_hash table of type-C devices that AP has firmware updates for. */
#ifdef CONFIG_COMMON_RUNTIME
#define RW_HASH_ENTRIES 4
extern struct ec_params_usb_pd_rw_hash_entry rw_hash_table[RW_HASH_ENTRIES];
#endif /* CONFIG_COMMON_RUNTIME */

/*
 * defined in common/usb_common.c
 * This variable is used in a couple of overridable functions
 * in usb_common code.  If the routines are overridden then
 * this variable should be the same as that used in the common
 * code.
 */
extern uint64_t svdm_hpd_deadline[];

/* DFP data needed to support alternate mode entry and exit */
struct svdm_amode_data {
	const struct svdm_amode_fx *fx;
	/* VDM object position */
	int opos;
	/* mode capabilities specific to SVID amode. */
	struct svid_mode_data *data;
};

enum hpd_event {
	hpd_none,
	hpd_low,
	hpd_high,
	hpd_irq,
};

/* DisplayPort flags */
#define DP_FLAGS_DP_ON              BIT(0) /* Display port mode is on */
#define DP_FLAGS_HPD_HI_PENDING     BIT(1) /* Pending HPD_HI */

/* Discover Identity ACK contents after headers */
union disc_ident_ack {
	struct {
		struct id_header_vdo_rev20 idh;
		struct cert_stat_vdo cert;
		struct product_vdo product;
		union product_type_vdo1 product_t1;
		union product_type_vdo2 product_t2;
		uint32_t product_t3;
	};

	uint32_t raw_value[PDO_MAX_OBJECTS - 1];
};

/* Discover Identity data - ACK plus discovery state */
struct identity_data {
	union disc_ident_ack response;
	enum pd_discovery_state discovery;
};

/* supported alternate modes */
enum pd_alternate_modes {
	PD_AMODE_GOOGLE,
	PD_AMODE_DISPLAYPORT,
	PD_AMODE_INTEL,
	/* not a real mode */
	PD_AMODE_COUNT,
};

/* Discover all SOP* communications when enabled */
#ifdef CONFIG_USB_PD_DECODE_SOP
#define DISCOVERY_TYPE_COUNT (TCPC_TX_SOP_PRIME + 1)
#else
#define DISCOVERY_TYPE_COUNT (TCPC_TX_SOP + 1)
#endif

/* Discovery results for a port partner (SOP) or cable plug (SOP') */
struct pd_discovery {
	/* Identity data */
	union disc_ident_ack identity;
	/* Supported SVIDs and corresponding mode VDOs */
	struct svid_mode_data svids[SVID_DISCOVERY_MAX];
	/*  active modes */
	struct svdm_amode_data amodes[PD_AMODE_COUNT];
	/* index of SVID currently being operated on */
	int svid_idx;
	/* Count of SVIDs discovered */
	int svid_cnt;
	/* Next index to insert DFP alternate mode into amodes */
	int amode_idx;
	/* Identity discovery state */
	enum pd_discovery_state identity_discovery;
	/* SVID discovery state */
	enum pd_discovery_state svids_discovery;
};

/*
 * VDO : Vendor Defined Message Object
 * VDM object is minimum of VDM header + 6 additional data objects.
 */
#define VDO_HDR_SIZE 1
#define VDO_MAX_SIZE 7

#define VDM_VER10 0
#define VDM_VER20 1

#define PD_VDO_INVALID -1

/*
 * VDM header
 * ----------
 * <31:16>  :: SVID
 * <15>     :: VDM type ( 1b == structured, 0b == unstructured )
 * <14:13>  :: Structured VDM version (00b == Rev 2.0, 01b == Rev 3.0 )
 * <12:11>  :: reserved
 * <10:8>   :: object position (1-7 valid ... used for enter/exit mode only)
 * <7:6>    :: command type (SVDM only?)
 * <5>      :: reserved (SVDM), command type (UVDM)
 * <4:0>    :: command
 */
#define VDO(vid, type, custom) \
	(((vid) << 16) |       \
	((type) << 15) |       \
	((custom) & 0x7FFF))

#define VDO_SVDM_TYPE     BIT(15)
#define VDO_SVDM_VERS(x)  (x << 13)
#define VDO_OPOS(x)       (x << 8)
#define VDO_CMDT(x)       (x << 6)
#define VDO_OPOS_MASK     VDO_OPOS(0x7)
#define VDO_CMDT_MASK     VDO_CMDT(0x3)

#define CMDT_INIT     0
#define CMDT_RSP_ACK  1
#define CMDT_RSP_NAK  2
#define CMDT_RSP_BUSY 3


/* reserved for SVDM ... for Google UVDM */
#define VDO_SRC_INITIATOR (0 << 5)
#define VDO_SRC_RESPONDER BIT(5)

#define CMD_DISCOVER_IDENT  1
#define CMD_DISCOVER_SVID   2
#define CMD_DISCOVER_MODES  3
#define CMD_ENTER_MODE      4
#define CMD_EXIT_MODE       5
#define CMD_ATTENTION       6
#define CMD_DP_STATUS      16
#define CMD_DP_CONFIG      17

#define VDO_CMD_VENDOR(x)    (((10 + (x)) & 0x1f))

/* ChromeOS specific commands */
#define VDO_CMD_VERSION      VDO_CMD_VENDOR(0)
#define VDO_CMD_SEND_INFO    VDO_CMD_VENDOR(1)
#define VDO_CMD_READ_INFO    VDO_CMD_VENDOR(2)
#define VDO_CMD_REBOOT       VDO_CMD_VENDOR(5)
#define VDO_CMD_FLASH_ERASE  VDO_CMD_VENDOR(6)
#define VDO_CMD_FLASH_WRITE  VDO_CMD_VENDOR(7)
#define VDO_CMD_ERASE_SIG    VDO_CMD_VENDOR(8)
#define VDO_CMD_PING_ENABLE  VDO_CMD_VENDOR(10)
#define VDO_CMD_CURRENT      VDO_CMD_VENDOR(11)
#define VDO_CMD_FLIP         VDO_CMD_VENDOR(12)
#define VDO_CMD_GET_LOG      VDO_CMD_VENDOR(13)
#define VDO_CMD_CCD_EN       VDO_CMD_VENDOR(14)

#define PD_VDO_VID(vdo)  ((vdo) >> 16)
#define PD_VDO_SVDM(vdo) (((vdo) >> 15) & 1)
#define PD_VDO_OPOS(vdo) (((vdo) >> 8) & 0x7)
#define PD_VDO_CMD(vdo)  ((vdo) & 0x1f)
#define PD_VDO_CMDT(vdo) (((vdo) >> 6) & 0x3)

/*
 * SVDM Identity request -> response
 *
 * Request is simply properly formatted SVDM header
 *
 * Response is 4 data objects.
 * In case of Active cables, the response is 5 data objects:
 * [0] :: SVDM header
 * [1] :: Identitiy header
 * [2] :: Cert Stat VDO
 * [3] :: (Product | Cable) VDO
 * [4] :: AMA VDO
 * [4] :: Product type UFP1 VDO
 * [4] :: Product type Cable VDO 1
 * [5] :: Product type UFP2 VDO
 * [5] :: Product type Cable VDO 2
 * [6] :: Product type DFP VDO
 *
 */
#define VDO_INDEX_HDR            0
#define VDO_INDEX_IDH            1
#define VDO_INDEX_CSTAT          2
#define VDO_INDEX_CABLE          3
#define VDO_INDEX_PRODUCT        3
#define VDO_INDEX_AMA            4
#define VDO_INDEX_PTYPE_UFP1_VDO 4
#define VDO_INDEX_PTYPE_CABLE1   4
#define VDO_INDEX_PTYPE_UFP2_VDO 4
#define VDO_INDEX_PTYPE_CABLE2   5
#define VDO_INDEX_PTYPE_DFP_VDO  6
#define VDO_I(name) VDO_INDEX_##name

#define VDO_IDH(usbh, usbd, ptype, is_modal, vid)		\
	((usbh) << 31 | (usbd) << 30 | ((ptype) & 0x7) << 27	\
	 | (is_modal) << 26 | ((vid) & 0xffff))

#define PD_IDH_PTYPE(vdo)    (((vdo) >> 27) & 0x7)
#define PD_IDH_IS_MODAL(vdo) (((vdo) >> 26) & 0x1)
#define PD_IDH_VID(vdo)      ((vdo) & 0xffff)

#define VDO_CSTAT(tid)    ((tid) & 0xfffff)
#define PD_CSTAT_TID(vdo) ((vdo) & 0xfffff)

#define VDO_PRODUCT(pid, bcd) (((pid) & 0xffff) << 16 | ((bcd) & 0xffff))
#define PD_PRODUCT_PID(vdo) (((vdo) >> 16) & 0xffff)

/*
 * Message id starts from 0 to 7. If last_msg_id is initialized to 0,
 * it will lead to repetitive message id with first received packet,
 * so initialize it with an invalid value 0xff.
 */
#define INVALID_MSG_ID_COUNTER 0xff

/* PD Stack Version */
enum pd_stack_version {
	TCPMV1 = 1,
	TCPMV2,
};

/* Protocol revision */
enum pd_rev_type {
	PD_REV10,
	PD_REV20,
	PD_REV30,
};

#ifdef CONFIG_USB_PD_REV30
#define PD_REVISION     PD_REV30
#else
#define PD_REVISION     PD_REV20
#endif

#if defined(CONFIG_USB_PD_TCPMV1)
#define PD_STACK_VERSION TCPMV1
#elif defined(CONFIG_USB_PD_TCPMV2)
#define PD_STACK_VERSION TCPMV2
#endif

/* Cable structure for storing cable attributes */
struct pd_cable {
	/* Note: the following fields are used by TCPMv1 */
	/* Last received SOP' message id counter*/
	uint8_t last_sop_p_msg_id;
	/* Last received SOP'' message id counter*/
	uint8_t last_sop_p_p_msg_id;
	/* Cable flags. See CABLE_FLAGS_* */
	uint8_t flags;
	/* For storing Discover mode response from device */
	union tbt_mode_resp_device dev_mode_resp;
	/* For storing Discover mode response from cable */
	union tbt_mode_resp_cable cable_mode_resp;

	/* Shared fields between TCPMv1 and TCPMv2 */
	uint8_t is_identified;
	/* Type of cable */
	enum idh_ptype type;
	/* Cable attributes */
	union product_type_vdo1 attr;
	/* For USB PD REV3, active cable has 2 VDOs */
	union product_type_vdo2 attr2;
	/* Cable revision */
	enum pd_rev_type rev;

};

/* Note: These flags are only used for TCPMv1 */
/* Flag for sending SOP Prime packet */
#define CABLE_FLAGS_SOP_PRIME_ENABLE	   BIT(0)
/* Flag for sending SOP Prime Prime packet */
#define CABLE_FLAGS_SOP_PRIME_PRIME_ENABLE BIT(1)
/* Check if Thunderbolt-compatible mode enabled */
#define CABLE_FLAGS_TBT_COMPAT_ENABLE	   BIT(2)
/* Flag to limit speed to TBT Gen 2 passive cable */
#define CABLE_FLAGS_TBT_COMPAT_LIMIT_SPEED BIT(3)
/* Flag for checking if device is USB4.0 capable */
#define CABLE_FLAGS_USB4_CAPABLE           BIT(4)
/* Flag for entering ENTER_USB mode */
#define CABLE_FLAGS_ENTER_USB_MODE         BIT(5)

/*
 * SVDM Discover SVIDs request -> response
 *
 * Request is properly formatted VDM Header with discover SVIDs command.
 * Response is a set of SVIDs of all all supported SVIDs with all zero's to
 * mark the end of SVIDs.  If more than 12 SVIDs are supported command SHOULD be
 * repeated.
 */
#define VDO_SVID(svid0, svid1) (((svid0) & 0xffff) << 16 | ((svid1) & 0xffff))
#define PD_VDO_SVID_SVID0(vdo) ((vdo) >> 16)
#define PD_VDO_SVID_SVID1(vdo) ((vdo) & 0xffff)

/*
 * Google modes capabilities
 * <31:8> : reserved
 * <7:0>  : mode
 */
#define VDO_MODE_GOOGLE(mode) (mode & 0xff)

#define MODE_GOOGLE_FU 1 /* Firmware Update mode */

/*
 * Mode Capabilities
 *
 * Number of VDOs supplied is SID dependent (but <= 6 VDOS?)
 */
#define VDO_MODE_CNT_DISPLAYPORT 1

/*
 * DisplayPort modes capabilities
 * -------------------------------
 * <31:24> : SBZ
 * <23:16> : UFP_D pin assignment supported
 * <15:8>  : DFP_D pin assignment supported
 * <7>     : USB 2.0 signaling (0b=yes, 1b=no)
 * <6>     : Plug | Receptacle (0b == plug, 1b == receptacle)
 * <5:2>   : xxx1: Supports DPv1.3, xx1x Supports USB Gen 2 signaling
 *           Other bits are reserved.
 * <1:0>   : signal direction ( 00b=rsv, 01b=sink, 10b=src 11b=both )
 */
#define VDO_MODE_DP(snkp, srcp, usb, gdr, sign, sdir)			\
	(((snkp) & 0xff) << 16 | ((srcp) & 0xff) << 8			\
	 | ((usb) & 1) << 7 | ((gdr) & 1) << 6 | ((sign) & 0xF) << 2	\
	 | ((sdir) & 0x3))

#define MODE_DP_PIN_A 0x01
#define MODE_DP_PIN_B 0x02
#define MODE_DP_PIN_C 0x04
#define MODE_DP_PIN_D 0x08
#define MODE_DP_PIN_E 0x10
#define MODE_DP_PIN_F 0x20
#define MODE_DP_PIN_ALL 0x3f

#define MODE_DP_DFP_PIN_SHIFT 8
#define MODE_DP_UFP_PIN_SHIFT 16

/* Pin configs B/D/F support multi-function */
#define MODE_DP_PIN_MF_MASK 0x2a
/* Pin configs A/B support BR2 signaling levels */
#define MODE_DP_PIN_BR2_MASK 0x3
/* Pin configs C/D/E/F support DP signaling levels */
#define MODE_DP_PIN_DP_MASK 0x3c
/* Pin configs A/B/C/D/E/F */
#define MODE_DP_PIN_CAPS_MASK 0x3f

#define MODE_DP_V13  0x1
#define MODE_DP_GEN2 0x2

#define MODE_DP_SNK  0x1
#define MODE_DP_SRC  0x2
#define MODE_DP_BOTH 0x3

#define MODE_DP_CABLE_SHIFT 6

/*
 * Determine which pin assignments are valid for DP
 *
 * Based on whether the DP adapter identifies itself as a plug (permanently
 * attached cable) or a receptacle, the pin assignments may be in the DFP_D
 * field or the UFP_D field.
 *
 * Refer to DisplayPort Alt Mode On USB Type-C Standard version 1.0, table 5-2
 * depending on state of receptacle bit, use pins for DFP_D (if receptacle==0)
 * or UFP_D (if receptacle==1)
 * Also refer to DisplayPort Alt Mode Capabilities Clarification (4/30/2015)
 */
#define PD_DP_PIN_CAPS(x) ((((x) >> MODE_DP_CABLE_SHIFT) & 0x1) \
	? (((x) >> MODE_DP_UFP_PIN_SHIFT) & MODE_DP_PIN_CAPS_MASK) \
	: (((x) >> MODE_DP_DFP_PIN_SHIFT) & MODE_DP_PIN_CAPS_MASK))

/*
 * DisplayPort Status VDO
 * ----------------------
 * <31:9> : SBZ
 * <8>    : IRQ_HPD : 1 == irq arrived since last message otherwise 0.
 * <7>    : HPD state : 0 = HPD_LOW, 1 == HPD_HIGH
 * <6>    : Exit DP Alt mode: 0 == maintain, 1 == exit
 * <5>    : USB config : 0 == maintain current, 1 == switch to USB from DP
 * <4>    : Multi-function preference : 0 == no pref, 1 == MF preferred.
 * <3>    : enabled : is DPout on/off.
 * <2>    : power low : 0 == normal or LPM disabled, 1 == DP disabled for LPM
 * <1:0>  : connect status : 00b ==  no (DFP|UFP)_D is connected or disabled.
 *          01b == DFP_D connected, 10b == UFP_D connected, 11b == both.
 */
#define VDO_DP_STATUS(irq, lvl, amode, usbc, mf, en, lp, conn)		\
	(((irq) & 1) << 8 | ((lvl) & 1) << 7 | ((amode) & 1) << 6	\
	 | ((usbc) & 1) << 5 | ((mf) & 1) << 4 | ((en) & 1) << 3	\
	 | ((lp) & 1) << 2 | ((conn & 0x3) << 0))

#define PD_VDO_DPSTS_MF_MASK BIT(4)

#define PD_VDO_DPSTS_HPD_IRQ(x) (((x) >> 8) & 1)
#define PD_VDO_DPSTS_HPD_LVL(x) (((x) >> 7) & 1)
#define PD_VDO_DPSTS_MF_PREF(x) (((x) >> 4) & 1)

/* Per DisplayPort Spec v1.3 Section 3.3 */
#define HPD_USTREAM_DEBOUNCE_LVL (2*MSEC)
#define HPD_USTREAM_DEBOUNCE_IRQ (250)
#define HPD_DSTREAM_DEBOUNCE_IRQ (500)  /* between 500-1000us */

/*
 * DisplayPort Configure VDO
 * -------------------------
 * <31:24> : SBZ
 * <23:16> : SBZ
 * <15:8>  : Pin assignment requested.  Choose one from mode caps.
 * <7:6>   : SBZ
 * <5:2>   : signalling : 1h == DP v1.3, 2h == Gen 2
 *           Oh is only for USB, remaining values are reserved
 * <1:0>   : cfg : 00 == USB, 01 == DFP_D, 10 == UFP_D, 11 == reserved
 */
#define VDO_DP_CFG(pin, sig, cfg) \
	(((pin) & 0xff) << 8 | ((sig) & 0xf) << 2 | ((cfg) & 0x3))

#define PD_DP_CFG_DPON(x) (((x & 0x3) == 1) || ((x & 0x3) == 2))
/*
 * Get the pin assignment mask
 * for backward compatibility, if it is null,
 * get the former sink pin assignment we used to be in <23:16>.
 */
#define PD_DP_CFG_PIN(x) ((((x) >> 8) & 0xff) ? (((x) >> 8) & 0xff) \
					      : (((x) >> 16) & 0xff))
/*
 * ChromeOS specific PD device Hardware IDs. Used to identify unique
 * products and used in VDO_INFO. Note this field is 10 bits.
 */
#define USB_PD_HW_DEV_ID_RESERVED    0
#define USB_PD_HW_DEV_ID_ZINGER      1
#define USB_PD_HW_DEV_ID_MINIMUFFIN  2
#define USB_PD_HW_DEV_ID_DINGDONG    3
#define USB_PD_HW_DEV_ID_HOHO        4
#define USB_PD_HW_DEV_ID_HONEYBUNS   5

/*
 * ChromeOS specific VDO_CMD_READ_INFO responds with device info including:
 * RW Hash: First 20 bytes of SHA-256 of RW (20 bytes)
 * HW Device ID: unique descriptor for each ChromeOS model (2 bytes)
 *               top 6 bits are minor revision, bottom 10 bits are major
 * SW Debug Version: Software version useful for debugging (15 bits)
 * IS RW: True if currently in RW, False otherwise (1 bit)
 */
#define VDO_INFO(id, id_minor, ver, is_rw) ((id_minor) << 26 \
				  | ((id) & 0x3ff) << 16 \
				  | ((ver) & 0x7fff) << 1 \
				  | ((is_rw) & 1))
#define VDO_INFO_HW_DEV_ID(x)    ((x) >> 16)
#define VDO_INFO_SW_DBG_VER(x)   (((x) >> 1) & 0x7fff)
#define VDO_INFO_IS_RW(x)        ((x) & 1)

#define HW_DEV_ID_MAJ(x) (x & 0x3ff)
#define HW_DEV_ID_MIN(x) ((x) >> 10)

/* USB-IF SIDs */
#define USB_SID_PD          0xff00 /* power delivery */
#define USB_SID_DISPLAYPORT 0xff01

#define USB_GOOGLE_TYPEC_URL "http://www.google.com/chrome/devices/typec"
/* USB Vendor ID assigned to Google Inc. */
#define USB_VID_GOOGLE 0x18d1

/* Other Vendor IDs */
#define USB_VID_APPLE  0x05ac
#define USB_PID1_APPLE 0x1012
#define USB_PID2_APPLE 0x1013
#define USB_VID_INTEL  0x8087

/* Timeout for message receive in microseconds */
#define USB_PD_RX_TMOUT_US 1800

/* --- Protocol layer functions --- */

enum pd_states {
	PD_STATE_DISABLED,			/* C0  */
	PD_STATE_SUSPENDED,			/* C1  */
	PD_STATE_SNK_DISCONNECTED,		/* C2  */
	PD_STATE_SNK_DISCONNECTED_DEBOUNCE,	/* C3  */
	PD_STATE_SNK_HARD_RESET_RECOVER,	/* C4  */
	PD_STATE_SNK_DISCOVERY,			/* C5  */
	PD_STATE_SNK_REQUESTED,			/* C6  */
	PD_STATE_SNK_TRANSITION,		/* C7  */
	PD_STATE_SNK_READY,			/* C8  */
	PD_STATE_SNK_SWAP_INIT,			/* C9  */
	PD_STATE_SNK_SWAP_SNK_DISABLE,		/* C10 */
	PD_STATE_SNK_SWAP_SRC_DISABLE,		/* C11 */
	PD_STATE_SNK_SWAP_STANDBY,		/* C12 */
	PD_STATE_SNK_SWAP_COMPLETE,		/* C13 */
	PD_STATE_SRC_DISCONNECTED,		/* C14 */
	PD_STATE_SRC_DISCONNECTED_DEBOUNCE,	/* C15 */
	PD_STATE_SRC_HARD_RESET_RECOVER,	/* C16 */
	PD_STATE_SRC_STARTUP,			/* C17 */
	PD_STATE_SRC_DISCOVERY,			/* C18 */
	PD_STATE_SRC_NEGOCIATE,			/* C19 */
	PD_STATE_SRC_ACCEPTED,			/* C20 */
	PD_STATE_SRC_POWERED,			/* C21 */
	PD_STATE_SRC_TRANSITION,		/* C22 */
	PD_STATE_SRC_READY,			/* C23 */
	PD_STATE_SRC_GET_SINK_CAP,		/* C24 */
	PD_STATE_DR_SWAP,			/* C25 */
	PD_STATE_SRC_SWAP_INIT,			/* C26 */
	PD_STATE_SRC_SWAP_SNK_DISABLE,		/* C27 */
	PD_STATE_SRC_SWAP_SRC_DISABLE,		/* C28 */
	PD_STATE_SRC_SWAP_STANDBY,		/* C29 */
	PD_STATE_VCONN_SWAP_SEND,		/* C30 */
	PD_STATE_VCONN_SWAP_INIT,		/* C31 */
	PD_STATE_VCONN_SWAP_READY,		/* C32 */
	PD_STATE_SOFT_RESET,			/* C33 */
	PD_STATE_HARD_RESET_SEND,		/* C34 */
	PD_STATE_HARD_RESET_EXECUTE,		/* C35 */
	PD_STATE_BIST_RX,			/* C36 */
	PD_STATE_BIST_TX,			/* C37 */
	PD_STATE_DRP_AUTO_TOGGLE,		/* C38 */
	PD_STATE_ENTER_USB,			/* C39 */
	/* Number of states. Not an actual state. */
	PD_STATE_COUNT,
};

/* Generate compile-time errors for unsupported states */
#ifndef CONFIG_USB_PD_DUAL_ROLE
#define PD_STATE_SNK_DISCONNECTED UNSUPPORTED_PD_STATE_SNK_DISCONNECTED
#define PD_STATE_SNK_DISCONNECTED_DEBOUNCE UNSUPPORTED_SNK_DISCONNECTED_DEBOUNCE
#define PD_STATE_SNK_HARD_RESET_RECOVER UNSUPPORTED_SNK_HARD_RESET_RECOVER
#define PD_STATE_SNK_DISCOVERY UNSUPPORTED_PD_STATE_SNK_DISCOVERY
#define PD_STATE_SNK_REQUESTED UNSUPPORTED_PD_STATE_SNK_REQUESTED
#define PD_STATE_SNK_TRANSITION UNSUPPORTED_PD_STATE_SNK_TRANSITION
#define PD_STATE_SNK_READY UNSUPPORTED_PD_STATE_SNK_READY
#define PD_STATE_SNK_SWAP_INIT UNSUPPORTED_PD_STATE_SNK_SWAP_INIT
#define PD_STATE_SNK_SWAP_SNK_DISABLE UNSUPPORTED_PD_STATE_SNK_SWAP_SNK_DISABLE
#define PD_STATE_SNK_SWAP_SRC_DISABLE UNSUPPORTED_PD_STATE_SNK_SWAP_SRC_DISABLE
#define PD_STATE_SNK_SWAP_STANDBY UNSUPPORTED_PD_STATE_SNK_SWAP_STANDBY
#define PD_STATE_SNK_SWAP_COMPLETE UNSUPPORTED_PD_STATE_SNK_SWAP_COMPLETE
#define PD_STATE_SRC_SWAP_INIT UNSUPPORTED_PD_STATE_SRC_SWAP_INIT
#define PD_STATE_SRC_SWAP_SNK_DISABLE UNSUPPORTED_PD_STATE_SRC_SWAP_SNK_DISABLE
#define PD_STATE_SRC_SWAP_SRC_DISABLE UNSUPPORTED_PD_STATE_SRC_SWAP_SRC_DISABLE
#define PD_STATE_SRC_SWAP_STANDBY UNSUPPORTED_PD_STATE_SRC_SWAP_STANDBY
#endif /* CONFIG_USB_PD_DUAL_ROLE */

/* Generate compile-time errors for unsupported states */
#if !defined(CONFIG_USBC_VCONN_SWAP) || !defined(CONFIG_USB_PD_DUAL_ROLE)
#define PD_STATE_VCONN_SWAP_SEND UNSUPPORTED_PD_STATE_VCONN_SWAP_SEND
#define PD_STATE_VCONN_SWAP_INIT UNSUPPORTED_PD_STATE_VCONN_SWAP_INIT
#define PD_STATE_VCONN_SWAP_READY UNSUPPORTED_PD_STATE_VCONN_SWAP_READY
#endif

/* Generate compile-time errors for unsupported states */
#ifndef CONFIG_COMMON_RUNTIME
#define PD_STATE_BIST_RX UNSUPPORTED_PD_STATE_BIST_RX
#define PD_STATE_BIST_TX UNSUPPORTED_PD_STATE_BIST_TX
#endif

/* Generate compile-time errors for unsupported states */
#ifndef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define PD_STATE_DRP_AUTO_TOGGLE UNSUPPORTED_PD_STATE_DRP_AUTO_TOGGLE
#endif

#ifdef CONFIG_USB_PD_TCPMV1
/* Flags used for TCPMv1 */
#define PD_FLAGS_PING_ENABLED      BIT(0) /* SRC_READY pings enabled */
#define PD_FLAGS_PARTNER_DR_POWER  BIT(1) /* port partner is dualrole power */
#define PD_FLAGS_PARTNER_DR_DATA   BIT(2) /* port partner is dualrole data */
#define PD_FLAGS_CHECK_IDENTITY    BIT(3) /* discover identity in READY */
#define PD_FLAGS_SNK_CAP_RECVD     BIT(4) /* sink capabilities received */
#define PD_FLAGS_TCPC_DRP_TOGGLE   BIT(5) /* TCPC-controlled DRP toggling */
#define PD_FLAGS_EXPLICIT_CONTRACT BIT(6) /* explicit pwr contract in place */
#define PD_FLAGS_VBUS_NEVER_LOW    BIT(7) /* VBUS input has never been low */
#define PD_FLAGS_PREVIOUS_PD_CONN  BIT(8) /* previously PD connected */
#define PD_FLAGS_CHECK_PR_ROLE     BIT(9) /* check power role in READY */
#define PD_FLAGS_CHECK_DR_ROLE     BIT(10)/* check data role in READY */
#define PD_FLAGS_PARTNER_UNCONSTR  BIT(11)/* port partner unconstrained pwr */
#define PD_FLAGS_VCONN_ON          BIT(12)/* vconn is being sourced */
#define PD_FLAGS_TRY_SRC           BIT(13)/* Try.SRC states are active */
#define PD_FLAGS_PARTNER_USB_COMM  BIT(14)/* port partner is USB comms */
#define PD_FLAGS_UPDATE_SRC_CAPS   BIT(15)/* send new source capabilities */
#define PD_FLAGS_TS_DTS_PARTNER    BIT(16)/* partner has rp/rp or rd/rd */
/*
 * These PD_FLAGS_LPM* flags track the software state (PD_LPM_FLAGS_REQUESTED)
 * and hardware state (PD_LPM_FLAGS_ENGAGED) of the TCPC low power mode.
 * PD_FLAGS_LPM_TRANSITION is set while the HW is transitioning into or out of
 * low power (when PD_LPM_FLAGS_ENGAGED is changing).
 */
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
#define PD_FLAGS_LPM_REQUESTED     BIT(17)/* Tracks SW LPM state */
#define PD_FLAGS_LPM_ENGAGED       BIT(18)/* Tracks HW LPM state */
#define PD_FLAGS_LPM_TRANSITION    BIT(19)/* Tracks HW LPM transition */
#endif
/*
 * Tracks whether port negotiation may have stalled due to not starting reset
 * timers in SNK_DISCOVERY
 */
#define PD_FLAGS_SNK_WAITING_BATT  BIT(20)
/* Check vconn state in READY */
#define PD_FLAGS_CHECK_VCONN_STATE BIT(21)
#endif /* CONFIG_USB_PD_TCPMV1 */

/* Per-port battery backed RAM flags */
#define PD_BBRMFLG_EXPLICIT_CONTRACT BIT(0)
#define PD_BBRMFLG_POWER_ROLE        BIT(1)
#define PD_BBRMFLG_DATA_ROLE         BIT(2)
#define PD_BBRMFLG_VCONN_ROLE        BIT(3)
#define PD_BBRMFLG_DBGACC_ROLE       BIT(4)

/* Initial value for CC debounce variable */
#define PD_CC_UNSET -1

enum pd_dual_role_states {
	/* While disconnected, toggle between src and sink */
	PD_DRP_TOGGLE_ON,
	/* Stay in src until disconnect, then stay in sink forever */
	PD_DRP_TOGGLE_OFF,
	/* Stay in current power role, don't switch. No auto-toggle support */
	PD_DRP_FREEZE,
	/* Switch to sink */
	PD_DRP_FORCE_SINK,
	/* Switch to source */
	PD_DRP_FORCE_SOURCE,
};
/**
 * Get dual role state
 *
 * @param port Port number from which to get state
 * @return Current dual-role state, from enum pd_dual_role_states
 */
enum pd_dual_role_states pd_get_dual_role(int port);
/**
 * Set dual role state, from among enum pd_dual_role_states
 *
 * @param port Port number of which to set state
 * @param state New state of dual-role port, selected from
 *              enum pd_dual_role_states
 */
void pd_set_dual_role(int port, enum pd_dual_role_states state);

/**
 * Get current data role
 *
 * @param port Port number from which to get role
 */
enum pd_data_role pd_get_data_role(int port);

/**
 * Get current power role
 *
 * @param port Port number from which to get power role
 */
enum pd_power_role pd_get_power_role(int port);

/*
 * Return true if PD is capable of trying as source else false
 */
bool pd_is_try_source_capable(void);

/**
 * Request for VCONN swap
 *
 * @param port USB-C Port number
 */
void pd_request_vconn_swap(int port);

/**
 * Get the current CC line states from PD task
 *
 * @param port USB-C Port number
 * @return CC state
 */
enum pd_cc_states pd_get_task_cc_state(int port);

/**
 * Get the current PD state of USB-C port
 *
 * @param port USB-C Port number
 * @return PD state
 * Note: TCPMv1 returns enum pd_states
 *       TCPMv2 returns enum usb_tc_state
 */
uint8_t pd_get_task_state(int port);

/**
 * Get the current PD state name of USB-C port
 *
 * @param port USB-C Port number
 * @return Pointer to PD state name
 */
const char *pd_get_task_state_name(int port);

/**
 * Get current VCONN state of USB-C port
 *
 * @param port USB-C Port number
 * @return true if VCONN is on else false
 */
bool pd_get_vconn_state(int port);

/**
 * Check if port partner is dual role power
 *
 * @param port USB-C Port number
 * @return true if partner is dual role power else false
 */
bool pd_get_partner_dual_role_power(int port);

/**
 * Check if port partner is unconstrained power
 *
 * @param port USB-C Port number
 * @return true if partner is unconstrained power else false
 */
bool pd_get_partner_unconstr_power(int port);

/* Control Message type */
enum pd_ctrl_msg_type {
	/* 0 Reserved */
	PD_CTRL_GOOD_CRC = 1,
	PD_CTRL_GOTO_MIN = 2,
	PD_CTRL_ACCEPT = 3,
	PD_CTRL_REJECT = 4,
	PD_CTRL_PING = 5,
	PD_CTRL_PS_RDY = 6,
	PD_CTRL_GET_SOURCE_CAP = 7,
	PD_CTRL_GET_SINK_CAP = 8,
	PD_CTRL_DR_SWAP = 9,
	PD_CTRL_PR_SWAP = 10,
	PD_CTRL_VCONN_SWAP = 11,
	PD_CTRL_WAIT = 12,
	PD_CTRL_SOFT_RESET = 13,
	/* 14-15 Reserved */

	/* Used for REV 3.0 */
	PD_CTRL_NOT_SUPPORTED = 16,
	PD_CTRL_GET_SOURCE_CAP_EXT = 17,
	PD_CTRL_GET_STATUS = 18,
	PD_CTRL_FR_SWAP = 19,
	PD_CTRL_GET_PPS_STATUS = 20,
	PD_CTRL_GET_COUNTRY_CODES = 21,
	/* 22-31 Reserved */
};

/* Control message types which always mark the start of an AMS */
#define PD_CTRL_AMS_START_MASK ((1 << PD_CTRL_GOTO_MIN) | \
				(1 << PD_CTRL_GET_SOURCE_CAP) | \
				(1 << PD_CTRL_GET_SINK_CAP) | \
				(1 << PD_CTRL_DR_SWAP) | \
				(1 << PD_CTRL_PR_SWAP) | \
				(1 << PD_CTRL_VCONN_SWAP) | \
				(1 << PD_CTRL_GET_SOURCE_CAP_EXT) | \
				(1 << PD_CTRL_GET_STATUS) | \
				(1 << PD_CTRL_FR_SWAP) | \
				(1 << PD_CTRL_GET_PPS_STATUS) | \
				(1 << PD_CTRL_GET_COUNTRY_CODES))


/* Battery Status Data Object fields for REV 3.0 */
#define BSDO_CAP_UNKNOWN 0xffff
#define BSDO_CAP(n)      (((n) & 0xffff) << 16)
#define BSDO_INVALID     BIT(8)
#define BSDO_PRESENT     BIT(9)
#define BSDO_DISCHARGING BIT(10)
#define BSDO_IDLE        BIT(11)

/* Get Battery Cap Message fields for REV 3.0 */
#define BATT_CAP_REF(n)  (((n) >> 16) & 0xff)

/* Extended message type for REV 3.0 */
enum pd_ext_msg_type {
	/* 0 Reserved */
	PD_EXT_SOURCE_CAP = 1,
	PD_EXT_STATUS = 2,
	PD_EXT_GET_BATTERY_CAP = 3,
	PD_EXT_GET_BATTERY_STATUS = 4,
	PD_EXT_BATTERY_CAP = 5,
	PD_EXT_GET_MANUFACTURER_INFO = 6,
	PD_EXT_MANUFACTURER_INFO = 7,
	PD_EXT_SECURITY_REQUEST = 8,
	PD_EXT_SECURITY_RESPONSE = 9,
	PD_EXT_FIRMWARE_UPDATE_REQUEST = 10,
	PD_EXT_FIRMWARE_UPDATE_RESPONSE = 11,
	PD_EXT_PPS_STATUS = 12,
	PD_EXT_COUNTRY_INFO = 13,
	PD_EXT_COUNTRY_CODES = 14,
	/* 15-31 Reserved */
};

/* Data message type */
enum pd_data_msg_type {
	/* 0 Reserved */
	PD_DATA_SOURCE_CAP = 1,
	PD_DATA_REQUEST = 2,
	PD_DATA_BIST = 3,
	PD_DATA_SINK_CAP = 4,
	/* 5-14 Reserved for REV 2.0 */
	PD_DATA_BATTERY_STATUS = 5,
	PD_DATA_ALERT = 6,
	PD_DATA_GET_COUNTRY_INFO = 7,
	/* 8-14 Reserved for REV 3.0 */
	PD_DATA_ENTER_USB = 8,
	PD_DATA_VENDOR_DEF = 15,
};

/*
 * Power role. See 6.2.1.1.4 Port Power Role. Only applies to SOP packets.
 * Replaced by pd_cable_plug for SOP' and SOP" packets.
 */
enum pd_power_role {
	PD_ROLE_SINK = 0,
	PD_ROLE_SOURCE = 1
};

/*
 * Data role. See 6.2.1.1.6 Port Data Role. Only applies to SOP.
 * Replaced by reserved field for SOP' and SOP" packets.
 */
enum pd_data_role {
	PD_ROLE_UFP = 0,
	PD_ROLE_DFP = 1,
	PD_ROLE_DISCONNECTED = 2,
};

/*
 * Cable plug. See 6.2.1.1.7 Cable Plug. Only applies to SOP' and SOP".
 * Replaced by pd_power_role for SOP packets.
 */
enum pd_cable_plug {
	PD_PLUG_FROM_DFP_UFP = 0,
	PD_PLUG_FROM_CABLE = 1
};

enum cable_outlet {
	CABLE_PLUG = 0,
	CABLE_RECEPTACLE = 1,
};

/* Vconn role */
#define PD_ROLE_VCONN_OFF 0
#define PD_ROLE_VCONN_ON  1

/* chunk is a request or response in REV 3.0 */
#define CHUNK_RESPONSE 0
#define CHUNK_REQUEST  1

/* collision avoidance Rp values in REV 3.0 */
#define SINK_TX_OK TYPEC_RP_3A0
#define SINK_TX_NG TYPEC_RP_1A5

/* Port role at startup */
#ifndef PD_ROLE_DEFAULT
#ifdef CONFIG_USB_PD_DUAL_ROLE
#define PD_ROLE_DEFAULT(port) PD_ROLE_SINK
#else
#define PD_ROLE_DEFAULT(port) PD_ROLE_SOURCE
#endif
#endif

/* Port default state at startup */
#ifdef CONFIG_USB_PD_DUAL_ROLE
#define PD_DEFAULT_STATE(port) ((PD_ROLE_DEFAULT(port) == PD_ROLE_SOURCE) ? \
				PD_STATE_SRC_DISCONNECTED :	      \
				PD_STATE_SNK_DISCONNECTED)
#else
#define PD_DEFAULT_STATE(port) PD_STATE_SRC_DISCONNECTED
#endif

/* build extended message header */
/* All extended messages are chunked, so set bit 15 */
#define PD_EXT_HEADER(cnum, rchk, dsize) \
	 (BIT(15) | ((cnum) << 11) | \
	 ((rchk) << 10) | (dsize))

/* build message header */
#define PD_HEADER(type, prole, drole, id, cnt, rev, ext) \
	((type) | ((rev) << 6) | \
	((drole) << 5) | ((prole) << 8) | \
	((id) << 9) | ((cnt) << 12) | ((ext) << 15))

/* Used for processing pd header */
#define PD_HEADER_EXT(header)   (((header) >> 15) & 1)
#define PD_HEADER_CNT(header)   (((header) >> 12) & 7)
/*
 * NOTE: bit 4 was added in PD 3.0, and should be reserved and set to 0 in PD
 * 2.0 messages
 */
#define PD_HEADER_TYPE(header)  ((header) & 0x1F)
#define PD_HEADER_ID(header)    (((header) >> 9) & 7)
#define PD_HEADER_PROLE(header) (((header) >> 8) & 1)
#define PD_HEADER_REV(header)   (((header) >> 6) & 3)
#define PD_HEADER_DROLE(header) (((header) >> 5) & 1)

/*
 * The message header is a 16-bit value that's stored in a 32-bit data type.
 * SOP* is encoded in bits 31 to 28 of the 32-bit data type.
 * NOTE: This is not part of the PD spec.
 */
#define PD_HEADER_GET_SOP(header) (((header) >> 28) & 0xf)
#define PD_HEADER_SOP(sop) (((sop) & 0xf) << 28)

enum pd_msg_type {
	PD_MSG_SOP,
	PD_MSG_SOP_PRIME,
	PD_MSG_SOP_PRIME_PRIME,
	PD_MSG_SOP_DBG_PRIME,
	PD_MSG_SOP_DBG_PRIME_PRIME,
	PD_MSG_SOP_CBL_RST,
};

/* Used for processing pd extended header */
#define PD_EXT_HEADER_CHUNKED(header)   (((header) >> 15) & 1)
#define PD_EXT_HEADER_CHUNK_NUM(header) (((header) >> 11) & 0xf)
#define PD_EXT_HEADER_REQ_CHUNK(header) (((header) >> 10) & 1)
#define PD_EXT_HEADER_DATA_SIZE(header) ((header) & 0x1ff)

/* Used to get extended header from the first 32-bit word of the message */
#define GET_EXT_HEADER(msg) (msg & 0xffff)

/* K-codes for special symbols */
#define PD_SYNC1 0x18
#define PD_SYNC2 0x11
#define PD_SYNC3 0x06
#define PD_RST1  0x07
#define PD_RST2  0x19
#define PD_EOP   0x0D

/* Minimum PD supply current  (mA) */
#define PD_MIN_MA	500

/* Minimum PD voltage (mV) */
#define PD_MIN_MV	5000

/* No connect voltage threshold for sources based on Rp */
#define PD_SRC_DEF_VNC_MV        1600
#define PD_SRC_1_5_VNC_MV        1600
#define PD_SRC_3_0_VNC_MV        2600

/* Rd voltage threshold for sources based on Rp */
#define PD_SRC_DEF_RD_THRESH_MV  200
#define PD_SRC_1_5_RD_THRESH_MV  400
#define PD_SRC_3_0_RD_THRESH_MV  800

/* Voltage threshold to detect connection when presenting Rd */
#define PD_SNK_VA_MV             250

/* --- Policy layer functions --- */

/** Schedules the interrupt handler for the TCPC on a high priority task. */
void schedule_deferred_pd_interrupt(int port);

/**
 * Get current PD VDO Version
 *
 * @param port USB-C port number
 * @param type USB-C port partner
 * @return 0 for PD_REV1.0, 1 for PD_REV2.0
 */
int pd_get_vdo_ver(int port, enum tcpm_transmit_type type);

/**
 * Check if max voltage request is allowed (only used if
 * CONFIG_USB_PD_CHECK_MAX_REQUEST_ALLOWED is defined).
 *
 * @return True if max voltage request allowed, False otherwise
 */
int pd_is_max_request_allowed(void);

/**
 * Waits for the TCPC to exit low power mode (including re-initializing) if it
 * is currently in low power mode. If not, then the function immediately
 * returns.
 *
 * @param port USB-C port number
 */
void pd_wait_exit_low_power(int port);

/**
 * Informs the TCPM state machine that code within the EC has accessed the TCPC
 * via its communication bus (e.g. i2c). This is important to keep track of as
 * accessing a TCPC may pull the hardware out of low-power mode.
 *
 * Note: Call this function after finished accessing the hardware.
 *
 * @param port USB-C port number
 */
void pd_device_accessed(int port);

/**
 * Prevents the TCPC from going back into low power mode. Invocations must be
 * called in a pair from the same task, otherwise the TCPC will never re-enter
 * low power mode.
 *
 * Note: This will not wake the device up if it is in LPM.
 *
 * @param port USB-C port number
 * @param prevent 1 to prevent this port from entering LPM
 */
void pd_prevent_low_power_mode(int port, int prevent);

/**
 * Process source capabilities packet
 *
 * @param port USB-C port number
 * @param cnt  the number of Power Data Objects.
 * @param src_caps Power Data Objects representing the source capabilities.
 */
void pd_process_source_cap(int port, int cnt, uint32_t *src_caps);

/**
 * Reduce the sink power consumption to a minimum value.
 *
 * @param port USB-C port number
 * @param ma reduce current to minimum value.
 * @param mv reduce voltage to minimum value.
 */
void pd_snk_give_back(int port, uint32_t * const ma, uint32_t * const mv);

/**
 * Put a cap on the max voltage requested as a sink.
 * @param mv maximum voltage in millivolts.
 */
void pd_set_max_voltage(unsigned mv);

/**
 * Get the max voltage that can be requested as set by pd_set_max_voltage().
 * @return max voltage
 */
unsigned pd_get_max_voltage(void);

/**
 * Check if this board supports the given input voltage.
 *
 * @mv input voltage
 * @return 1 if voltage supported, 0 if not
 */
__override_proto int pd_is_valid_input_voltage(int mv);

/**
 * Request a new operating voltage.
 *
 * @param rdo  Request Data Object with the selected operating point.
 * @param port The port which the request came in on.
 * @return EC_SUCCESS if we can get the requested voltage/OP, <0 else.
 */
int pd_check_requested_voltage(uint32_t rdo, const int port);

/**
 * Run board specific checks on request message
 *
 * @param rdo the request data object word sent by the sink.
 * @param pdo_cnt the total number of source PDOs.
 * @return EC_SUCCESS if request is ok , <0 else.
 */
__override_proto int pd_board_check_request(uint32_t rdo, int pdo_cnt);

/**
 * Select a new output voltage.
 *
 * param idx index of the new voltage in the source PDO table.
 */
__override_proto void pd_transition_voltage(int idx);

/**
 * Go back to the default/safe state of the power supply
 *
 * @param port USB-C port number
 */
void pd_power_supply_reset(int port);

/**
 * Enable or disable VBUS discharge for a given port.
 *
 * @param port USB-C port number
 * @enable 1 if enabling discharge, 0 if disabling
 */
void pd_set_vbus_discharge(int port, int enable);

/**
 * Enable the power supply output after the ready delay.
 *
 * @param port USB-C port number
 * @return EC_SUCCESS if the power supply is ready, <0 else.
 */
int pd_set_power_supply_ready(int port);

/**
 * Ask the specified voltage from the PD source.
 *
 * It triggers a new negotiation sequence with the source.
 * @param port USB-C port number
 * @param mv request voltage in millivolts.
 */
void pd_request_source_voltage(int port, int mv);

/**
 * Set a voltage limit from the PD source.
 *
 * If the source is currently active, it triggers a new negotiation.
 * @param port USB-C port number
 * @param mv limit voltage in millivolts.
 */
void pd_set_external_voltage_limit(int port, int mv);

/**
 * Set the PD input current limit.
 *
 * @param port USB-C port number
 * @param max_ma Maximum current limit
 * @param supply_voltage Voltage at which current limit is applied
 */
void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage);


/**
 * Update the power contract if it exists.
 *
 * @param port USB-C port number.
 */
void pd_update_contract(int port);

/* Encode DTS status of port partner in current limit parameter */
typedef uint32_t typec_current_t;
#define TYPEC_CURRENT_DTS_MASK BIT(31)
#define TYPEC_CURRENT_ILIM_MASK (~TYPEC_CURRENT_DTS_MASK)

/**
 * Set the type-C input current limit.
 *
 * @param port USB-C port number
 * @param max_ma Maximum current limit
 * @param supply_voltage Voltage at which current limit is applied
 */
__override_proto void typec_set_input_current_limit(int port,
						    typec_current_t max_ma,
						    uint32_t supply_voltage);

/**
 * Set the type-C current limit when sourcing current..
 *
 * @param port USB-C port number
 * @param rp One of enum tcpc_rp_value (eg TYPEC_RP_3A0) defining the limit.
 */
void typec_set_source_current_limit(int port, enum tcpc_rp_value rp);

/**
 * Verify board specific health status : current, voltages...
 *
 * @return EC_SUCCESS if the board is good, <0 else.
 */
__override_proto int pd_board_checks(void);

/**
 * Return if VBUS is detected on type-C port
 *
 * @param port USB-C port number
 * @return VBUS is detected
 */
int pd_snk_is_vbus_provided(int port);

/**
 * Notify PD protocol that VBUS has gone low
 *
 * @param port USB-C port number
 */
void pd_vbus_low(int port);

/**
 * Check if power swap is allowed.
 *
 * @param port USB-C port number
 * @return True if power swap is allowed, False otherwise
 */
__override_proto int pd_check_power_swap(int port);

/**
 * Check if data swap is allowed.
 *
 * @param port USB-C port number
 * @param data_role current data role
 * @return True if data swap is allowed, False otherwise
 */
__override_proto int pd_check_data_swap(int port,
				enum pd_data_role data_role);

/**
 * Check if vconn swap is allowed.
 *
 * @param port USB-C port number
 * @return True if vconn swap is allowed, False otherwise
 */

int pd_check_vconn_swap(int port);

/**
 * Check current power role for potential power swap
 *
 * @param port USB-C port number
 * @param pr_role Our power role
 * @param flags PD flags
 */
__override_proto void pd_check_pr_role(int port,
				enum pd_power_role pr_role,
				int flags);

/**
 * Check current data role for potential data swap
 *
 * @param port USB-C port number
 * @param dr_role Our data role
 * @param flags PD flags
 */
__override_proto void pd_check_dr_role(int port,
				enum pd_data_role dr_role,
				int flags);

/**
 * Check for a potential Vconn swap if the port isn't
 * supplying Vconn
 *
 * @param port USB-C port number
 * @param flags PD flags
 */
__override_proto void pd_try_execute_vconn_swap(int port, int flags);

/**
 * Check if we should charge from this device. This is
 * basically a white-list for chargers that are dual-role,
 * don't set the unconstrained bit, but we should charge
 * from by default.
 *
 * @param vid Port partner Vendor ID
 * @param pid Port partner Product ID
 */
int pd_charge_from_device(uint16_t vid, uint16_t pid);

/**
 * Execute data swap.
 *
 * @param port USB-C port number
 * @param data_role new data role
 */
__override_proto void pd_execute_data_swap(int port,
				enum pd_data_role data_role);

/**
 * Get PD device info used for VDO_CMD_SEND_INFO / VDO_CMD_READ_INFO
 *
 * @param info_data pointer to info data array
 */
void pd_get_info(uint32_t *info_data);

/**
 * Handle Vendor Defined Messages
 *
 * @param port     USB-C port number
 * @param cnt      number of data objects in the payload.
 * @param payload  payload data.
 * @param rpayload pointer to the data to send back.
 * @return if >0, number of VDOs to send back.
 */
__override_proto int pd_custom_vdm(int port, int cnt, uint32_t *payload,
				   uint32_t **rpayload);

/**
 * Handle Structured Vendor Defined Messages
 *
 * @param port     USB-C port number
 * @param cnt      number of data objects in the payload.
 * @param payload  payload data.
 * @param rpayload pointer to the data to send back.
 * @param head     message header
 * @return if >0, number of VDOs to send back.
 */
int pd_svdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload,
		uint16_t head);

/**
 * Handle Custom VDMs for flashing.
 *
 * @param port     USB-C port number
 * @param cnt      number of data objects in the payload.
 * @param payload  payload data.
 * @return if >0, number of VDOs to send back.
 */
int pd_custom_flash_vdm(int port, int cnt, uint32_t *payload);

/**
 * Enter alternate mode on DFP
 *
 * @param port     USB-C port number
 * @param svid USB standard or vendor id to exit or zero for DFP amode reset.
 * @param opos object position of mode to exit.
 * @return vdm for UFP to be sent to enter mode or zero if not.
 */
uint32_t pd_dfp_enter_mode(int port, uint16_t svid, int opos);

/**
 *  Get DisplayPort pin mode for DFP to request from UFP's capabilities.
 *
 * @param port     USB-C port number.
 * @param status   DisplayPort Status VDO.
 * @return one-hot PIN config to request.
 */
int pd_dfp_dp_get_pin_mode(int port, uint32_t status);

/**
 * Exit alternate mode on DFP
 *
 * @param port USB-C port number
 * @param svid USB standard or vendor id to exit or zero for DFP amode reset.
 * @param opos object position of mode to exit.
 * @return 1 if UFP should be sent exit mode VDM.
 */
int pd_dfp_exit_mode(int port, uint16_t svid, int opos);

/**
 * Consume the SVDM attention data
 *
 * @param port USB-C port number
 * @param payload  payload data.
 */
void dfp_consume_attention(int port, uint32_t *payload);

/**
 * Consume the discover identity message
 *
 * @param port    USB-C port number
 * @param cnt     number of data objects in payload
 * @param payload payload data.
 */
void dfp_consume_identity(int port, int cnt, uint32_t *payload);

/**
 * Consume the SVIDs
 *
 * @param port    USB-C port number
 * @param type    Transmit type (SOP, SOP') for received SVIDs
 * @param cnt     number of data objects in payload
 * @param payload payload data.
 */
void dfp_consume_svids(int port, enum tcpm_transmit_type type, int cnt,
		uint32_t *payload);

/**
 * Consume the alternate modes
 *
 * @param port    USB-C port number
 * @param type    Transmit type (SOP, SOP') for received modes
 * @param cnt     number of data objects in payload
 * @param payload payload data.
 */
void dfp_consume_modes(int port, enum tcpm_transmit_type type, int cnt,
		uint32_t *payload);

/**
 * Return the discover alternate mode payload data
 *
 * @param port    USB-C port number
 * @param payload Pointer to payload data to fill
 * @return 1 if valid SVID present else 0
 */
int dfp_discover_modes(int port, uint32_t *payload);

/**
 * Initialize alternate mode discovery info for DFP
 *
 * @param port     USB-C port number
 */
void pd_dfp_discovery_init(int port);


/**
 * Set identity discovery state for this type and port
 *
 * @param port  USB-C port number
 * @param type	SOP* type to set
 * @param disc  Discovery state to set (failed or complete)
 */
void pd_set_identity_discovery(int port, enum tcpm_transmit_type type,
			       enum pd_discovery_state disc);

/**
 * Get identity discovery state for this type and port
 *
 * @param port  USB-C port number
 * @param type	SOP* type to retrieve
 * @return      Current discovery state (failed or complete)
 */
enum pd_discovery_state pd_get_identity_discovery(int port,
						enum tcpm_transmit_type type);

/**
 * Set SVID discovery state for this type and port.
 *
 * @param port USB-C port number
 * @param type SOP* type to set
 * @param disc Discovery state to set (failed or complete)
 */
void pd_set_svids_discovery(int port, enum tcpm_transmit_type type,
		enum pd_discovery_state disc);

/**
 * Get SVID discovery state for this type and port
 *
 * @param port USB-C port number
 * @param type SOP* type to retrieve
 * @return     Current discovery state (failed or complete)
 */
enum pd_discovery_state pd_get_svids_discovery(int port,
		enum tcpm_transmit_type type);

/**
 * Set Modes discovery state for this port, SOP* type, and SVID.
 *
 * @param port USB-C port number
 * @param type SOP* type to set
 * @param svid SVID to set mode discovery state for
 * @param disc Discovery state to set (failed or complete)
 */
void pd_set_modes_discovery(int port, enum tcpm_transmit_type type,
		uint16_t svid, enum pd_discovery_state disc);

/**
 * Get Modes discovery state for this port and SOP* type. Modes discover is
 * considered complete for a port and type when modes have been discovered for
 * all discovered SVIDs. Mode discovery is failed if mode discovery for any SVID
 * failed.
 *
 * @param port USB-C port number
 * @param type SOP* type to retrieve
 * @return      Current discovery state (failed or complete)
 */
enum pd_discovery_state pd_get_modes_discovery(int port,
		enum tcpm_transmit_type type);

/**
 * Get a pointer to mode data for the next SVID with undiscovered modes. This
 * data may indicate that discovery failed.
 *
 * @param port USB-C port number
 * @param type SOP* type to retrieve
 * @return     Pointer to the first SVID-mode structure with undiscovered mode;
 *             discovery may be needed or failed; returns NULL if all SVIDs have
 *             discovered modes
 */
struct svid_mode_data *pd_get_next_mode(int port, enum tcpm_transmit_type type);

/**
 * Return a pointer to the discover identity response structure for this SOP*
 * type
 *
 * @param port  USB-C port number
 * @param type	SOP* type to retrieve
 * @return      pointer to response structure, which the caller may not alter
 */
const union disc_ident_ack *pd_get_identity_response(int port,
					       enum tcpm_transmit_type type);

/**
 * Return the VID of the USB PD accessory connected to a specified port
 *
 * @param port  USB-C port number
 * @return      the USB Vendor Identifier or 0 if it doesn't exist
 */
uint16_t pd_get_identity_vid(int port);

/**
 * Return the PID of the USB PD accessory connected to a specified port
 *
 * @param port  USB-C port number
 * @return      the USB Product Identifier or 0 if it doesn't exist
 */
uint16_t pd_get_identity_pid(int port);

/**
 * Return the product type connected to a specified port
 *
 * @param port  USB-C port number
 * @return      USB-C product type (hub,periph,cable,ama)
 */
uint8_t pd_get_product_type(int port);

/**
 * Return the SVID count of port partner connected to a specified port
 *
 * @param port  USB-C port number
 * @param type	SOP* type to retrieve
 * @return      SVID count
 */
int pd_get_svid_count(int port, enum tcpm_transmit_type type);

/**
 * Return the SVID of given SVID index of port partner connected
 * to a specified port
 *
 * @param port     USB-C port number
 * @param svid_idx SVID Index
 * @param type	   SOP* type to retrieve
 * @return         SVID
 */
uint16_t pd_get_svid(int port, uint16_t svid_idx, enum tcpm_transmit_type type);

/**
 * Return the pointer to modes of VDO of port partner connected
 * to a specified port
 *
 * @param port     USB-C port number
 * @param svid_idx SVID Index
 * @param type     SOP* type to retrieve
 * @return         Pointer to modes of VDO
 */
uint32_t *pd_get_mode_vdo(int port, uint16_t svid_idx,
		enum tcpm_transmit_type type);

/**
 * Return the alternate mode entry and exit data
 *
 * @param port  USB-C port number
 * @param svid  SVID
 * @return      pointer to SVDM mode data
 */
struct svdm_amode_data *pd_get_amode_data(int port, uint16_t svid);

/**
 * Returns false if previous SOP' messageId count is different from received
 * messageId count.
 *
 * @param port		USB-C port number
 * @param msg_id        Received cable msg_id
 * @return              False if Received MessageId count is different from the
 *                      previous one.
 *                      True Otherwise
 */
bool consume_sop_prime_repeat_msg(int port, uint8_t msg_id);

/**
 * Returns false if previous SOP'' messageId count is different from received
 * messageId count.
 *
 * @param port		USB-C port number
 * @param msg_id        Received cable msg_id
 * @return              False if Received MessageId count is different from the
 *                      previous one.
 *                      True Otherwise
 */
bool consume_sop_prime_prime_repeat_msg(int port, uint8_t msg_id);

/**
 * Returns the status of cable flag - CABLE_FLAGS_SOP_PRIME_ENABLE
 *
 * @param port		USB-C port number
 * @return              Status of CABLE_FLAGS_SOP_PRIME_ENABLE flag
 */
bool is_transmit_msg_sop_prime(int port);

/*
 * Returns the pointer to PD alternate mode discovery results
 * Note: Caller function can mutate the data in this structure.
 *
 * @param port USB-C port number
 * @param type Transmit type (SOP, SOP') for discovered information
 * @return     pointer to PD alternate mode discovery results
 */
struct pd_discovery *pd_get_am_discovery(int port,
		enum tcpm_transmit_type type);

/*
 * Return the pointer to PD cable attributes
 * Note: Caller function can mutate the data in this structure.
 *
 * @param port  USB-C port number
 * @return      pointer to PD cable attributes
 */
struct pd_cable *pd_get_cable_attributes(int port);

/*
 * Returns True if cable supports USB2 connection
 *
 * @param port  USB-C port number
 * @return      True is usb2_supported, false otherwise
 */
bool is_usb2_cable_support(int port);

/*
 * Checks if Cable speed is Gen2 or better
 *
 * @param port  USB-C port number
 * @return      True if Cable supports speed USB_R20_SS_U31_GEN1_GEN2/
 *                                           USB_R30_SS_U32_U40_GEN2/
 *                                           USB_R30_SS_U40_GEN3,
 *             False otherwise
 */
bool is_cable_speed_gen2_capable(int port);

/*
 * Checks if Active Cable has retimer as an active element
 *
 * @param port  USB-C port number
 * @return      True if Active element is Retimer
 *              False otherwise
 */
bool is_active_cable_element_retimer(int port);

/**
 * Set DFP enter mode flags if available
 *
 * @param port  USB-C port number
 * @param set   If true set the flag else clear
 */
void pd_set_dfp_enter_mode_flag(int port, bool set);

/**
 * Returns the status of cable flag - CABLE_FLAGS_SOP_PRIME_PRIME_ENABLE
 *
 * @param port		USB-C port number
 * @return		Status of CABLE_FLAGS_SOP_PRIME_PRIME_ENABLE flag
 */
bool is_transmit_msg_sop_prime_prime(int port);

/**
 * Returns the type of communication (SOP/SOP'/SOP'')
 *
 * @param port		USB-C port number
 * @return		Type of message to be transmitted
 */
enum pd_msg_type pd_msg_tx_type(int port);

/**
 * Reset Cable type, Cable attributes and cable flags
 *
 * @param port     USB-C port number
 */
void reset_pd_cable(int port);

/**
 * Returns true if the number of data objects in the payload is greater than
 * than the VDO index
 *
 * @param cnt      number of data objects in payload
 * @param index    VDO Index
 * @return         True if number of data objects is greater than VDO index,
 *                 false otherwise
 */
bool is_vdo_present(int cnt, int index);

/**
 * Return the type of cable attached
 *
 * @param port	USB-C port number
 * @return	cable type
 */
enum idh_ptype get_usb_pd_cable_type(int port);

/**
 * Stores the cable's response to discover Identity SOP' request
 *
 * @param port      USB-C port number
 * @param cnt       number of data objects in payload
 * @param payload   payload data
 * @param head      PD packet header
 */
void dfp_consume_cable_response(int port, int cnt, uint32_t *payload,
					uint16_t head);

/**
 * Returns USB4 cable speed according to the port, if port supports lesser
 * USB4 cable speed than the cable.
 *
 * For USB4 cable speed = USB3.2 Gen 2:
 *                              |
 *                    Is DFP gen 3 capable?
 *                              |
 *                      Yes ----|----- No ----
 *                      |                     |
 *           Cable supports Thunderbolt 3     |
 *           Gen 3 cable speed?               |
 *                     |                      |
 *         ---- yes ---|-----No --------------|
 *         |                                  |
 * USB4 cable speed = USB4 gen 3     USB4 cable speed = USB3.2/USB4 Gen 2
 *
 * Ref: USB Type-C Cable and Connector Specification, figure 5-1 USB4 Discovery
 * and Entry Flow Mode.
 *
 * @param port      USB-C port number
 * @return          USB4 cable speed
 */
enum usb_rev30_ss get_usb4_cable_speed(int port);

/**
 * Check if attached device has USB4 VDO
 *
 * @param port      USB-C port number
 * @param cnt       number of data objects in payload
 * @param payload   payload data
 * @return          True if device has USB4 VDO
 */
bool is_usb4_vdo(int port, int cnt, uint32_t *payload);

/**
 * Return enter USB message payload
 *
 * @param port	USB-C port number
 */
uint32_t get_enter_usb_msg_payload(int port);

/**
 * Enter USB4 mode
 *
 * @param port	USB-C port number
 */
void enter_usb4_mode(int port);

/**
 * Clear enter USB4 mode
 *
 * @param port	USB-C port number
 */
void disable_enter_usb4_mode(int port);

/**
 * Return if need to enter into USB4 mode
 *
 * @param port	USB-C port number
 */
bool should_enter_usb4_mode(int port);

/**
 * Return the response of discover mode SOP prime, with SVID = 0x8087
 *
 * @param port	USB-C port number
 * @return	cable mode response vdo
 */
union tbt_mode_resp_cable get_cable_tbt_vdo(int port);

/**
 * Return the response of discover mode SOP, with SVID = 0x8087
 *
 * @param port	USB-C port number
 * @return	device mode response vdo
 */
union tbt_mode_resp_device get_dev_tbt_vdo(int port);

/**
 * Return Thunderbolt rounded support
 * Rounded support indicates if the cable can support rounding the
 * frequency depending upon the cable generation.
 *
 * @param port USB-C port number
 * @return tbt_rounded_support
 */
enum tbt_compat_rounded_support get_tbt_rounded_support(int port);

/**
 * Sets the Mux state to Thunderbolt-Compatible mode
 *
 *  @param port  USB-C port number
 */
void set_tbt_compat_mode_ready(int port);

/**
 * Checks if the attached cable supports superspeed
 *
 * @param port	USB-C port number
 * @return      True if cable is superspeed, false otherwise
 */
bool is_tbt_cable_superspeed(int port);

/**
 * Check if product supports any Modal Operation (Alternate Modes)
 *
 * @param port	   USB-C port number
 * @param cnt      number of data objects in payload
 * @param payload  payload data
 * @return         True if product supports Modal Operation, false otherwise
 */
bool is_modal(int port, int cnt, const uint32_t *payload);

/**
 * Checks all the SVID for USB_VID_INTEL
 *
 * @param port	        USB-C port number
 * @param prev_svid_cnt Previous SVID count
 * @return              True is SVID = USB_VID_INTEL, false otherwise
 */
bool is_intel_svid(int port, int prev_svid_cnt);

/**
 * Checks if Device discover mode response contains Thunderbolt alternate mode
 *
 * @param port	   USB-C port number
 * @param cnt      number of data objects in payload
 * @param payload  payload data
 * @return         True if Thunderbolt Alternate mode response is received,
 *                 false otherwise
 */
bool is_tbt_compat_mode(int port, int cnt, const uint32_t *payload);

/**
 * Returns Thunderbolt-compatible cable speed according to the port if,
 * port supports lesser speed than the cable
 *
 * @param port USB-C port number
 * @return thunderbolt-cable cable speed
 */
enum tbt_compat_cable_speed get_tbt_cable_speed(int port);

/*
 * Checks if the cable supports Thunderbolt speed.
 *
 * @param port   USB-C port number
 * @return       True if the Thunderbolt cable speed is TBT_SS_TBT_GEN3 or
 *               TBT_SS_U32_GEN1_GEN2, false otherwise
 */
bool cable_supports_tbt_speed(int port);

/**
 * Fills the TBT3 objects in the payload and returns the number
 * of objects it has filled.
 *
 * @param port      USB-C port number
 * @param sop       Type of SOP message transmitted (SOP/SOP'/SOP'')
 * @param payload   payload data
 * @return          Number of object filled
 */
int enter_tbt_compat_mode(int port, enum tcpm_transmit_type sop,
			uint32_t *payload);

/**
 * Return maximum allowed speed for Thunderbolt-compatible mode
 *
 * NOTE: Chromebooks require that all USB-C ports support the same features,
 * so the maximum speed returned by this function should be set to the lowest
 * speed supported by all ports. Products in development (i.e. proto boards)
 * can support different speeds on each port for validation purpose.
 *
 * Ref: TGL PDG
 * 3.1: Fiberweave Impact for HSIOs Operating at ≥8 GT/s Speeds
 * MAX TBT routing length is 205mm prior to connection to re-timer
 *
 * Thunderbolt-compatible mode has electrical and PCB requirements for signal
 * routing and length. Default speed is set for connected cable's speed.
 * Board level function can override the cable speed based on the design.
 *
 * @param port USB-C port number
 * @return cable speed
 */
__override_proto enum tbt_compat_cable_speed board_get_max_tbt_speed(int port);

/**
 * Return true if the board's port supports TBT or USB4
 *
 * NOTE: This is only applicable for products in development (i.e. proto boards)
 * For customer products, all USB-C ports need to support the same features.
 *
 * Ref: TGL PDG
 * 5.2 USB-C* Sub-System:
 * Motherboard should have re-timer for all USB-C connectors that supports TBT.
 * Aux/LSx platform level muxing is required.
 *
 * When TBT or USB4 mode is enabled, by default all the ports are assumed to be
 * supporting TBT or USB4. However, not all the ports may support TBT & USB4
 * due to dependency on retimer and platform level Aux/LSx muxing. This board
 * level function can override the TBT & USB4 logic based on board design.
 *
 * @param port USB-C port number
 * @return True if TBT or USB4 is supported on the specified port, else false.
 */
__override_proto bool board_is_tbt_usb4_port(int port);

/**
 * Store Device ID & RW hash of device
 *
 * @param port			USB-C port number
 * @param dev_id		device identifier
 * @param rw_hash		pointer to rw_hash
 * @param current_image		current image: RW or RO
 * @return			true if the dev / hash match an existing hash
 *				in our table, false otherwise
 */
int pd_dev_store_rw_hash(int port, uint16_t dev_id, uint32_t *rw_hash,
			 uint32_t ec_image);

/**
 * Get Device ID & RW hash of device
 *
 * @param port			USB-C port number
 * @param dev_id		pointer to device identifier
 * @param rw_hash		pointer to rw_hash
 * @param current_image		pointer to current image: RW or RO
 */
void pd_dev_get_rw_hash(int port, uint16_t *dev_id, uint8_t *rw_hash,
			uint32_t *current_image);

/**
 * Fast Role Swap was detected
 *
 * @param port			USB-C port number
 */
void pd_got_frs_signal(int port);

/**
 * Try to fetch one PD log entry from accessory
 *
 * @param port	USB-C accessory port number
 * @return	EC_RES_SUCCESS if the VDM was sent properly else error code
 */
int pd_fetch_acc_log_entry(int port);

/**
 * Analyze the log entry received as the VDO_CMD_GET_LOG payload.
 *
 * @param port		USB-C accessory port number
 * @param cnt		number of data objects in payload
 * @param payload	payload data
 */
void pd_log_recv_vdm(int port, int cnt, uint32_t *payload);

/**
 * Send Vendor Defined Message
 *
 * @param port     USB-C port number
 * @param vid      Vendor ID
 * @param cmd      VDO command number
 * @param data     Pointer to payload to send
 * @param count    number of data objects in payload
 */
void pd_send_vdm(int port, uint32_t vid, int cmd, const uint32_t *data,
		 int count);

/* Power Data Objects for the source and the sink */
__override_proto extern const uint32_t pd_src_pdo[];
extern const int pd_src_pdo_cnt;
extern const uint32_t pd_src_pdo_max[];
extern const int pd_src_pdo_max_cnt;
extern const uint32_t pd_snk_pdo[];
extern const int pd_snk_pdo_cnt;

/**
 * Request that a host event be sent to notify the AP of a PD power event.
 *
 * @param mask host event mask.
 */
#if defined(HAS_TASK_HOSTCMD) && !defined(TEST_BUILD)
void pd_send_host_event(int mask);
#else
static inline void pd_send_host_event(int mask) { }
#endif

/**
 * Determine if in alternate mode or not.
 *
 * @param port port number.
 * @param svid USB standard or vendor id
 * @return object position of mode chosen in alternate mode otherwise zero.
 */
int pd_alt_mode(int port, uint16_t svid);

/**
 * Send hpd over USB PD.
 *
 * @param port port number.
 * @param hpd hotplug detect type.
 */
void pd_send_hpd(int port, enum hpd_event hpd);

/**
 * Enable USB Billboard Device.
 */
extern const struct deferred_data pd_usb_billboard_deferred_data;
/* --- Physical layer functions : chip specific --- */

/* Packet preparation/retrieval */

/**
 * Prepare packet reading state machine.
 *
 * @param port USB-C port number
 */
void pd_init_dequeue(int port);

/**
 * Prepare packet reading state machine.
 *
 * @param port USB-C port number
 * @param off  current position in the packet buffer.
 * @param len  minimum size to read in bits.
 * @param val  the read bits.
 * @return new position in the packet buffer.
 */
int pd_dequeue_bits(int port, int off, int len, uint32_t *val);

/**
 * Advance until the end of the preamble.
 *
 * @param port USB-C port number
 * @return new position in the packet buffer.
 */
int pd_find_preamble(int port);

/**
 * Write the preamble in the TX buffer.
 *
 * @param port USB-C port number
 * @return new position in the packet buffer.
 */
int pd_write_preamble(int port);

/**
 * Write one 10-period symbol in the TX packet.
 * corresponding to a quartet with 4b5b encoding
 * and Biphase Mark Coding.
 *
 * @param port USB-C port number
 * @param bit_off current position in the packet buffer.
 * @param val10    the 10-bit integer.
 * @return new position in the packet buffer.
 */
int pd_write_sym(int port, int bit_off, uint32_t val10);


/**
 * Ensure that we have an edge after EOP and we end up at level 0,
 * also fill the last byte.
 *
 * @param port USB-C port number
 * @param bit_off current position in the packet buffer.
 * @return new position in the packet buffer.
 */
int pd_write_last_edge(int port, int bit_off);

/**
 * Do 4B5B encoding on a 32-bit word.
 *
 * @param port USB-C port number
 * @param off current offset in bits inside the message
 * @param val32 32-bit word value to encode
 * @return new offset in the message in bits.
 */
int encode_word(int port, int off, uint32_t val32);

/**
 * Ensure that we have an edge after EOP and we end up at level 0,
 * also fill the last byte.
 *
 * @param port USB-C port number
 * @param header PD packet header
 * @param cnt number of payload words
 * @param data payload content
 * @return length of the message in bits.
 */
int prepare_message(int port, uint16_t header, uint8_t cnt,
		    const uint32_t *data);

/**
 * Dump the current PD packet on the console for debug.
 *
 * @param port USB-C port number
 * @param msg  context string.
 */
void pd_dump_packet(int port, const char *msg);

/**
 * Change the TX data clock frequency.
 *
 * @param port USB-C port number
 * @param freq frequency in hertz.
 */
void pd_set_clock(int port, int freq);

/* TX/RX callbacks */

/**
 * Start sending over the wire the prepared packet.
 *
 * @param port USB-C port number
 * @param polarity plug polarity (0=CC1, 1=CC2).
 * @param bit_len size of the packet in bits.
 * @return length transmitted or negative if error
 */
int pd_start_tx(int port, int polarity, int bit_len);

/**
 * Set PD TX DMA to use circular mode. Call this before pd_start_tx() to
 * continually loop over the transmit buffer given in pd_start_tx().
 *
 * @param port USB-C port number
 */
void pd_tx_set_circular_mode(int port);

/**
 * Stop PD TX DMA circular mode transaction already in progress.
 *
 * @param port USB-C port number
 */
void pd_tx_clear_circular_mode(int port);

/**
 * Call when we are done sending a packet.
 *
 * @param port USB-C port number
 * @param polarity plug polarity (0=CC1, 1=CC2).
 */
void pd_tx_done(int port, int polarity);

/**
 * Check whether the PD reception is started.
 *
 * @param port USB-C port number
 * @return true if the reception is on-going.
 */
int pd_rx_started(int port);

/**
 * Suspend the PD task.
 * @param port USB-C port number
 * @param suspend pass 0 to resume, anything else to suspend
 */
void pd_set_suspend(int port, int suspend);

/**
 * Resume the PD task for a port after a period of time has elapsed.
 * @param port USB-C port number
 */
void pd_deferred_resume(int port);

/**
 * Check if the port has been initialized and PD task has not been
 * suspended.
 *
 * @param port USB-C port number
 * @return true if the PD task is not suspended.
 */
int pd_is_port_enabled(int port);

/* Callback when the hardware has detected an incoming packet */
void pd_rx_event(int port);
/* Start sampling the CC line for reception */
void pd_rx_start(int port);
/* Call when we are done reading a packet */
void pd_rx_complete(int port);

/* restart listening to the CC wire */
void pd_rx_enable_monitoring(int port);
/* stop listening to the CC wire during transmissions */
void pd_rx_disable_monitoring(int port);

/* get time since last RX edge interrupt */
uint64_t get_time_since_last_edge(int port);

/**
 * Deinitialize the hardware used for PD.
 *
 * @param port USB-C port number
 */
void pd_hw_release(int port);

/**
 * Initialize the hardware used for PD RX/TX.
 *
 * @param port USB-C port number
 * @param role Role to initialize pins in
 */
void pd_hw_init(int port, enum pd_power_role role);

/**
 * Initialize the reception side of hardware used for PD.
 *
 * This is a subset of pd_hw_init() including only :
 * the comparators + the RX edge delay timer + the RX DMA.
 *
 * @param port USB-C port number
 */
void pd_hw_init_rx(int port);

/* --- Protocol layer functions --- */

/**
 * Decode a raw packet in the RX buffer.
 *
 * @param port USB-C port number
 * @param payload buffer to store the packet payload (must be 7x 32-bit)
 * @return the packet header or <0 in case of error
 */
int pd_analyze_rx(int port, uint32_t *payload);

/**
 * Check if PD communication is enabled
 *
 * @return true if it's enabled or false otherwise
 */
int pd_comm_is_enabled(int port);

/**
 * Check if PD is capable of alternate mode
 *
 * @return true if PD is capable of alternate mode else false
 */
bool pd_alt_mode_capable(int port);

/**
 * Get connected state
 *
 * @param port USB-C port number
 * @return True if port is in connected state
 */
int pd_is_connected(int port);

/**
 * Execute a hard reset
 *
 * @param port USB-C port number
 */
void pd_execute_hard_reset(int port);

/**
 * Signal to protocol layer that PD transmit is complete
 *
 * @param port USB-C port number
 * @param status status of the transmission
 */
void pd_transmit_complete(int port, int status);

/**
 * Get port polarity.
 *
 * @param port USB-C port number
 */
enum tcpc_cc_polarity pd_get_polarity(int port);

/**
 * Get port partner data swap capable status
 *
 * @param port USB-C port number
 * @return True if data swap capable else false
 */
bool pd_get_partner_data_swap_capable(int port);

/**
 * Handle an overcurrent protection event.  The port acting as a source has
 * reported an overcurrent event.
 *
 * @param port: USB-C port number.
 */
void pd_handle_overcurrent(int port);

/**
 * Handle a CC overvoltage protection event.
 *
 * @param port: USB-C port number.
 */
void pd_handle_cc_overvoltage(int port);

/**
 * Request power swap command to be issued
 *
 * @param port USB-C port number
 */
void pd_request_power_swap(int port);

/**
 * Try to become the VCONN source, if we are not already the source and the
 * other side is willing to accept a VCONN swap.
 *
 * @param port USB-C port number
 */
void pd_try_vconn_src(int port);

/**
 * Request data swap command to be issued
 *
 * @param port USB-C port number
 */
void pd_request_data_swap(int port);

/**
 * Set the PD communication enabled flag. When communication is disabled,
 * the port can still detect connection and source power but will not
 * send or respond to any PD communication.
 *
 * @param port USB-C port number
 * @param enable Enable flag to set
 */
void pd_comm_enable(int port, int enable);

/**
 * Set the PD pings enabled flag. When source has negotiated power over
 * PD successfully, it can optionally send pings periodically based on
 * this enable flag.
 *
 * @param port USB-C port number
 * @param enable Enable flag to set
 */
void pd_ping_enable(int port, int enable);

/* Issue PD soft reset */
void pd_soft_reset(void);

/* Prepare PD communication for reset */
void pd_prepare_reset(void);

/**
 * Signal power request to indicate a charger update that affects the port.
 *
 * @param port USB-C port number
 */
void pd_set_new_power_request(int port);

/**
 * Return true if partner port is a DTS or TS capable of entering debug
 * mode (eg. is presenting Rp/Rp or Rd/Rd).
 *
 * @param port USB-C port number
 */
int pd_ts_dts_plugged(int port);

/**
 * Return true if partner port is known to be PD capable.
 *
 * @param port USB-C port number
 * @return true if PD capable else false
 */
bool pd_capable(int port);

/**
 * Returns the source caps list
 *
 * @param port USB-C port number
 */
const uint32_t * const pd_get_src_caps(int port);

/**
 * Returns the number of source caps
 *
 * @param port USB-C port number
 */
uint8_t pd_get_src_cap_cnt(int port);

/**
 * Set the source caps list & count
 *
 * @param port     USB-C port number
 * @param cnt      Source caps count
 * @param src_caps Pointer to source caps
 *
 */
void pd_set_src_caps(int port, int cnt, uint32_t *src_caps);

/**
 * Return true if partner port is capable of communication over USB data
 * lines.
 *
 * @param port USB-C port number
 */
bool pd_get_partner_usb_comm_capable(int port);

/**
 * Return true if PD is in disconnect state
 *
 * @param port USB-C port number
 */
bool pd_is_disconnected(int port);

/**
 * Return true if vbus is at level on the specified port.
 *
 * @param port USB-C port number
 * @param level vbus_level to check against
 */
bool pd_check_vbus_level(int port, enum vbus_level level);

/**
 * Return true if vbus is at Safe5V on the specified port.
 *
 * @param port USB-C port number
 */
int pd_is_vbus_present(int port);

/**
 * Get current DisplayPort pin mode on the specified port.
 *
 * @param port USB-C port number
 * @return MODE_DP_PIN_[A-E] if used else 0
 */
__override_proto uint8_t get_dp_pin_mode(int port);

#ifdef CONFIG_USB_PD_PORT_MAX_COUNT
#ifdef CONFIG_USB_POWER_DELIVERY
/**
 * Get board specific usb pd port count
 *
 * @return <= CONFIG_USB_PD_PORT_MAX_COUNT if configured in board file,
 *         else return CONFIG_USB_PD_PORT_MAX_COUNT
 */
uint8_t board_get_usb_pd_port_count(void);
#else
static inline uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}
#endif /* CONFIG_USB_POWER_DELIVERY */
#endif /* CONFIG_USB_PD_PORT_MAX_COUNT */

/**
 * Return true if specified PD port is debug accessory.
 *
 * @param port USB-C port number
 */
bool pd_is_debug_acc(int port);

/**
 * Sets the polarity of the port
 *
 * @param port USB-C port number
 * @param polarity 0 for CC1, else 1 for CC2
 */
void pd_set_polarity(int port, enum tcpc_cc_polarity polarity);

/*
 * Notify the AP that we have entered into DisplayPort Alternate Mode.  This
 * sets a DP_ALT_MODE_ENTERED MKBP event which may wake the AP.
 */
void pd_notify_dp_alt_mode_entry(void);

/*
 * Determines the PD state of the port partner according to Table 4-10 in USB PD
 * specification.
 */
enum pd_cc_states pd_get_cc_state(
	enum tcpc_cc_voltage_status cc1, enum tcpc_cc_voltage_status cc2);

/*
 * Optional, get the board-specific SRC DTS polarity.
 *
 * This function is used for SRC DTS mode. The polarity is predetermined as a
 * board-specific setting, i.e. what Rp impedance the CC lines are pulled.
 *
 * @param port USB-C port number
 * @return port polarity (0=CC1, 1=CC2)
 */
__override_proto uint8_t board_get_src_dts_polarity(int port);

/* ----- Logging ----- */
#ifdef CONFIG_USB_PD_LOGGING
/**
 * Record one event in the PD logging FIFO.
 *
 * @param type event type as defined by PD_EVENT_xx in ec_commands.h
 * @param size_port payload size and port num (defined by PD_LOG_PORT_SIZE)
 * @param data type-defined information
 * @param payload pointer to the optional payload (0..16 bytes)
 */
void pd_log_event(uint8_t type, uint8_t size_port,
		  uint16_t data, void *payload);

/**
 * Retrieve one logged event and prepare a VDM with it.
 *
 * Used to answer the VDO_CMD_GET_LOG unstructured VDM.
 *
 * @param payload pointer to the payload data buffer (must be 7 words)
 * @return number of 32-bit words in the VDM payload.
 */
int pd_vdm_get_log_entry(uint32_t *payload);
#else  /* CONFIG_USB_PD_LOGGING */
static inline void pd_log_event(uint8_t type, uint8_t size_port,
				uint16_t data, void *payload) {}
static inline int pd_vdm_get_log_entry(uint32_t *payload) { return 0; }
#endif /* CONFIG_USB_PD_LOGGING */

/**
 * Prepare for a sysjump by exiting any alternate modes, if PD communication is
 * allowed.
 *
 * Note: this call will block until the PD task has finished its exit mode and
 * re-awoken the calling task.
 */
void pd_prepare_sysjump(void);

/* ----- SVDM handlers ----- */

/* DisplayPort Alternate Mode */
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
extern int dp_flags[CONFIG_USB_PD_PORT_MAX_COUNT];
extern uint32_t dp_status[CONFIG_USB_PD_PORT_MAX_COUNT];
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

/*
 * Configure the USB MUX in safe mode
 *
 * @param port The PD port number
 */
void usb_mux_set_safe_mode(int port);

/**
 * Configure the pins used for DisplayPort Alternate Mode into safe state.
 *
 * @param port The PD port number
 */
__override_proto void svdm_safe_dp_mode(int port);

/**
 * Enter DisplayPort Alternate Mode.
 *
 * The default implementation will only enter DP Alt Mode if the SoC is on.
 * Also, it may notify the AP that the mode was entered.
 *
 * @param port The PD port number
 * @param mode_caps Bitmask indicating DisplayPort mode capabilities
 * @return 0 if mode is entered, -1 otherwise.
 */
__override_proto int svdm_enter_dp_mode(int port, uint32_t mode_caps);

/**
 * Construct a DP status response.
 *
 * @param port The PD port number
 * @param payload Pointer to the PDO payload which is filled with the DPStatus
 *                information.
 * @return number of VDOs
 */
__override_proto int svdm_dp_status(int port, uint32_t *payload);

/**
 * Configure the pins used for DisplayPort Alternate Mode.
 *
 * @param port The PD port number
 * @payload payload Pointer to the PDO payload which is filled with the
 *                  DPConfigure response message
 * @return number of VDOs
 */
__override_proto int svdm_dp_config(int port, uint32_t *payload);

/**
 * Perform any other work required after configuring the pins for DP Alt Mode.
 *
 * Typically, this involves sending the HPD signal from either the EC or TCPC to
 * the GPU.
 * @param port The PD port number
 */
__override_proto void svdm_dp_post_config(int port);

/**
 * Called when a DisplayPort Attention command is received
 *
 * The default implementation will parse the Attention message and indicate the
 * HPD level to the GPU.
 *
 * @param port The PD port number
 * @param payload Pointer to the payload received from the attention command
 * @return 0 for NAK, 1 for ACK
 */
__override_proto int svdm_dp_attention(int port, uint32_t *payload);

/**
 * Exit DisplayPort Alternate Mode.
 *
 * @param port The PD port number
 */
__override_proto void svdm_exit_dp_mode(int port);

/* Google Firmware Update Alternate Mode */
/**
 * Enter Google Firmware Update (GFU) Mode.
 *
 * @param port The PD port number
 * @param mode_caps Unused for GFU
 * @return 0 to enter the mode, -1 otherwise
 */
__override_proto int svdm_enter_gfu_mode(int port, uint32_t mode_caps);

/**
 * Exit Google Firmware Update Mode.
 *
 * @param port The PD port number
 */
__override_proto void svdm_exit_gfu_mode(int port);

/**
 * Called after successful entry into GFU Mode
 *
 * The default implementation sends VDO_CMD_READ_INFO.
 * @param port The PD port number
 * @param payload Unused for GFU
 * @return The number of VDOs
 */
__override_proto int svdm_gfu_status(int port, uint32_t *payload);

/**
 * Configure any pins needed for GFU Mode
 *
 * @param port The PD port number
 * @param payload Unused for GFU
 * @return The number of VDOs
 */
__override_proto int svdm_gfu_config(int port, uint32_t *payload);

/**
 * Called when an Attention Message is received
 *
 * @param port The PD port number
 * @param payload Unusued for GFU
 * @return The number of VDOs
 */
__override_proto int svdm_gfu_attention(int port, uint32_t *payload);

/* Thunderbolt-compatible Alternate Mode */
/**
 * Enter Thunderbolt-compatible Mode.
 *
 * @param port The PD port number
 * @param mode_caps Unused
 * @return 0 on success else -1
 */
__override_proto int svdm_tbt_compat_enter_mode(int port, uint32_t mode_caps);

/**
 * Exit Thunderbolt-compatible Mode.
 *
 * @param port The PD port number
 */
__override_proto void svdm_tbt_compat_exit_mode(int port);

/**
 * Called to get Thunderbolt-compatible mode status
 *
 * @param port The PD port number
 * @param payload Unused
 * @return 0 on success else -1
 */
__override_proto int svdm_tbt_compat_status(int port, uint32_t *payload);

/**
 * Called to configure Thunderbolt-compatible mode
 *
 * @param port The PD port number
 * @param payload Unused
 * @return 0 on success else -1
 */
__override_proto int svdm_tbt_compat_config(int port, uint32_t *payload);

/**
 * Called when Thunderbolt-compatible Attention Message is received
 *
 * @param port The PD port number
 * @param payload Unusued
 * @return 0 on success else -1
 */
__override_proto int svdm_tbt_compat_attention(int port, uint32_t *payload);

/* Miscellaneous */

/**
 * Called for responding to the EC_CMD_GET_PD_PORT_CAPS host command
 *
 * @param port	The PD port number
 * @return	Location of the port
 */

__override_proto enum ec_pd_port_location board_get_pd_port_location(int port);

/**
 * Can be called whenever VBUS presence changes.  The default implementation
 * does nothing, but a board may override it.
 */
__override_proto void board_vbus_present_change(void);
#endif  /* __CROS_EC_USB_PD_H */

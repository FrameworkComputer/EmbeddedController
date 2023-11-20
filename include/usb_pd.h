/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery module */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#ifndef __CROS_EC_USB_PD_H
#define __CROS_EC_USB_PD_H

#include "common.h"
#include "ec_commands.h"
#include "usb_pd_tbt.h"
#include "usb_pd_tcpm.h"
#include "usb_pd_vdo.h"

#include <stdbool.h>
#include <stdint.h>

/* PD Host command timeout */
#define PD_HOST_COMMAND_TIMEOUT_US SECOND

/*
 * Define PD_PORT_TO_TASK_ID() and TASK_ID_TO_PD_PORT() macros to
 * go between PD port number and task ID. Assume that TASK_ID_PD_C0 is the
 * lowest task ID and IDs are on a continuous range.
 */
#if defined(HAS_TASK_PD_C0) && defined(CONFIG_USB_PD_PORT_MAX_COUNT)
#define PD_PORT_TO_TASK_ID(port) (TASK_ID_PD_C0 + (port))
#define TASK_ID_TO_PD_PORT(id) ((id)-TASK_ID_PD_C0)
#else
#define PD_PORT_TO_TASK_ID(port) -1 /* stub task ID */
#define TASK_ID_TO_PD_PORT(id) 0
#endif /* CONFIG_USB_PD_PORT_MAX_COUNT && HAS_TASK_PD_C0 */

enum pd_rx_errors {
	PD_RX_ERR_INVAL = -1, /* Invalid packet */
	PD_RX_ERR_HARD_RESET = -2, /* Got a Hard-Reset packet */
	PD_RX_ERR_CRC = -3, /* CRC mismatch */
	PD_RX_ERR_ID = -4, /* Invalid ID number */
	PD_RX_ERR_UNSUPPORTED_SOP = -5, /* Unsupported SOP */
	PD_RX_ERR_CABLE_RESET = -6 /* Got a Cable-Reset packet */
};

/* Events for USB PD task */

/* Outgoing packet event */
#define PD_EVENT_TX TASK_EVENT_CUSTOM_BIT(3)
/* CC line change event */
#define PD_EVENT_CC TASK_EVENT_CUSTOM_BIT(4)
/* TCPC has reset */
#define PD_EVENT_TCPC_RESET TASK_EVENT_CUSTOM_BIT(5)
/* DRP state has changed */
#define PD_EVENT_UPDATE_DUAL_ROLE TASK_EVENT_CUSTOM_BIT(6)
/*
 * A task, other than the task owning the PD port, accessed the TCPC. The task
 * that owns the port does not send itself this event.
 */
#define PD_EVENT_DEVICE_ACCESSED TASK_EVENT_CUSTOM_BIT(7)
/* Chipset power state changed */
#define PD_EVENT_POWER_STATE_CHANGE TASK_EVENT_CUSTOM_BIT(8)
/* Issue a Hard Reset. */
#define PD_EVENT_SEND_HARD_RESET TASK_EVENT_CUSTOM_BIT(9)
/* Prepare for sysjump */
#define PD_EVENT_SYSJUMP TASK_EVENT_CUSTOM_BIT(10)
/* Receive a Hard Reset. */
#define PD_EVENT_RX_HARD_RESET TASK_EVENT_CUSTOM_BIT(11)
/* MUX configured notification event */
#define PD_EVENT_AP_MUX_DONE TASK_EVENT_CUSTOM_BIT(12)
/* First free event on PD task */
#define PD_EVENT_FIRST_FREE_BIT 13

/* Ensure TCPC is out of low power mode before handling these events. */
#define PD_EXIT_LOW_POWER_EVENT_MASK               \
	(PD_EVENT_CC | PD_EVENT_UPDATE_DUAL_ROLE | \
	 PD_EVENT_POWER_STATE_CHANGE | PD_EVENT_TCPC_RESET)

/* --- PD data message helpers --- */
#ifdef CONFIG_USB_PD_EPR
#define PDO_MAX_OBJECTS 11
#else
#define PDO_MAX_OBJECTS 7
#endif

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
 *
 * Note: Some bits and decode macros are defined in ec_commands.h
 */
#define PDO_FIXED_SUSPEND BIT(28) /* USB Suspend supported */
/* Higher capability in vSafe5V sink PDO */
#define PDO_FIXED_SNK_HIGHER_CAP BIT(28)
#define PDO_FIXED_FRS_CURR_NOT_SUPPORTED (0 << 23)
#define PDO_FIXED_FRS_CURR_DFLT_USB_POWER (1 << 23)
#define PDO_FIXED_FRS_CURR_1A5_AT_5V (2 << 23)
#define PDO_FIXED_FRS_CURR_3A0_AT_5V (3 << 23)
#define PDO_FIXED_EPR_MODE_CAPABLE BIT(23)
#define PDO_FIXED_PEAK_CURR () /* [21..20] Peak current */
#define PDO_FIXED_VOLT(mv) (((mv) / 50) << 10) /* Voltage in 50mV units */
#define PDO_FIXED_CURR(ma) (((ma) / 10) << 0) /* Max current in 10mA units */
#define PDO_FIXED_GET_VOLT(pdo) (((pdo >> 10) & 0x3FF) * 50)
#define PDO_FIXED_GET_CURR(pdo) ((pdo & 0x3FF) * 10)

#define PDO_FIXED(mv, ma, flags) \
	(PDO_FIXED_VOLT(mv) | PDO_FIXED_CURR(ma) | (flags))

#define PDO_VAR_MAX_VOLT(mv) ((((mv) / 50) & 0x3FF) << 20)
#define PDO_VAR_MIN_VOLT(mv) ((((mv) / 50) & 0x3FF) << 10)
#define PDO_VAR_OP_CURR(ma) ((((ma) / 10) & 0x3FF) << 0)

#define PDO_VAR(min_mv, max_mv, op_ma)                         \
	(PDO_VAR_MIN_VOLT(min_mv) | PDO_VAR_MAX_VOLT(max_mv) | \
	 PDO_VAR_OP_CURR(op_ma) | PDO_TYPE_VARIABLE)

#define PDO_BATT_MAX_VOLT(mv) ((((mv) / 50) & 0x3FF) << 20)
#define PDO_BATT_MIN_VOLT(mv) ((((mv) / 50) & 0x3FF) << 10)
#define PDO_BATT_OP_POWER(mw) ((((mw) / 250) & 0x3FF) << 0)

#define PDO_BATT(min_mv, max_mv, op_mw)                          \
	(PDO_BATT_MIN_VOLT(min_mv) | PDO_BATT_MAX_VOLT(max_mv) | \
	 PDO_BATT_OP_POWER(op_mw) | PDO_TYPE_BATTERY)

#define PDO_AUG_MAX_VOLT(mv) ((((mv) / 100) & 0xFF) << 17)
#define PDO_AUG_MIN_VOLT(mv) ((((mv) / 100) & 0xFF) << 8)
#define PDO_AUG_MAX_CURR(ma) ((((ma) / 50) & 0x7F) << 0)

#define PDO_AUG(min_mv, max_mv, max_ma)                        \
	(PDO_AUG_MIN_VOLT(min_mv) | PDO_AUG_MAX_VOLT(max_mv) | \
	 PDO_AUG_MAX_CURR(max_ma) | PDO_TYPE_AUGMENTED)

/* RDO : Request Data Object */
#define RDO_OBJ_POS(n) (((n) & 0xF) << 28)
#define RDO_POS(rdo) (((rdo) >> 28) & 0xF)
#define RDO_GIVE_BACK BIT(27)
#define RDO_CAP_MISMATCH BIT(26)
#define RDO_COMM_CAP BIT(25)
#define RDO_NO_SUSPEND BIT(24)
#define RDO_EPR_MODE_CAPABLE BIT(22)
#define RDO_FIXED_VAR_OP_CURR(ma) ((((ma) / 10) & 0x3FF) << 10)
#define RDO_FIXED_VAR_MAX_CURR(ma) ((((ma) / 10) & 0x3FF) << 0)

#define RDO_BATT_OP_POWER(mw) ((((mw) / 250) & 0x3FF) << 10)
#define RDO_BATT_MAX_POWER(mw) ((((mw) / 250) & 0x3FF) << 0)

#define RDO_FIXED(n, op_ma, max_ma, flags)                         \
	(RDO_OBJ_POS(n) | (flags) | RDO_FIXED_VAR_OP_CURR(op_ma) | \
	 RDO_FIXED_VAR_MAX_CURR(max_ma))

#define RDO_BATT(n, op_mw, max_mw, flags)                      \
	(RDO_OBJ_POS(n) | (flags) | RDO_BATT_OP_POWER(op_mw) | \
	 RDO_BATT_MAX_POWER(max_mw))

/* BDO : BIST Data Object
 * 31:28 BIST Mode
 *       In PD 3.0, all but Carrier Mode 2 (as Carrier Mode) and Test Data are
 *       reserved, with a new BIST shared mode added
 * 27:16 Reserved
 * 15:0  Returned error counters (reserved in PD 3.0)
 */
#define BDO_MODE_RECV (BIST_RECEIVER_MODE << 28)
#define BDO_MODE_TRANSMIT (BIST_TRANSMIT_MODE << 28)
#define BDO_MODE_COUNTERS (BIST_RETURNED_COUNTER << 28)
#define BDO_MODE_CARRIER0 (BIST_CARRIER_MODE_0 << 28)
#define BDO_MODE_CARRIER1 (BIST_CARRIER_MODE_1 << 28)
#define BDO_MODE_CARRIER2 (BIST_CARRIER_MODE_2 << 28)
#define BDO_MODE_CARRIER3 (BIST_CARRIER_MODE_3 << 28)
#define BDO_MODE_EYE (BIST_EYE_PATTERN << 28)
#define BDO_MODE_TEST_DATA (BIST_TEST_DATA << 28)
#define BDO_MODE_SHARED_ENTER (BIST_SHARED_MODE_ENTER << 28)
#define BDO_MODE_SHARED_EXIT (BIST_SHARED_MODE_EXIT << 28)

#define BDO(mode, cnt) ((mode) | ((cnt) & 0xFFFF))

#define BIST_MODE(n) ((n) >> 28)
#define BIST_ERROR_COUNTER(n) ((n) & 0xffff)
#define BIST_RECEIVER_MODE 0
#define BIST_TRANSMIT_MODE 1
#define BIST_RETURNED_COUNTER 2
#define BIST_CARRIER_MODE_0 3
#define BIST_CARRIER_MODE_1 4
#define BIST_CARRIER_MODE_2 5
#define BIST_CARRIER_MODE_3 6
#define BIST_EYE_PATTERN 7
#define BIST_TEST_DATA 8
#define BIST_SHARED_MODE_ENTER 9
#define BIST_SHARED_MODE_EXIT 10

#define SVID_DISCOVERY_MAX 16

/* Timers */
#define PD_T_SINK_TX (18 * MSEC) /* between 16ms and 20 */
#define PD_T_CHUNKING_NOT_SUPPORTED (45 * MSEC) /* between 40ms and 50ms */
#define PD_T_HARD_RESET_COMPLETE (5 * MSEC) /* between 4ms and 5ms*/
#define PD_T_HARD_RESET_RETRY (1 * MSEC) /* 1ms */
#define PD_T_SEND_SOURCE_CAP (100 * MSEC) /* between 100ms and 200ms */
#define PD_T_SINK_WAIT_CAP (575 * MSEC) /* between 310ms and 620ms */
#define PD_T_SINK_TRANSITION (35 * MSEC) /* between 20ms and 35ms */
#define PD_T_SOURCE_ACTIVITY (45 * MSEC) /* between 40ms and 50ms */
#define PD_T_ENTER_EPR (500 * MSEC) /* between 450ms and 550ms */
/*
 * Adjusting for TCPMv2 PD2 Compliance. In tests like TD.PD.SRC.E5 this
 * value is the duration before the Hard Reset can be sent. Setting the
 * timer value to the maximum will delay sending the HardReset until
 * after the window has closed instead of when it is desired at the
 * beginning of the window.
 * Leaving TCPMv1 as it was as there are no current requests to adjust
 * for compliance on the old stack and making this change  breaks the
 * usb_pd unit test.
 */
#ifndef CONFIG_USB_PD_TCPMV2
#define PD_T_SENDER_RESPONSE (30 * MSEC) /* between 24ms and 30ms */
#else
/*
 * In USB Power Delivery Specification Revision 3.1, Version 1.5,
 * the tSenderResponse have changed to min 26/ max 32 ms.
 */
#define PD_T_SENDER_RESPONSE (26 * MSEC) /* between 26ms and 32ms */
#endif
#define PD_T_PS_TRANSITION (500 * MSEC) /* between 450ms and 550ms */
/*
 * This is adjusted for PD3.1 Compliance test TEST.PD.PROT.SRC.10.
 */
#define PD_T_PS_SOURCE_ON (435 * MSEC) /* between 390ms and 480ms */
#define PD_T_PS_SOURCE_OFF (835 * MSEC) /* between 750ms and 920ms */
#define PD_T_PS_HARD_RESET (25 * MSEC) /* between 25ms and 35ms */
#define PD_T_ERROR_RECOVERY (240 * MSEC) /* min 240ms if sourcing VConn */
#define PD_T_CC_DEBOUNCE (100 * MSEC) /* between 100ms and 200ms */
/* DRP_SNK + DRP_SRC must be between 50ms and 100ms with 30%-70% duty cycle */
#define PD_T_DRP_SNK (40 * MSEC) /* toggle time for sink DRP */
#define PD_T_DRP_SRC (30 * MSEC) /* toggle time for source DRP */
#define PD_T_DEBOUNCE (15 * MSEC) /* between 10ms and 20ms */
#define PD_T_TRY_CC_DEBOUNCE (15 * MSEC) /* between 10ms and 20ms */
#define PD_T_SINK_ADJ (55 * MSEC) /* between tPDDebounce and 60ms */
#define PD_T_SRC_RECOVER (760 * MSEC) /* between 660ms and 1000ms */
#define PD_T_SRC_RECOVER_MAX (1000 * MSEC) /* 1000ms */
#define PD_T_SRC_TURN_ON (275 * MSEC) /* 275ms */
#define PD_T_SAFE_0V (650 * MSEC) /* 650ms */
#define PD_T_NO_RESPONSE (5500 * MSEC) /* between 4.5s and 5.5s */
#define PD_T_BIST_TRANSMIT (50 * MSEC) /* 50ms (for task_wait arg) */
#define PD_T_BIST_RECEIVE (60 * MSEC) /* 60ms (time to process bist) */
#define PD_T_BIST_CONT_MODE (55 * MSEC) /* 30ms to 60ms */
#define PD_T_VCONN_SOURCE_ON (100 * MSEC) /* 100ms */
#define PD_T_DRP_TRY (125 * MSEC) /* between 75ms and 150ms */
#define PD_T_TRY_TIMEOUT (550 * MSEC) /* between 550ms and 1100ms */
#define PD_T_TRY_WAIT (600 * MSEC) /* Wait time for TryWait.SNK */
#define PD_T_SINK_REQUEST (100 * MSEC) /* 100ms before next request */
#define PD_T_PD_DEBOUNCE (15 * MSEC) /* between 10ms and 20ms */
#define PD_T_CHUNK_SENDER_RESPONSE (25 * MSEC) /* 25ms */
#define PD_T_CHUNK_SENDER_REQUEST (25 * MSEC) /* 25ms */
#define PD_T_SWAP_SOURCE_START (25 * MSEC) /* Min of 20ms */
#define PD_T_RP_VALUE_CHANGE (20 * MSEC) /* 20ms */
#define PD_T_SRC_DISCONNECT (15 * MSEC) /* 15ms */
#define PD_T_SRC_TRANSITION (25 * MSEC) /* 25ms to 35 ms */
#define PD_T_VCONN_STABLE (50 * MSEC) /* 50ms */
#define PD_T_DISCOVER_IDENTITY (45 * MSEC) /* between 40ms and 50ms */
#define PD_T_SYSJUMP (1000 * MSEC) /* 1s */
#define PD_T_PR_SWAP_WAIT (100 * MSEC) /* tPRSwapWait 100ms */
#define PD_T_DATA_RESET (225 * MSEC) /* between 200ms and 250ms */
#define PD_T_DATA_RESET_FAIL (300 * MSEC) /* 300ms */
#define PD_T_VCONN_REAPPLIED (10 * MSEC) /* between 10ms and 20ms */
#define PD_T_VCONN_DISCHARGE (240 * MSEC) /* between 160ms and 240ms */
#define PD_T_SINK_EPR_KEEP_ALIVE (375 * MSEC) /* between 250ms and 500ms */

/*
 * Non-spec timer to prevent going Unattached if Vbus drops before a partner FRS
 * signal comes through.  This timer should be shorter than tSinkDisconnect
 * (40ms) to ensure we still transition out of Attached.SNK in time.
 */
#define PD_T_FRS_VBUS_DEBOUNCE (5 * MSEC)

/* number of edges and time window to detect CC line is not idle */
#define PD_RX_TRANSITION_COUNT 3
#define PD_RX_TRANSITION_WINDOW 20 /* between 12us and 20us */

/* from USB Type-C Specification Table 5-1 */
#define PD_T_AME (1 * SECOND) /* timeout from UFP attach to Alt Mode Entry */

/* VDM Timers ( USB PD Spec Rev2.0 Table 6-30 )*/
#define PD_T_VDM_BUSY (50 * MSEC) /* at least 50ms */
#define PD_T_VDM_E_MODE (25 * MSEC) /* enter/exit the same max */
#define PD_T_VDM_RCVR_RSP (15 * MSEC) /* max of 15ms */
#define PD_T_VDM_SNDR_RSP (30 * MSEC) /* max of 30ms */
#define PD_T_VDM_WAIT_MODE_E (100 * MSEC) /* enter/exit the same max */

/* CTVPD Timers ( USB Type-C ECN Table 4-27 ) */
#define PD_T_VPDDETACH (20 * MSEC) /* max of 20*MSEC */
#define PD_T_VPDCTDD (4 * MSEC) /* max of 4ms */
#define PD_T_VPDDISABLE (25 * MSEC) /* min of 25ms */

/* Voltage thresholds in mV (Table 7-24, PD 3.0 Version 2.0 Spec) */
#define PD_V_SAFE0V_MAX 800
#define PD_V_SAFE5V_MIN 4750
#define PD_V_SAFE5V_NOM 5000
#define PD_V_SAFE5V_MAX 5500

/* USB Type-C voltages in mV (Table 4-3, USB Type-C Release 2.0 Spec) */
#define PD_V_SINK_DISCONNECT_MAX 3670
/* TODO(b/149530538): Add equation for vSinkDisconnectPD */

/* Maximum SPR voltage in mV offered by PD 3.0 Version 2.0 Spec */
#define PD_REV3_MAX_VOLTAGE 20000

/* Maximum SPR voltage in mV */
#define PD_MAX_SPR_VOLTAGE 20000

/* Maximum EPR voltage in mV */
#define PD_MAX_EPR_VOLTAGE 48000

/* Power in mW at which we will automatically charge from a DRP partner */
#define PD_DRP_CHARGE_POWER_MIN 27000

/* function table for entered mode */
struct amode_fx {
	int (*status)(int port, uint32_t *payload);
	int (*config)(int port, uint32_t *payload);
};

/* function table for alternate mode capable responders */
struct svdm_response {
	/**
	 * Gets VDM response messages
	 *
	 * @param port    USB-C Port number
	 * @param payload buffer used to pass input data and store output data
	 * @return        number of data objects in payload; <0 means BUSY;
	 * =0 means NAK.
	 */
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
	PD_DISC_NEEDED = 0, /* Cable or partner still needs to be probed */
	PD_DISC_COMPLETE, /* Successfully probed, valid to read VDO */
	PD_DISC_FAIL, /* Cable did not respond, or Discover* NAK */
};

/* Mode discovery state for a particular SVID with a particular transmit type */
struct svid_mode_data {
	/* The SVID for which modes are discovered */
	uint16_t svid;
	/* The number of modes discovered for this SVID */
	int mode_cnt;
	/* The discovered mode VDOs */
	uint32_t mode_vdo[VDO_MAX_OBJECTS];
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
/* This is *NOT* a hash-table, it's a table (ring-buffer) of hashes */
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
	const struct svid_mode_data *data;
};

enum hpd_event {
	hpd_none,
	hpd_low,
	hpd_high,
	hpd_irq,
};

/* DisplayPort flags */
#define DP_FLAGS_DP_ON BIT(0) /* Display port mode is on */
#define DP_FLAGS_HPD_HI_PENDING BIT(1) /* Pending HPD_HI */

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

	uint32_t raw_value[VDO_MAX_OBJECTS];
};
BUILD_ASSERT(sizeof(union disc_ident_ack) ==
	     sizeof(uint32_t) * (VDO_MAX_OBJECTS));

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

/* Discover and possibly enter modes for all SOP* communications when enabled */
#ifdef CONFIG_USB_PD_DECODE_SOP
#define DISCOVERY_TYPE_COUNT (TCPCI_MSG_SOP_PRIME + 1)
#define AMODE_TYPE_COUNT (TCPCI_MSG_SOP_PRIME_PRIME + 1)
#else
#define DISCOVERY_TYPE_COUNT (TCPCI_MSG_SOP + 1)
#define AMODE_TYPE_COUNT (TCPCI_MSG_SOP + 1)
#endif

enum usb_pd_svdm_ver {
	SVDM_VER_1_0,
	SVDM_VER_2_0,
	SVDM_VER_2_1,
};

/* Discovery results for a port partner (SOP) or cable plug (SOP') */
struct pd_discovery {
	/* Identity data */
	union disc_ident_ack identity;
	/* Identity VDO count */
	int identity_cnt;
	/* svdm version */
	enum usb_pd_svdm_ver svdm_vers;
	/* Supported SVIDs and corresponding mode VDOs */
	struct svid_mode_data svids[SVID_DISCOVERY_MAX];
	/* index of SVID currently being operated on */
	int svid_idx;
	/* Count of SVIDs discovered */
	int svid_cnt;
	/* Identity discovery state */
	enum pd_discovery_state identity_discovery;
	/* SVID discovery state */
	enum pd_discovery_state svids_discovery;
};

/* Active modes for a partner (SOP, SOP', or SOP'') */
struct partner_active_modes {
	/*  Active modes */
	struct svdm_amode_data amodes[PD_AMODE_COUNT];
	/* Next index to insert DFP alternate mode into amodes */
	int amode_idx;
};

/*
 * VDO : Vendor Defined Message Object
 * VDM object is minimum of VDM header + 6 additional data objects.
 */
#define VDO_HDR_SIZE 1

#define PD_VDO_INVALID -1

/*
 * VDM header
 * ----------
 * <31:16>  :: SVID
 * <15>     :: VDM type ( 1b == structured, 0b == unstructured )
 * <14:13>  :: SVDM version major (00b == <= Vers 2.0, 01b == Vers 2.(minor))
 * <12:11>  :: SVDM version minor (00b == <= Vers 2.0, 01b == Vers 2.1)
 * <10:8>   :: object position (1-7 valid ... used for enter/exit mode only)
 * <7:6>    :: command type (SVDM only?)
 * <5>      :: reserved (SVDM), command type (UVDM)
 * <4:0>    :: command
 */
#define VDO(vid, type, custom) \
	(((vid) << 16) | ((type) << 15) | ((custom) & 0x7FFF))

#define VDO_SVDM_TYPE BIT(15)
#define VDO_SVDM_VERS_MAJOR(x) (x << 13)
#define VDO_SVDM_VERS_MINOR(x) (x << 11)
#define VDO_OPOS(x) (x << 8)
#define VDO_CMDT(x) (x << 6)
#define VDO_OPOS_MASK VDO_OPOS(0x7)
#define VDO_CMDT_MASK VDO_CMDT(0x3)
#define VDO_SVDM_VERS_MASK (VDO_SVDM_VERS_MAJOR(0x3) | VDO_SVDM_VERS_MINOR(0x3))

#define CMDT_INIT 0
#define CMDT_RSP_ACK 1
#define CMDT_RSP_NAK 2
#define CMDT_RSP_BUSY 3

/* reserved for SVDM ... for Google UVDM */
#define VDO_SRC_INITIATOR (0 << 5)
#define VDO_SRC_RESPONDER BIT(5)

#define CMD_DISCOVER_IDENT 1
#define CMD_DISCOVER_SVID 2
#define CMD_DISCOVER_MODES 3
#define CMD_ENTER_MODE 4
#define CMD_EXIT_MODE 5
#define CMD_ATTENTION 6
#define CMD_DP_STATUS 16
#define CMD_DP_CONFIG 17

#define VDO_CMD_VENDOR(x) (((10 + (x)) & 0x1f))

/* ChromeOS specific commands */
#define VDO_CMD_VERSION VDO_CMD_VENDOR(0)
#define VDO_CMD_SEND_INFO VDO_CMD_VENDOR(1)
#define VDO_CMD_READ_INFO VDO_CMD_VENDOR(2)
#define VDO_CMD_REBOOT VDO_CMD_VENDOR(5)
#define VDO_CMD_FLASH_ERASE VDO_CMD_VENDOR(6)
#define VDO_CMD_FLASH_WRITE VDO_CMD_VENDOR(7)
#define VDO_CMD_ERASE_SIG VDO_CMD_VENDOR(8)
#define VDO_CMD_PING_ENABLE VDO_CMD_VENDOR(10)
#define VDO_CMD_CURRENT VDO_CMD_VENDOR(11)
#define VDO_CMD_FLIP VDO_CMD_VENDOR(12)
#define VDO_CMD_GET_LOG VDO_CMD_VENDOR(13)
#define VDO_CMD_CCD_EN VDO_CMD_VENDOR(14)

#define PD_VDO_VID(vdo) ((vdo) >> 16)
#define PD_VDO_SVDM(vdo) (((vdo) >> 15) & 1)
#define PD_VDO_OPOS(vdo) (((vdo) >> 8) & 0x7)
#define PD_VDO_CMD(vdo) ((vdo) & 0x1f)
#define PD_VDO_CMDT(vdo) (((vdo) >> 6) & 0x3)
#define PD_VDO_SVDM_VERS_MAJOR(vdo) (((vdo) >> 13) & 0x3)
#define PD_VDO_SVDM_VERS_MINOR(vdo) (((vdo) >> 11) & 0x3)

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
#define VDO_INDEX_HDR 0
#define VDO_INDEX_IDH 1
#define VDO_INDEX_CSTAT 2
#define VDO_INDEX_CABLE 3
#define VDO_INDEX_PRODUCT 3
#define VDO_INDEX_AMA 4
#define VDO_INDEX_PTYPE_UFP1_VDO 4
#define VDO_INDEX_PTYPE_CABLE1 4
#define VDO_INDEX_PTYPE_UFP2_VDO 5
#define VDO_INDEX_PTYPE_CABLE2 5
#define VDO_INDEX_PTYPE_DFP_VDO 6
#define VDO_I(name) VDO_INDEX_##name

/* PD Rev 2.0 ID Header VDO */
#define VDO_IDH(usbh, usbd, ptype, is_modal, vid)              \
	((usbh) << 31 | (usbd) << 30 | ((ptype) & 0x7) << 27 | \
	 (is_modal) << 26 | ((vid) & 0xffff))

/* PD Rev 3.0 ID Header VDO */
#define VDO_IDH_REV30(usbh, usbd, ptype_u, is_modal, ptype_d, ctype, vid) \
	(VDO_IDH(usbh, usbd, ptype_u, is_modal, vid) |                    \
	 ((ptype_d) & 0x7) << 23 | ((ctype) & 0x3) << 21)

#define PD_IDH_PTYPE(vdo) (((vdo) >> 27) & 0x7)
#define PD_IDH_IS_MODAL(vdo) (((vdo) >> 26) & 0x1)
#define PD_IDH_VID(vdo) ((vdo) & 0xffff)

#define VDO_CSTAT(tid) ((tid) & 0xfffff)
#define PD_CSTAT_TID(vdo) ((vdo) & 0xfffff)

#define VDO_PRODUCT(pid, bcd) (((pid) & 0xffff) << 16 | ((bcd) & 0xffff))
#define PD_PRODUCT_PID(vdo) (((vdo) >> 16) & 0xffff)

/* Max Attention length is header + 1 VDO */
#define PD_ATTENTION_MAX_VDO 2

/*
 * 6.4.10 EPR_Mode Message (PD Rev 3.1)
 */

enum pd_eprmdo_action {
	/* 0x00: Reserved */
	PD_EPRMDO_ACTION_ENTER = 0x01,
	PD_EPRMDO_ACTION_ENTER_ACK = 0x02,
	PD_EPRMDO_ACTION_ENTER_SUCCESS = 0x03,
	PD_EPRMDO_ACTION_ENTER_FAILED = 0x04,
	PD_EPRMDO_ACTION_EXIT = 0x05,
	/* 0x06 ... 0xFF: Reserved */
} __packed;
BUILD_ASSERT(sizeof(enum pd_eprmdo_action) == 1);

enum pd_eprmdo_enter_failed_data {
	PD_EPRMDO_ENTER_FAILED_DATA_UNKNOWN = 0x00,
	PD_EPRMDO_ENTER_FAILED_DATA_CABLE = 0x01,
	PD_EPRMDO_ENTER_FAILED_DATA_VCONN = 0x02,
	PD_EPRMDO_ENTER_FAILED_DATA_RDO = 0x03,
	PD_EPRMDO_ENTER_FAILED_DATA_UNABLE = 0x04,
	PD_EPRMDO_ENTER_FAILED_DATA_PDO = 0x05,
} __packed;
BUILD_ASSERT(sizeof(enum pd_eprmdo_enter_failed_data) == 1);

struct eprmdo {
	uint16_t reserved;
	enum pd_eprmdo_enter_failed_data data;
	enum pd_eprmdo_action action;
};
BUILD_ASSERT(sizeof(struct eprmdo) == 4);

/*
 * 6.5.14 Extended Control Message
 */
enum pd_ext_ctrl_msg_type {
	/* 0: Reserved */
	PD_EXT_CTRL_EPR_GET_SOURCE_CAP = 1,
	PD_EXT_CTRL_EPR_GET_SINK_CAP = 2,
	PD_EXT_CTRL_EPR_KEEPALIVE = 3,
	PD_EXT_CTRL_EPR_KEEPALIVE_ACK = 4,
	/* 5-255: Reserved */
} __packed;
BUILD_ASSERT(sizeof(enum pd_ext_ctrl_msg_type) == 1);

/* Extended Control Data Block (ECDB) */
struct pd_ecdb {
	uint8_t type;
	uint8_t data;
} __packed;

/* PD Rev 3.1 Revision Message Data Object (RMDO) */
struct rmdo {
	uint32_t reserved : 16;
	uint32_t minor_ver : 4;
	uint32_t major_ver : 4;
	uint32_t minor_rev : 4;
	uint32_t major_rev : 4;
};

/* Confirm RMDO is 32 bits. */
BUILD_ASSERT(sizeof(struct rmdo) == 4);

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
	PD_CONTROLLER,
};

/* Protocol revision */
enum pd_rev_type {
	PD_REV10,
	PD_REV20,
	PD_REV30,
};

#ifdef CONFIG_USB_PD_REV30
#define PD_REVISION PD_REV30
#else
#define PD_REVISION PD_REV20
#endif

#if defined(CONFIG_USB_PD_TCPMV1)
#define PD_STACK_VERSION TCPMV1
#elif defined(CONFIG_USB_PD_TCPMV2)
#define PD_STACK_VERSION TCPMV2
#elif defined(CONFIG_USB_PD_CONTROLLER)
#define PD_STACK_VERSION PD_CONTROLLER
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

	/* Cable revision */
	enum pd_rev_type rev;
};

/* Note: These flags are only used for TCPMv1 */
/* Check if Thunderbolt-compatible mode enabled */
#define CABLE_FLAGS_TBT_COMPAT_ENABLE BIT(0)
/* Flag to limit speed to TBT Gen 2 passive cable */
#define CABLE_FLAGS_TBT_COMPAT_LIMIT_SPEED BIT(1)
/* Flag for checking if device is USB4.0 capable */
#define CABLE_FLAGS_USB4_CAPABLE BIT(2)
/* Flag for entering ENTER_USB mode */
#define CABLE_FLAGS_ENTER_USB_MODE BIT(3)

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
#define VDO_MODE_DP(snkp, srcp, usb, gdr, sign, sdir)                      \
	(((snkp) & 0xff) << 16 | ((srcp) & 0xff) << 8 | ((usb) & 1) << 7 | \
	 ((gdr) & 1) << 6 | ((sign) & 0xF) << 2 | ((sdir) & 0x3))

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

#define MODE_DP_V13 0x1
#define MODE_DP_GEN2 0x2

#define MODE_DP_SNK 0x1
#define MODE_DP_SRC 0x2
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
#define PD_DP_PIN_CAPS(x)                                                   \
	((((x) >> MODE_DP_CABLE_SHIFT) & 0x1) ?                             \
		 (((x) >> MODE_DP_UFP_PIN_SHIFT) & MODE_DP_PIN_CAPS_MASK) : \
		 (((x) >> MODE_DP_DFP_PIN_SHIFT) & MODE_DP_PIN_CAPS_MASK))

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
#define VDO_DP_STATUS(irq, lvl, amode, usbc, mf, en, lp, conn)      \
	(((irq) & 1) << 8 | ((lvl) & 1) << 7 | ((amode) & 1) << 6 | \
	 ((usbc) & 1) << 5 | ((mf) & 1) << 4 | ((en) & 1) << 3 |    \
	 ((lp) & 1) << 2 | ((conn & 0x3) << 0))

#define PD_VDO_DPSTS_MF_MASK BIT(4)

#define PD_VDO_DPSTS_HPD_IRQ(x) (((x) >> 8) & 1)
#define PD_VDO_DPSTS_HPD_LVL(x) (((x) >> 7) & 1)
#define PD_VDO_DPSTS_MF_PREF(x) (((x) >> 4) & 1)

/* Per DisplayPort Spec v1.3 Section 3.3 */
#define HPD_USTREAM_DEBOUNCE_LVL (2 * MSEC)
#define HPD_USTREAM_DEBOUNCE_IRQ (250)
#define HPD_DSTREAM_DEBOUNCE_IRQ (500) /* between 500-1000us */

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
#define PD_DP_CFG_PIN(x) \
	((((x) >> 8) & 0xff) ? (((x) >> 8) & 0xff) : (((x) >> 16) & 0xff))
/*
 * ChromeOS specific PD device Hardware IDs. Used to identify unique
 * products and used in VDO_INFO. Note this field is 10 bits.
 */
#define USB_PD_HW_DEV_ID_RESERVED 0
#define USB_PD_HW_DEV_ID_ZINGER 1
#define USB_PD_HW_DEV_ID_MINIMUFFIN 2
#define USB_PD_HW_DEV_ID_DINGDONG 3
#define USB_PD_HW_DEV_ID_HOHO 4
#define USB_PD_HW_DEV_ID_HONEYBUNS 5

/*
 * ChromeOS specific VDO_CMD_READ_INFO responds with device info including:
 * RW Hash: First 20 bytes of SHA-256 of RW (20 bytes)
 * HW Device ID: unique descriptor for each ChromeOS model (2 bytes)
 *               top 6 bits are minor revision, bottom 10 bits are major
 * SW Debug Version: Software version useful for debugging (15 bits)
 * IS RW: True if currently in RW, False otherwise (1 bit)
 */
#define VDO_INFO(id, id_minor, ver, is_rw)                                 \
	((id_minor) << 26 | ((id) & 0x3ff) << 16 | ((ver) & 0x7fff) << 1 | \
	 ((is_rw) & 1))
#define VDO_INFO_HW_DEV_ID(x) ((x) >> 16)
#define VDO_INFO_SW_DBG_VER(x) (((x) >> 1) & 0x7fff)
#define VDO_INFO_IS_RW(x) ((x) & 1)

#define HW_DEV_ID_MAJ(x) (x & 0x3ff)
#define HW_DEV_ID_MIN(x) ((x) >> 10)

/* USB-IF SIDs */
#define USB_SID_PD 0xff00 /* power delivery */
#define USB_SID_DISPLAYPORT 0xff01

#define USB_GOOGLE_TYPEC_URL "http://www.google.com/chrome/devices/typec"
/* USB Vendor ID assigned to Google LLC */
#define USB_VID_GOOGLE 0x18d1

/* Other Vendor IDs */
#define USB_VID_APPLE 0x05ac
#define USB_PID1_APPLE 0x1012
#define USB_PID2_APPLE 0x1013

#define USB_VID_HP 0x03F0
#define USB_PID_HP_USB_C_DOCK_G5 0x036B
#define USB_PID_HP_USB_C_A_UNIV_DOCK_G2 0x096B
#define USB_PID_HP_E24D_DOCK_MONITOR 0x0467
#define USB_PID_HP_ELITE_E233_MONITOR 0x1747
#define USB_PID_HP_E244D_DOCK_MONITOR 0x056D
#define USB_PID_HP_E274D_DOCK_MONITOR 0x016E

#define USB_VID_INTEL 0x8087

#define USB_VID_FRAMEWORK 0X32ac
#define USB_PID_FRAMEWORK_HDMI_CARD 0X2
#define USB_PID_FRAMEWORK_DP_CARD 0X3

/* Timeout for message receive in microseconds */
#define USB_PD_RX_TMOUT_US 1800

/* Power button press length triggered by USB PD short button press */
#define USB_PD_SHORT_BUTTON_PRESS_MS 500

/* --- Protocol layer functions --- */

enum pd_states {
	PD_STATE_DISABLED, /* C0  */
	PD_STATE_SUSPENDED, /* C1  */
	PD_STATE_SNK_DISCONNECTED, /* C2  */
	PD_STATE_SNK_DISCONNECTED_DEBOUNCE, /* C3  */
	PD_STATE_SNK_HARD_RESET_RECOVER, /* C4  */
	PD_STATE_SNK_DISCOVERY, /* C5  */
	PD_STATE_SNK_REQUESTED, /* C6  */
	PD_STATE_SNK_TRANSITION, /* C7  */
	PD_STATE_SNK_READY, /* C8  */
	PD_STATE_SNK_SWAP_INIT, /* C9  */
	PD_STATE_SNK_SWAP_SNK_DISABLE, /* C10 */
	PD_STATE_SNK_SWAP_SRC_DISABLE, /* C11 */
	PD_STATE_SNK_SWAP_STANDBY, /* C12 */
	PD_STATE_SNK_SWAP_COMPLETE, /* C13 */
	PD_STATE_SRC_DISCONNECTED, /* C14 */
	PD_STATE_SRC_DISCONNECTED_DEBOUNCE, /* C15 */
	PD_STATE_SRC_HARD_RESET_RECOVER, /* C16 */
	PD_STATE_SRC_STARTUP, /* C17 */
	PD_STATE_SRC_DISCOVERY, /* C18 */
	PD_STATE_SRC_NEGOCIATE, /* C19 */
	PD_STATE_SRC_ACCEPTED, /* C20 */
	PD_STATE_SRC_POWERED, /* C21 */
	PD_STATE_SRC_TRANSITION, /* C22 */
	PD_STATE_SRC_READY, /* C23 */
	PD_STATE_SRC_GET_SINK_CAP, /* C24 */
	PD_STATE_DR_SWAP, /* C25 */
	PD_STATE_SRC_SWAP_INIT, /* C26 */
	PD_STATE_SRC_SWAP_SNK_DISABLE, /* C27 */
	PD_STATE_SRC_SWAP_SRC_DISABLE, /* C28 */
	PD_STATE_SRC_SWAP_STANDBY, /* C29 */
	PD_STATE_VCONN_SWAP_SEND, /* C30 */
	PD_STATE_VCONN_SWAP_INIT, /* C31 */
	PD_STATE_VCONN_SWAP_READY, /* C32 */
	PD_STATE_SOFT_RESET, /* C33 */
	PD_STATE_HARD_RESET_SEND, /* C34 */
	PD_STATE_HARD_RESET_EXECUTE, /* C35 */
	PD_STATE_BIST_RX, /* C36 */
	PD_STATE_BIST_TX, /* C37 */
	PD_STATE_DRP_AUTO_TOGGLE, /* C38 */
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
#define PD_FLAGS_PING_ENABLED BIT(0) /* SRC_READY pings enabled */
#define PD_FLAGS_PARTNER_DR_POWER BIT(1) /* port partner is dualrole power */
#define PD_FLAGS_PARTNER_DR_DATA BIT(2) /* port partner is dualrole data */
#define PD_FLAGS_CHECK_IDENTITY BIT(3) /* discover identity in READY */
#define PD_FLAGS_SNK_CAP_RECVD BIT(4) /* sink capabilities received */
#define PD_FLAGS_TCPC_DRP_TOGGLE BIT(5) /* TCPC-controlled DRP toggling */
#define PD_FLAGS_EXPLICIT_CONTRACT BIT(6) /* explicit pwr contract in place */
#define PD_FLAGS_VBUS_NEVER_LOW BIT(7) /* VBUS input has never been low */
#define PD_FLAGS_PREVIOUS_PD_CONN BIT(8) /* previously PD connected */
#define PD_FLAGS_CHECK_PR_ROLE BIT(9) /* check power role in READY */
#define PD_FLAGS_CHECK_DR_ROLE BIT(10) /* check data role in READY */
#define PD_FLAGS_PARTNER_UNCONSTR BIT(11) /* port partner unconstrained pwr */
#define PD_FLAGS_VCONN_ON BIT(12) /* vconn is being sourced */
#define PD_FLAGS_TRY_SRC BIT(13) /* Try.SRC states are active */
#define PD_FLAGS_PARTNER_USB_COMM BIT(14) /* port partner is USB comms */
#define PD_FLAGS_UPDATE_SRC_CAPS BIT(15) /* send new source capabilities */
#define PD_FLAGS_TS_DTS_PARTNER BIT(16) /* partner has rp/rp or rd/rd */
/*
 * These PD_FLAGS_LPM* flags track the software state (PD_LPM_FLAGS_REQUESTED)
 * and hardware state (PD_LPM_FLAGS_ENGAGED) of the TCPC low power mode.
 * PD_FLAGS_LPM_TRANSITION is set while the HW is transitioning into or out of
 * low power (when PD_LPM_FLAGS_ENGAGED is changing).
 */
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
#define PD_FLAGS_LPM_REQUESTED BIT(17) /* Tracks SW LPM state */
#define PD_FLAGS_LPM_ENGAGED BIT(18) /* Tracks HW LPM state */
#define PD_FLAGS_LPM_TRANSITION BIT(19) /* Tracks HW LPM transition */
#define PD_FLAGS_LPM_EXIT BIT(19) /* Tracks HW LPM exit */
#endif
/*
 * Tracks whether port negotiation may have stalled due to not starting reset
 * timers in SNK_DISCOVERY
 */
#define PD_FLAGS_SNK_WAITING_BATT BIT(21)
/* Check vconn state in READY */
#define PD_FLAGS_CHECK_VCONN_STATE BIT(22)
#endif /* CONFIG_USB_PD_TCPMV1 */

/* Per-port battery backed RAM flags */
#define PD_BBRMFLG_EXPLICIT_CONTRACT BIT(0)
#define PD_BBRMFLG_POWER_ROLE BIT(1)
#define PD_BBRMFLG_DATA_ROLE BIT(2)
#define PD_BBRMFLG_VCONN_ROLE BIT(3)
#define PD_BBRMFLG_DBGACC_ROLE BIT(4)

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

/*
 * Device Policy Manager Requests.
 * NOTE: These are usually set by host commands from the AP.
 */
enum pd_dpm_request {
	DPM_REQUEST_DR_SWAP = BIT(0),
	DPM_REQUEST_PR_SWAP = BIT(1),
	DPM_REQUEST_VCONN_SWAP = BIT(2),
	DPM_REQUEST_GOTO_MIN = BIT(3),
	DPM_REQUEST_SRC_CAP_CHANGE = BIT(4),
	DPM_REQUEST_GET_SNK_CAPS = BIT(5),
	DPM_REQUEST_SEND_PING = BIT(6),
	DPM_REQUEST_SOURCE_CAP = BIT(7),
	DPM_REQUEST_NEW_POWER_LEVEL = BIT(8),
	DPM_REQUEST_VDM = BIT(9),
	DPM_REQUEST_BIST_TX = BIT(10),
	DPM_REQUEST_SNK_STARTUP = BIT(11),
	DPM_REQUEST_SRC_STARTUP = BIT(12),
	DPM_REQUEST_HARD_RESET_SEND = BIT(13),
	DPM_REQUEST_SOFT_RESET_SEND = BIT(14),
	DPM_REQUEST_PORT_DISCOVERY = BIT(15),
	DPM_REQUEST_SEND_ALERT = BIT(16),
	DPM_REQUEST_ENTER_USB = BIT(17),
	DPM_REQUEST_GET_SRC_CAPS = BIT(18),
	DPM_REQUEST_EXIT_MODES = BIT(19),
	DPM_REQUEST_SOP_PRIME_SOFT_RESET_SEND = BIT(20),
	DPM_REQUEST_FRS_DET_ENABLE = BIT(21),
	DPM_REQUEST_FRS_DET_DISABLE = BIT(22),
	DPM_REQUEST_DATA_RESET = BIT(23),
	DPM_REQUEST_GET_REVISION = BIT(24),
	DPM_REQUEST_EPR_MODE_ENTRY = BIT(25),
	DPM_REQUEST_EPR_MODE_EXIT = BIT(26),
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

/**
 * Check if the battery is capable of powering the system
 *
 * @return true if capable of, else false.
 */
bool pd_is_battery_capable(void);

/**
 * Check if PD is capable of trying as source
 *
 * @return true if capable of, else false.
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

/**
 * Check if poower role swap may be needed on AP resume.
 *
 * @param port USB-C Port number
 */
void pd_resume_check_pr_swap_needed(int port);

/* Control Message type - USB-PD Spec Rev 3.0, Ver 1.1, Table 6-5 */
enum pd_ctrl_msg_type {
	PD_CTRL_INVALID = 0, /* 0 Reserved - DO NOT PUT IN MESSAGES */
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
	/* Used for REV 3.0 */
	PD_CTRL_DATA_RESET = 14,
	PD_CTRL_DATA_RESET_COMPLETE = 15,
	PD_CTRL_NOT_SUPPORTED = 16,
	PD_CTRL_GET_SOURCE_CAP_EXT = 17,
	PD_CTRL_GET_STATUS = 18,
	PD_CTRL_FR_SWAP = 19,
	PD_CTRL_GET_PPS_STATUS = 20,
	PD_CTRL_GET_COUNTRY_CODES = 21,
	PD_CTRL_GET_SINK_CAP_EXT = 22,
	/* Used for REV 3.1 */
	PD_CTRL_GET_SOURCE_INFO = 23,
	PD_CTRL_GET_REVISION = 24,
	/* 25-31 Reserved */
};

/* Control message types which always mark the start of an AMS */
#define PD_CTRL_AMS_START_MASK                                           \
	((1 << PD_CTRL_GOTO_MIN) | (1 << PD_CTRL_GET_SOURCE_CAP) |       \
	 (1 << PD_CTRL_GET_SINK_CAP) | (1 << PD_CTRL_DR_SWAP) |          \
	 (1 << PD_CTRL_PR_SWAP) | (1 << PD_CTRL_VCONN_SWAP) |            \
	 (1 << PD_CTRL_GET_SOURCE_CAP_EXT) | (1 << PD_CTRL_GET_STATUS) | \
	 (1 << PD_CTRL_FR_SWAP) | (1 << PD_CTRL_GET_PPS_STATUS) |        \
	 (1 << PD_CTRL_GET_COUNTRY_CODES))

/* Battery Status Data Object fields for REV 3.0 */
#define BSDO_CAP_UNKNOWN 0xffff
#define BSDO_CAP(n) (((n) & 0xffff) << 16)
#define BSDO_INVALID BIT(8)
#define BSDO_PRESENT BIT(9)
#define BSDO_DISCHARGING BIT(10)
#define BSDO_IDLE BIT(11)

/* Battery Capability offsets for 16-bit array indexes */
#define BCDB_VID 0
#define BCDB_PID 1
#define BCDB_DESIGN_CAP 2
#define BCDB_FULL_CAP 3
#define BCDB_BATT_TYPE 4

/* Battery Capability Data Block (BCDB) in struct format.
 * See USB-PD spec Rev 3.1, V 1.3 section 6.5.5
 */
struct pd_bcdb {
	/* Vendor ID*/
	uint16_t vid;
	/* Product ID */
	uint16_t pid;
	/* Battery’s design capacity in 0.1 Wh (0 = no batt, 0xFFFF = unknown)
	 */
	uint16_t design_cap;
	/* Battery’s last full charge capacity in 0.1 Wh (0 = no batt,
	 * 0xFFFF = unknown)
	 */
	uint16_t last_full_charge_cap;
	/* Bit 0 indicates if the request was invalid. Other bits reserved. */
	uint8_t battery_type;
} __packed;

/* Maximum number of different batteries that can be queried through Get Battery
 * Status and Get Battery Capability requests. Indices 0 to 3 are fixed
 * batteries and indices 4 to 7 are hot-swappable batteries. Not all are
 * necessarily present.
 *
 * See USB-PD spec Rev 3.1, V 1.3 sections 6.5.4 - .5
 */
#define PD_BATT_MAX (8)

/*
 * Get Battery Cap Message fields for REV 3.0 (assumes extended header is
 * present in first two bytes)
 */
#define BATT_CAP_REF(n) (((n) >> 16) & 0xff)

/* SOP SDB fields for PD Rev 3.0 Section 6.5.2.1 */
enum pd_sdb_temperature_status {
	PD_SDB_TEMPERATURE_STATUS_NOT_SUPPORTED = 0,
	PD_SDB_TEMPERATURE_STATUS_NORMAL = 2,
	PD_SDB_TEMPERATURE_STATUS_WARNING = 4,
	PD_SDB_TEMPERATURE_STATUS_OVER_TEMPERATURE = 6,
} __packed;
BUILD_ASSERT(sizeof(enum pd_sdb_temperature_status) == 1);

struct pd_sdb {
	/* SDB Fields for PD REV 3.0 */
	uint8_t internal_temp;
	uint8_t present_input;
	uint8_t present_battery_input;
	uint8_t event_flags;
	enum pd_sdb_temperature_status temperature_status;
	uint8_t power_status;
	/* SDB Fields for PD REV 3.1 */
	uint8_t power_state_change;
};

enum pd_sdb_power_state {
	PD_SDB_POWER_STATE_NOT_SUPPORTED = 0,
	PD_SDB_POWER_STATE_S0 = 1,
	PD_SDB_POWER_STATE_MODERN_STANDBY = 2,
	PD_SDB_POWER_STATE_S3 = 3,
	PD_SDB_POWER_STATE_S4 = 4,
	PD_SDB_POWER_STATE_S5 = 5,
	PD_SDB_POWER_STATE_G3 = 6,
};

enum pd_sdb_power_indicator {
	PD_SDB_POWER_INDICATOR_OFF = (0 << 3),
	PD_SDB_POWER_INDICATOR_ON = (1 << 3),
	PD_SDB_POWER_INDICATOR_BLINKING = (2 << 3),
	PD_SDB_POWER_INDICATOR_BREATHING = (3 << 3),
};

/* Extended message type for REV 3.0 - USB-PD Spec 3.0, Ver 1.1, Table 6-42 */
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
	/* Used for REV 3.1 */
	PD_EXT_SINK_CAP = 15,
	PD_EXT_CONTROL = 16,
	PD_EXT_EPR_SOURCE_CAP = 17,
	PD_EXT_EPR_SINK_CAP = 18,
	/* 19-29 Reserved */
	PD_EXT_VENDOR_DEF = 30,
	/* 31 Reserved */
};

/* Alert Data Object fields for REV 3.1 */
#define ADO_EXTENDED_ALERT_EVENT (BIT(24) << 7)
#define ADO_EXTENDED_ALERT_EVENT_TYPE 0xf
/* Alert Data Object fields for REV 3.0 */
#define ADO_OVP_EVENT (BIT(24) << 6)
#define ADO_SOURCE_INPUT_CHANGE (BIT(24) << 5)
#define ADO_OPERATING_CONDITION_CHANGE (BIT(24) << 4)
#define ADO_OTP_EVENT (BIT(24) << 3)
#define ADO_OCP_EVENT (BIT(24) << 2)
#define ADO_BATTERY_STATUS_CHANGE (BIT(24) << 1)
#define ADO_FIXED_BATTERIES(n) ((n & 0xf) << 20)
#define ADO_HOT_SWAPPABLE_BATTERIES(n) ((n & 0xf) << 16)

/* Extended alert event types for REV 3.1 */
enum ado_extended_alert_event_type {
	ADO_POWER_STATE_CHANGE = 0x1,
	ADO_POWER_BUTTON_PRESS = 0x2,
	ADO_POWER_BUTTON_RELEASE = 0x3,
	ADO_CONTROLLER_INITIATED_WAKE = 0x4,
};

/* Data message type - USB-PD Spec Rev 3.0, Ver 1.1, Table 6-6 */
enum pd_data_msg_type {
	/* 0 Reserved */
	PD_DATA_SOURCE_CAP = 1,
	PD_DATA_REQUEST = 2,
	PD_DATA_BIST = 3,
	PD_DATA_SINK_CAP = 4,
	/* Used for REV 3.0 */
	PD_DATA_BATTERY_STATUS = 5,
	PD_DATA_ALERT = 6,
	PD_DATA_GET_COUNTRY_INFO = 7,
	PD_DATA_ENTER_USB = 8,
	/* Used for REV 3.1 */
	PD_DATA_EPR_REQUEST = 9,
	PD_DATA_EPR_MODE = 10,
	PD_DATA_SOURCE_INFO = 11,
	PD_DATA_REVISION = 12,
	/* 13-14 Reserved */
	PD_DATA_VENDOR_DEF = 15,
	/* 16-31 Reserved */
};

/*
 * Cable plug. See 6.2.1.1.7 Cable Plug. Only applies to SOP' and SOP".
 * Replaced by pd_power_role for SOP packets.
 */
enum pd_cable_plug { PD_PLUG_FROM_DFP_UFP = 0, PD_PLUG_FROM_CABLE = 1 };

enum cable_outlet {
	CABLE_PLUG = 0,
	CABLE_RECEPTACLE = 1,
};

/* Vconn role */
#define PD_ROLE_VCONN_OFF 0
#define PD_ROLE_VCONN_ON 1

/* chunk is a request or response in REV 3.0 */
#define CHUNK_RESPONSE 0
#define CHUNK_REQUEST 1

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
#define PD_DEFAULT_STATE(port)                       \
	((PD_ROLE_DEFAULT(port) == PD_ROLE_SOURCE) ? \
		 PD_STATE_SRC_DISCONNECTED :         \
		 PD_STATE_SNK_DISCONNECTED)
#else
#define PD_DEFAULT_STATE(port) PD_STATE_SRC_DISCONNECTED
#endif

/* Build extended message header with chunking */
#define PD_EXT_HEADER(cnum, rchk, dsize) \
	(BIT(15) | ((cnum) << 11) | ((rchk) << 10) | (dsize))

/* Build extended message header without chunking */
#define PD_EXT_HEADER_UNCHUNKED(dsize) (dsize)

/* build message header */
#define PD_HEADER(type, prole, drole, id, cnt, rev, ext)           \
	((type) | ((rev) << 6) | ((drole) << 5) | ((prole) << 8) | \
	 ((id) << 9) | ((cnt) << 12) | ((ext) << 15))

/* Used for processing pd header */
#define PD_HEADER_EXT(header) (((header) >> 15) & 1)
#define PD_HEADER_CNT(header) (((header) >> 12) & 7)
/*
 * NOTE: bit 4 was added in PD 3.0, and should be reserved and set to 0 in PD
 * 2.0 messages
 */
#define PD_HEADER_TYPE(header) ((header) & 0x1F)
#define PD_HEADER_ID(header) (((header) >> 9) & 7)
#define PD_HEADER_PROLE(header) (((header) >> 8) & 1)
#define PD_HEADER_REV(header) (((header) >> 6) & 3)
#define PD_HEADER_DROLE(header) (((header) >> 5) & 1)

/*
 * The message header is a 16-bit value that's stored in a 32-bit data type.
 * SOP* is encoded in bits 31 to 28 of the 32-bit data type.
 * NOTE: This is not part of the PD spec.
 */
#define PD_HEADER_GET_SOP(header) (((header) >> 28) & 0xf)
#define PD_HEADER_SOP(sop) (((sop) & 0xf) << 28)

/* Used for processing pd extended header */
#define PD_EXT_HEADER_CHUNKED(header) (((header) >> 15) & 1)
#define PD_EXT_HEADER_CHUNK_NUM(header) (((header) >> 11) & 0xf)
#define PD_EXT_HEADER_REQ_CHUNK(header) (((header) >> 10) & 1)
#define PD_EXT_HEADER_DATA_SIZE(header) ((header) & 0x1ff)

/* Used to get extended header from the first 32-bit word of the message */
#define GET_EXT_HEADER(msg) (msg & 0xffff)

/* Extended message constants (PD 3.0, Rev. 2.0, section 6.13) */
#define PD_MAX_EXTENDED_MSG_LEN 260
#define PD_MAX_EXTENDED_MSG_CHUNK_LEN 26

/* K-codes for special symbols */
#define PD_SYNC1 0x18
#define PD_SYNC2 0x11
#define PD_SYNC3 0x06
#define PD_RST1 0x07
#define PD_RST2 0x19
#define PD_EOP 0x0D

/* Minimum PD supply current  (mA) */
#define PD_MIN_MA 500

/* Minimum PD voltage (mV) */
#define PD_MIN_MV 5000

/* No connect voltage threshold for sources based on Rp */
#define PD_SRC_DEF_VNC_MV 1600
#define PD_SRC_1_5_VNC_MV 1600
#define PD_SRC_3_0_VNC_MV 2600

/* Rd voltage threshold for sources based on Rp */
#define PD_SRC_DEF_RD_THRESH_MV 200
#define PD_SRC_1_5_RD_THRESH_MV 400
#define PD_SRC_3_0_RD_THRESH_MV 800

/* Voltage threshold to detect connection when presenting Rd */
#define PD_SNK_VA_MV 250

/* Maximum power consumption while in Sink Standby */
#define PD_SNK_STDBY_MW 2500

/* --- Policy layer functions --- */

/** Schedules the interrupt handler for the TCPC on a high priority task. */
void schedule_deferred_pd_interrupt(int port);

/**
 * Get current PD Revision
 *
 * @param port USB-C port number
 * @param type USB-C port partner
 * @return PD_REV10 for PD Revision 1.0
 *         PD_REV20 for PD Revision 2.0
 *         PD_REV30 for PD Revision 3.0
 */
int pd_get_rev(int port, enum tcpci_msg_type type);

/**
 * Get current PD VDO Version of Structured VDM
 *
 * @param port USB-C port number
 * @param type USB-C port partner
 * @return SVDM_VER_1_0 for VDM Version 1.0
 *         SVDM_VER_2_0 for VDM Version 2.0
 */
int pd_get_vdo_ver(int port, enum tcpci_msg_type type);

/**
 * Get transmit retry count for active PD revision.
 *
 * @param port The port to query
 * @param type The partner to query (SOP, SOP', or SOP'')
 * @return The number of retries to perform when transmitting.
 */
int pd_get_retry_count(int port, enum tcpci_msg_type type);

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
void pd_snk_give_back(int port, uint32_t *const ma, uint32_t *const mv);

/**
 * Put a cap on the max voltage requested as a sink.
 * @param mv maximum voltage in millivolts.
 */
void pd_set_max_voltage(unsigned int mv);

/**
 * Get the max voltage that can be requested as set by pd_set_max_voltage().
 * @return max voltage
 */
unsigned int pd_get_max_voltage(void);

/**
 * Check if this board supports the given input voltage.
 *
 * @mv input voltage
 * @return 1 if voltage supported, 0 if not
 */
__override_proto int pd_is_valid_input_voltage(int mv);

/*
 * Return the appropriate set of Source Capability PDOs to offer this
 * port
 *
 * @param src_pdo	Will point to appropriate PDO(s) to offer
 * @param port		USB-C port number
 * @return		Number of PDOs
 */
int pd_get_source_pdo(const uint32_t **src_pdo_p, const int port);

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
 * Check if we are allowed to automatically charge from port partner
 *
 * @param port USB-C port number
 * @pdo_cnt number of source cap PDOs
 * @*pdos  pointer to source cap PDOs
 * @return True if port partner can supply power
 */
__override_proto bool pd_can_charge_from_device(int port, const int pdo_cnt,
						const uint32_t *pdos);

/**
 * Check if data swap is allowed.
 *
 * @param port USB-C port number
 * @param data_role current data role
 * @return True if data swap is allowed, False otherwise
 */
__override_proto int pd_check_data_swap(int port, enum pd_data_role data_role);

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
__override_proto void pd_check_pr_role(int port, enum pd_power_role pr_role,
				       int flags);

/**
 * Check current data role for potential data swap
 *
 * @param port USB-C port number
 * @param dr_role Our data role
 * @param flags PD flags
 */
__override_proto void pd_check_dr_role(int port, enum pd_data_role dr_role,
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
 * Execute data swap.
 *
 * @param port USB-C port number
 * @param data_role new data role
 */
__override_proto void pd_execute_data_swap(int port,
					   enum pd_data_role data_role);

/**
 * Get desired dual role state when chipset is suspended.
 * Under some circumstances we are not allowed to be source
 * during suspend. This function should return appropriate state.
 */

__override_proto enum pd_dual_role_states pd_get_drp_state_in_suspend(void);

/**
 * Get desired dual role state when chipset is on.
 *
 * Under some circumstances we are not allowed to be source
 * during chipset on. This function should return appropriate state.
 */
__override_proto enum pd_dual_role_states pd_get_drp_state_in_s0(void);

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
 * @param rtype    pointer to the type of message (SOP/SOP'/SOP'')
 * @return if >0, number of VDOs to send back.
 */
int pd_svdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload,
	    uint32_t head, enum tcpci_msg_type *rtype);

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
 * @param type Transmit type (SOP, SOP') for which to enter mode
 * @param svid USB standard or vendor id to exit or zero for DFP amode reset.
 * @param opos object position of mode to exit.
 * @return vdm for UFP to be sent to enter mode or zero if not.
 */
uint32_t pd_dfp_enter_mode(int port, enum tcpci_msg_type type, uint16_t svid,
			   int opos);

/**
 * Save the Enter mode command data received from the port partner for setting
 * the retimer
 *
 * @param port     USB-C port number
 * @param payload  payload data.
 */
void pd_ufp_set_enter_mode(int port, uint32_t *payload);

/**
 * Return Enter mode command data received from the port partner
 *
 * @param port     USB-C port number
 * @return enter mode raw value requested to the UFP
 */
uint32_t pd_ufp_get_enter_mode(int port);

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
 * @param type Transmit type (SOP, SOP') for which to exit mode
 * @param svid USB standard or vendor id to exit or zero for DFP amode reset.
 * @param opos object position of mode to exit.
 * @return 1 if UFP should be sent exit mode VDM.
 */
int pd_dfp_exit_mode(int port, enum tcpci_msg_type type, uint16_t svid,
		     int opos);

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
 * @param type    Transmit type (SOP, SOP') for received modes
 * @param cnt     number of data objects in payload
 * @param payload payload data.
 */
void dfp_consume_identity(int port, enum tcpci_msg_type type, int cnt,
			  uint32_t *payload);

/**
 * Consume the SVIDs
 *
 * @param port    USB-C port number
 * @param type    Transmit type (SOP, SOP') for received SVIDs
 * @param cnt     number of data objects in payload
 * @param payload payload data.
 */
void dfp_consume_svids(int port, enum tcpci_msg_type type, int cnt,
		       uint32_t *payload);

/**
 * Consume the alternate modes
 *
 * @param port    USB-C port number
 * @param type    Transmit type (SOP, SOP') for received modes
 * @param cnt     number of data objects in payload
 * @param payload payload data.
 */
void dfp_consume_modes(int port, enum tcpci_msg_type type, int cnt,
		       uint32_t *payload);

/**
 * Returns true if connected VPD supports Charge Through
 *
 * @param port  USB-C port number
 * @return      TRUE if Charge Through is supported, else FALSE
 */
bool is_vpd_ct_supported(int port);

/**
 * Initialize alternate mode discovery info for DFP
 *
 * @param port     USB-C port number
 */
void pd_dfp_discovery_init(int port);

/**
 * Initialize active mode info (alternate mode or USB mode) for DFP
 *
 * @param port USB-C port number
 */
void pd_dfp_mode_init(int port);

/**
 * Mark all discovery types as failed to prevent any further discovery attempts
 * until a connection change or DPM request triggers discovery again.
 * @param port USB-C port number
 */
void pd_disable_discovery(int port);

/**
 * Set identity discovery state for this type and port
 *
 * @param port  USB-C port number
 * @param type	SOP* type to set
 * @param disc  Discovery state to set (failed or complete)
 */
void pd_set_identity_discovery(int port, enum tcpci_msg_type type,
			       enum pd_discovery_state disc);

/**
 * Get identity discovery state for this type and port
 *
 * @param port  USB-C port number
 * @param type	SOP* type to retrieve
 * @return      Current discovery state (failed or complete)
 */
enum pd_discovery_state pd_get_identity_discovery(int port,
						  enum tcpci_msg_type type);

/**
 * Set SVID discovery state for this type and port.
 *
 * @param port USB-C port number
 * @param type SOP* type to set
 * @param disc Discovery state to set (failed or complete)
 */
void pd_set_svids_discovery(int port, enum tcpci_msg_type type,
			    enum pd_discovery_state disc);

/**
 * Get SVID discovery state for this type and port
 *
 * @param port USB-C port number
 * @param type SOP* type to retrieve
 * @return     Current discovery state (failed or complete)
 */
enum pd_discovery_state pd_get_svids_discovery(int port,
					       enum tcpci_msg_type type);

/**
 * Set Modes discovery state for this port, SOP* type, and SVID.
 *
 * @param port USB-C port number
 * @param type SOP* type to set
 * @param svid SVID to set mode discovery state for
 * @param disc Discovery state to set (failed or complete)
 */
void pd_set_modes_discovery(int port, enum tcpci_msg_type type, uint16_t svid,
			    enum pd_discovery_state disc);

/**
 * Get Modes discovery state for this port and SOP* type. Modes discover is
 * considered NEEDED if there are any discovered SVIDs that still need to be
 * discovered. Modes discover is considered COMPLETE when no discovered SVIDs
 * need to go through discovery and at least one mode has been considered
 * complete or if there are no discovered SVIDs. Modes discovery is
 * considered FAIL if mode discovery for all SVIDs are failed.
 *
 * @param port USB-C port number
 * @param type SOP* type to retrieve
 * @return      Current discovery state (PD_DISC_NEEDED, PD_DISC_COMPLETE,
 *                                       PD_DISC_FAIL)
 */
enum pd_discovery_state pd_get_modes_discovery(int port,
					       enum tcpci_msg_type type);

/**
 * Returns the mode vdo count of the specified SVID and sets
 * the vdo_out with it's discovered mode VDO.
 *
 * @param port     USB-C port number
 * @param type     Transmit type (SOP, SOP') for VDM
 * @param svid     SVID to get
 * @param vdo_out  Discover Mode VDO response to set
 *                 Note: It must be able to fit within PDO_MAX_OBJECTS VDOs.
 * @return         Mode VDO cnt of specified SVID if is discovered,
 *                 0 otherwise
 */
int pd_get_mode_vdo_for_svid(int port, enum tcpci_msg_type type, uint16_t svid,
			     uint32_t *vdo_out);

/**
 * Get a pointer to mode data for the next SVID that needs to be discovered.
 * This data may indicate that discovery failed.
 *
 * @param port USB-C port number
 * @param type SOP* type to retrieve
 * @return     In order of precedence:
 *             Pointer to the first SVID-mode structure with needs discovered
 *             mode, if any exist;
 *             Pointer to the first SVID-mode structure with discovery failed
 *             mode, if any exist and no modes succeeded in discovery;
 *             NULL, otherwise
 */
const struct svid_mode_data *pd_get_next_mode(int port,
					      enum tcpci_msg_type type);

/**
 * Return a pointer to the discover identity response structure for this SOP*
 * type
 *
 * @param port  USB-C port number
 * @param type	SOP* type to retrieve
 * @return      pointer to response structure, which the caller may not alter
 */
const union disc_ident_ack *pd_get_identity_response(int port,
						     enum tcpci_msg_type type);

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
int pd_get_svid_count(int port, enum tcpci_msg_type type);

/**
 * Return the SVID of given SVID index of port partner connected
 * to a specified port
 *
 * @param port     USB-C port number
 * @param svid_idx SVID Index
 * @param type	   SOP* type to retrieve
 * @return         SVID
 */
uint16_t pd_get_svid(int port, uint16_t svid_idx, enum tcpci_msg_type type);

/**
 * Return the pointer to modes of VDO of port partner connected
 * to a specified port
 *
 * @param port     USB-C port number
 * @param svid_idx SVID Index
 * @param type     SOP* type to retrieve
 * @return         Pointer to modes of VDO
 */
const uint32_t *pd_get_mode_vdo(int port, uint16_t svid_idx,
				enum tcpci_msg_type type);

/*
 * Looks for a discovered mode VDO for the specified SVID.
 *
 * @param port USB-C port number
 * @param type SOP* type to retrieve
 * @param svid SVID to look up
 * @return     Whether a mode was discovered for the SVID
 */
bool pd_is_mode_discovered_for_svid(int port, enum tcpci_msg_type type,
				    uint16_t svid);

/**
 * Return the alternate mode entry and exit data
 *
 * @param port  USB-C port number
 * @param type  Transmit type (SOP, SOP', SOP'') for mode data
 * @param svid  SVID
 * @return      pointer to SVDM mode data
 */
struct svdm_amode_data *pd_get_amode_data(int port, enum tcpci_msg_type type,
					  uint16_t svid);

/*
 * Returns cable revision
 *
 * @param port          USB-C port number
 * @return              cable revision
 */
enum pd_rev_type get_usb_pd_cable_revision(int port);

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

/*
 * Clears record of which tasks have accessed discovery data for this port and
 * type.
 *
 * @param port USB-C port number
 * @param type Transmit type (SOP, SOP')
 */
void pd_discovery_access_clear(int port, enum tcpci_msg_type type);

/*
 * Validate that this current task is the only one which has retrieved the
 * pointer from pd_get_am_discovery() since last call to
 * pd_discovery_access_clear().
 *
 * @param port USB-C port number
 * @param type Transmit type (SOP, SOP')
 * @return     True - No other tasks have accessed the data
 */
bool pd_discovery_access_validate(int port, enum tcpci_msg_type type);

/*
 * Returns the pointer to PD alternate mode discovery results
 *
 * Note: Caller function can mutate the data in this structure.
 *
 * TCPMv2 will track all tasks which call this function after the most recent
 * pd_discovery_access_clear(), so that the host command task reading out this
 * structure may run pd_discovery_access_validate() at the end of its read to
 * verify whether data might have changed in that timeframe.
 *
 * @param port USB-C port number
 * @param type Transmit type (SOP, SOP') for discovered information
 * @return     pointer to PD alternate mode discovery results
 */
struct pd_discovery *
pd_get_am_discovery_and_notify_access(int port, enum tcpci_msg_type type);

/*
 * Returns the constant pointer to PD alternate mode discovery results
 * Note: Caller function is expected to only read the discovery results.
 *
 * @param port USB-C port number
 * @param type Transmit type (SOP, SOP') for discovered information
 * @return     pointer to PD alternate mode discovery results
 */
const struct pd_discovery *pd_get_am_discovery(int port,
					       enum tcpci_msg_type type);

/*
 * Returns the pointer to PD active alternate modes.
 * Note: Caller function can mutate the data in this structure.
 *
 * @param port USB-C port number
 * @param type Transmit type (SOP, SOP', SOP'') for active modes
 * @return     Pointer to PD active alternate modes.
 */
struct partner_active_modes *
pd_get_partner_active_modes(int port, enum tcpci_msg_type type);

/*
 * Sets the current object position for DP alt-mode
 * Note: opos == 0 means the mode is not active
 *
 * @param port USB-C port number
 * @param opos Object position for DP alternate mode
 */
void pd_ufp_set_dp_opos(int port, int opos);

/*
 * Gets the current object position for DP alt-mode
 *
 * @param port USB-C port number
 * @return Alt-DP object position value for the given port
 */
int pd_ufp_get_dp_opos(int port);

/**
 * Notify hpd->pd converter that display is configured
 *
 * @param port     USB-C port number
 */
void pd_ufp_enable_hpd_send(int port);

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
 * Reset Cable type, Cable attributes and cable flags
 *
 * @param port     USB-C port number
 */
void reset_pd_cable(int port);

/**
 * Return the type of cable attached
 *
 * @param port	USB-C port number
 * @return	cable type
 */
enum idh_ptype get_usb_pd_cable_type(int port);

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
 * Return Thunderbolt rounded support
 * Rounded support indicates if the cable can support rounding the
 * frequency depending upon the cable generation.
 *
 * @param port USB-C port number
 * @return tbt_rounded_support
 */
enum tbt_compat_rounded_support get_tbt_rounded_support(int port);

/**
 * Returns the first discovered Mode VDO for Intel SVID
 *
 * @param port  USB-C port number
 * @param type  Transmit type (SOP, SOP') for VDM
 * @return      Discover Mode VDO for Intel SVID if the Intel mode VDO is
 *              discovered, 0 otherwise
 */
uint32_t pd_get_tbt_mode_vdo(int port, enum tcpci_msg_type type);

/**
 * Sets the Mux state to Thunderbolt-Compatible mode
 *
 *  @param port  USB-C port number
 */
void set_tbt_compat_mode_ready(int port);

/**
 * Returns Thunderbolt-compatible cable speed according to the port if,
 * port supports lesser speed than the cable
 *
 * @param port USB-C port number
 * @return Thunderbolt cable speed
 */
enum tbt_compat_cable_speed get_tbt_cable_speed(int port);

/**
 * Fills the TBT3 objects in the payload and returns the number
 * of objects it has filled.
 *
 * @param port      USB-C port number
 * @param sop       Type of SOP message transmitted (SOP/SOP'/SOP'')
 * @param payload   payload data
 * @return          Number of object filled
 */
int enter_tbt_compat_mode(int port, enum tcpci_msg_type sop, uint32_t *payload);

/**
 * Return maximum speed supported by the port to enter into Thunderbolt mode
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
 * Set what this board should be replying to TBT EnterMode requests with, when
 * it is configured as the UFP (VDM Responder).
 *
 * @param port USB-C port number
 * @param reply AP-selected reply to the TBT EnterMode request
 * @return EC_RES_SUCCESS if board supports this configuration setting
 *	   EC_RES_INVALID_PARAM if board supports this feature, but not this
 *	   option
 *	   EC_RES_UNAVAILABLE if board does not support this feature
 */
__override_proto enum ec_status
board_set_tbt_ufp_reply(int port, enum typec_tbt_ufp_reply reply);

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

/**
 * Instruct the Policy Engine to perform a Device Policy Manager Request
 * This function is called from the Device Policy Manager and only has effect
 * if the current Policy Engine state is Src.Ready or Snk.Ready.
 *
 * @param port  USB-C port number
 * @param req   Device Policy Manager Request
 */
void pd_dpm_request(int port, enum pd_dpm_request req);

/*
 * TODO(b/155890173): Probably, this should only be used by the DPM, and
 * pd_send_vdm should be implemented in terms of DPM functions.
 */
/* Prepares the PE to send an VDM.
 *
 * @param port    USB-C port number
 * @param type    Transmit type (SOP, SOP', SOP'') for VDM
 * @param vdm     Buffer containing the message body to send, including the VDM
 *                Header but not the Message Header.
 * @param vdo_cnt The number of 32-bit VDOs in vdm, including the VDM Header;
 *                must be 1 - 7 inclusive.
 * @return        True if the setup was successful
 */
bool pd_setup_vdm_request(int port, enum tcpci_msg_type tx_type, uint32_t *vdm,
			  uint32_t vdo_cnt);

/* Power Data Objects for the source and the sink */
__override_proto extern const uint32_t pd_src_pdo[];
extern const int pd_src_pdo_cnt;
extern const uint32_t pd_src_pdo_max[];
extern const int pd_src_pdo_max_cnt;
extern const uint32_t pd_snk_pdo[];
extern const int pd_snk_pdo_cnt;

/**
 * TEST ONLY: Set PD_CONTROL command to enabled on this port
 *
 * @param port USB-C port number
 */
#ifdef TEST_BUILD
void pd_control_port_enable(int port);
#endif

/**
 * Request that a host event be sent to notify the AP of a PD power event.
 *
 * Note: per-port events should be retrieved through pd_get_events(), but this
 * function still notifies the AP there are events to retrieve, and directs it
 * to the per-port events through PD_EVENT_TYPEC
 *
 * @param mask host event mask.
 */
#if defined(CONFIG_USB_PD_HOST_CMD) && !defined(CONFIG_USB_PD_TCPM_STUB)
void pd_send_host_event(int mask);
#else
static inline void pd_send_host_event(int mask)
{
}
#endif

/**
 * Determine if in alternate mode or not.
 *
 * @param port port number.
 * @param type Transmit type (SOP, SOP', SOP'') for alt mode status
 * @param svid USB standard or vendor id
 * @return object position of mode chosen in alternate mode otherwise zero.
 */
int pd_alt_mode(int port, enum tcpci_msg_type type, uint16_t svid);

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
 * Request Error Recovery
 *
 * Note that Error Recovery will happen on the next cycle of the port's PD task
 * and may not have started yet at the time of the function return.
 *
 * @param port USB-C port number
 */
void pd_set_error_recovery(int port);

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

/**
 * interrupt handler
 */
void pd_rx_handler(void);

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
 * Get the port events.
 *
 * @param port USB-C port number
 * @return PD_STATUS_EVENT_* bitmask
 */
uint32_t pd_get_events(int port);

/**
 * Notify the AP of an event on the given port number
 *
 * @param port USB-C port number
 * @param event_mask bitmask of events to set (PD_STATUS_EVENT_* bitmask)
 */
void pd_notify_event(int port, uint32_t event_mask);

/**
 * Clear selected port events
 *
 * @param port USB-C port number
 * @param clear_mask bitmask of events to clear (PD_STATUS_EVENT_* bitmask)
 */
void pd_clear_events(int port, uint32_t clear_mask);

/*
 * Requests a VDM REQ message be sent. It is assumed that this message may be
 * coming from a task outside the PD task.
 *
 * @param port USB-C port number
 * @param *data pointer to the VDM Attention message
 * @param vdo_count number of VDOs (must be 1 or 2)
 * @param tx_type partner type to transmit
 * @return EC_RES_SUCCESS if a VDM message is scheduled.
 *         EC_RES_BUSY if a message is already pending
 *         EC_RES_INVALID_PARAM if the parameters given are invalid
 */
enum ec_status pd_request_vdm(int port, const uint32_t *data, int vdo_count,
			      enum tcpci_msg_type tx_type);

/*
 * Requests that the port enter the specified mode. A successful result just
 * means that the request was received, not that the mode has been entered yet.
 *
 * @param port USB-C port number
 * @param mode The mode to enter
 * @return EC_RES_SUCCESS if the request was made
 *         EC_RES_INVALID_PARAM for an invalid port or mode;
 *         EC_RES_BUSY if another mode entry request is already in progress
 */
enum ec_status pd_request_enter_mode(int port, enum typec_mode mode);

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
const uint32_t *const pd_get_src_caps(int port);

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
 * Returns the sink caps list
 *
 * @param port USB-C port number
 */
const uint32_t *const pd_get_snk_caps(int port);

/**
 * Returns the number of sink caps
 *
 * @param port USB-C port number
 */
uint8_t pd_get_snk_cap_cnt(int port);

/**
 * Returns requested voltage
 *
 * @param port USB-C port number
 */
uint32_t pd_get_requested_voltage(int port);

/**
 * Returns requested current
 *
 * @param port USB-C port number
 */
uint32_t pd_get_requested_current(int port);

/**
 * Return true if partner port is capable of communication over USB data
 * lines.
 *
 * @param port USB-C port number
 */
bool pd_get_partner_usb_comm_capable(int port);

/**
 * Gets the port partner's RMDO from the PE state.
 *
 * @param port USB-C port number
 * @return port partner's Revision Message Data Object (RMDO).
 */
struct rmdo pd_get_partner_rmdo(int port);

/**
 * Return true if PD is in disconnect state
 *
 * @param port USB-C port number
 */
bool pd_is_disconnected(int port);

/**
 * Return true if vbus is at level on the specified port.
 *
 * Note that boards may override this function if they have a method outside the
 * TCPCI driver to verify vSafe0V
 *
 * @param port USB-C port number
 * @param level vbus_level to check against
 */
__override_proto bool pd_check_vbus_level(int port, enum vbus_level level);

/**
 * Return true if vbus is at Safe5V on the specified port.
 *
 * @param port USB-C port number
 */
int pd_is_vbus_present(int port);

/**
 * Enable or disable the FRS trigger for a given port
 *
 * @param port   USB-C port number
 * @param enable 1 to enable the FRS trigger, 0 to disable
 * @return EC_SUCCESS on success, or an error
 */
int pd_set_frs_enable(int port, int enable);

/**
 * Optional, board-specific configuration to enable the FRS trigger on port
 *
 * @param port   USB-C port number
 * @param enable 1 to enable the FRS trigger, 0 to disable
 * @return EC_SUCCESS on success, or an error
 */
__override_proto int board_pd_set_frs_enable(int port, int enable);

/**
 * Optional board-level function called after TCPC detect FRS signal.
 *
 * @param port   USB-C port number
 */
__overridable void board_frs_handler(int port);

#ifdef CONFIG_USB_PD_DP_MODE
/**
 * Get current DisplayPort pin mode on the specified port.
 *
 * @param port USB-C port number
 * @return MODE_DP_PIN_[A-E] if used else 0
 */
__override_proto uint8_t get_dp_pin_mode(int port);
#else
static inline uint8_t get_dp_pin_mode(int port)
{
	return 0;
}
#endif /* CONFIG_USB_PD_DP_MODE */

/**
 * Get board specific usb pd port count
 *
 * @return <= CONFIG_USB_PD_PORT_MAX_COUNT if configured in board file,
 *         else return CONFIG_USB_PD_PORT_MAX_COUNT
 */
__override_proto uint8_t board_get_usb_pd_port_count(void);

/**
 * Return true if specified PD port is present. This is similar to
 * checking CONFIG_USB_PD_PORT_MAX_COUNT but handles sparse numbering.
 *
 * @param port USB-C port number
 *
 * @return true if port is present.
 */
__override_proto bool board_is_usb_pd_port_present(int port);

/**
 * Return true if specified PD port is DTS (Debug and Test System
 * capable).
 *
 * @param port USB-C port number
 *
 * @return true if port is DTS capable.
 */
__override_proto bool board_is_dts_port(int port);

/**
 * Process PD-related alerts for a chip which is sharing the TCPC interrupt line
 *
 * @param port USB-C port number
 */
__override_proto void board_process_pd_alert(int port);

/**
 * Resets external PD chips including TCPCs and MCUs.
 *
 * Boards must provide this when PDCMD (PD MCUs case) or PD INT (TCPC case)
 * tasks are present.
 */
void board_reset_pd_mcu(void);

/*
 * Notify the AP that we have entered into DisplayPort Alternate Mode.  This
 * sets a DP_ALT_MODE_ENTERED MKBP event which may wake the AP.
 */
__override_proto void pd_notify_dp_alt_mode_entry(int port);

/*
 * Determines the PD state of the port partner according to Table 4-10 in USB PD
 * specification.
 */
enum pd_cc_states pd_get_cc_state(enum tcpc_cc_voltage_status cc1,
				  enum tcpc_cc_voltage_status cc2);

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
void pd_log_event(uint8_t type, uint8_t size_port, uint16_t data,
		  void *payload);

/**
 * Retrieve one logged event and prepare a VDM with it.
 *
 * Used to answer the VDO_CMD_GET_LOG unstructured VDM.
 *
 * @param payload pointer to the payload data buffer (must be 7 words)
 * @return number of 32-bit words in the VDM payload.
 */
int pd_vdm_get_log_entry(uint32_t *payload);
#else /* CONFIG_USB_PD_LOGGING */
static inline void pd_log_event(uint8_t type, uint8_t size_port, uint16_t data,
				void *payload)
{
}
static inline int pd_vdm_get_log_entry(uint32_t *payload)
{
	return 0;
}
#endif /* CONFIG_USB_PD_LOGGING */

/**
 * Prepare for a sysjump by exiting any alternate modes, if PD communication is
 * allowed.
 *
 * Note: this call will block until the PD task has finished its exit mode and
 * re-awoken the calling task.
 */
void pd_prepare_sysjump(void);

/**
 * Compose SVDM Request Header
 *
 * @param port The PD port number
 * @param type The partner to query (SOP, SOP', or SOP'')
 * @param svid SVID to include in svdm header
 * @param cmd VDO CMD to send
 * @return svdm header to send
 */
uint32_t pd_compose_svdm_req_header(int port, enum tcpci_msg_type type,
				    uint16_t svid, int cmd);

/* ----- SVDM handlers ----- */

/* DisplayPort Alternate Mode */
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
extern int dp_flags[CONFIG_USB_PD_PORT_MAX_COUNT];
extern uint32_t dp_status[CONFIG_USB_PD_PORT_MAX_COUNT];
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

/*
 * Set HPD GPIO level
 *
 * @param port The PD port number
 * @param en 0 for HPD disabled, 1 for HPD enabled.
 */
void svdm_set_hpd_gpio(int port, int en);

/*
 * Get HPD GPIO level
 *
 * @param port The PD port number
 * @return 0 for HPD disabled, 1 for HPD enabled.
 */
int svdm_get_hpd_gpio(int port);

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

/**
 * Get the DP mode that's desired on this port
 *
 * @param  port The PD port number
 * @return USB_PD_MUX_DOCK or USB_PD_MUX_DP_ENABLED
 */
uint8_t svdm_dp_get_mux_mode(int port);

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

/****************************************************************************
 * TCPC CC/Rp Management
 */
/**
 * Called to cache Source Current Limit
 * A call to typec_update_cc will actually update the hardware to reflect the
 * cache.
 *
 * @param port The PD port number
 * @param rp   Rp is the Current Limit to advertise
 */
void typec_select_src_current_limit_rp(int port, enum tcpc_rp_value rp);

/**
 * Called to get a port's default current limit Rp.
 *
 * @param port The PD port number
 * @return rp   Rp is the Current Limit to advertise
 */
__override_proto int typec_get_default_current_limit_rp(int port);

/**
 * Called to cache Source Collision Rp
 * A call to typec_update_cc will actually update the hardware to reflect the
 * cache.
 *
 * @param port The PD port number
 * @param rp   Rp is the Collision Avoidance Rp value
 */
void typec_select_src_collision_rp(int port, enum tcpc_rp_value rp);

/**
 * Called to update cached CC/Rp values to hardware
 *
 * @param port The PD port number
 * @return 0 on success else failure
 */
int typec_update_cc(int port);

/**
 * Defines the New power state indicator bits in the Power State Change
 * field of the Status Data Block (SDB) in USB PD Revision 3.1 and above.
 *
 * @param pd_sdb_power_state enum defining the New Power State field of the SDB
 * @return pd_sdb_power_indicator enum for the SDB
 */
__override_proto enum pd_sdb_power_indicator
board_get_pd_sdb_power_indicator(enum pd_sdb_power_state power_state);

/****************************************************************************/

#endif /* __CROS_EC_USB_PD_H */

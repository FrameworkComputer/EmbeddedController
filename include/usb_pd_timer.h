/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery module */

#ifndef __CROS_EC_USB_PD_TIMER_H
#define __CROS_EC_USB_PD_TIMER_H

#include "atomic.h"
#include "atomic_bit.h"

#include <stdbool.h>

/*
 * List of all timers that will be managed by usb_pd_timer
 */
enum pd_task_timer {
	/*
	 * Timer to check if a USB PD power button press exceeds the long press
	 * time limit.
	 */
	DPM_TIMER_PD_BUTTON_LONG_PRESS,

	/*
	 * Timer to check if a USB PD power button press exceeds the short press
	 * time limit.
	 */
	DPM_TIMER_PD_BUTTON_SHORT_PRESS,

	/*
	 * In BIST_TX mode, this timer is used by a UUT to ensure that a
	 * Continuous BIST Mode (i.e. BIST Carrier Mode) is exited in a timely
	 * fashion.
	 *
	 * In BIST_RX mode, this timer is used to give the port partner time
	 * to respond.
	 */
	PE_TIMER_BIST_CONT_MODE,

	/*
	 * PD 3.0, version 2.0, section 6.6.18.1: The ChunkingNotSupportedTimer
	 * is used by a Source or Sink which does not support multi-chunk
	 * Chunking but has received a Message Chunk. The
	 * ChunkingNotSupportedTimer Shall be started when the last bit of the
	 * EOP of a Message Chunk of a multi-chunk Message is received. The
	 * Policy Engine Shall Not send its Not_Supported Message before the
	 * ChunkingNotSupportedTimer expires.
	 */
	PE_TIMER_CHUNKING_NOT_SUPPORTED,

	/*
	 * PD 3.0, rev. 3.1, v. 1.2, section 6.6.10.3: The DataResetFailTimer
	 * Shall be used by the DFP’s Policy Engine to ensure the Data Reset
	 * process completes within tDataResetFail of the last bit of the
	 * GoodCRC acknowledging the Accept Message in response to the
	 * Data_Reset Message.
	 */
	PE_TIMER_DATA_RESET_FAIL,

	/*
	 * This timer is used during an Explicit Contract when discovering
	 * whether a Port Partner is PD Capable using SOP'.
	 */
	PE_TIMER_DISCOVER_IDENTITY,

	/*
	 * The NoResponseTimer is used by the Policy Engine in a Source
	 * to determine that its Port Partner is not responding after a
	 * Hard Reset.
	 */
	PE_TIMER_NO_RESPONSE,

	/*
	 * This timer tracks the time after receiving a Wait message in
	 * response to a PR_Swap message.
	 */
	PE_TIMER_PR_SWAP_WAIT,

	/*
	 * This timer is used in a Source to ensure that the Sink has had
	 * sufficient time to process Hard Reset Signaling before turning
	 * off its power supply to VBUS.
	 */
	PE_TIMER_PS_HARD_RESET,

	/*
	 * This timer combines the PSSourceOffTimer and PSSourceOnTimer timers.
	 * For PSSourceOffTimer, when this DRP device is currently acting as a
	 * Sink, this timer times out on a PS_RDY Message during a Power Role
	 * Swap sequence.
	 *
	 * For PSSourceOnTimer, when this DRP device is currently acting as a
	 * Source that has just stopped sourcing power and is waiting to start
	 * sinking power to timeout on a PS_RDY Message during a Power Role
	 * Swap.
	 */
	PE_TIMER_PS_SOURCE,

	/*
	 * This timer is started when a request for a new Capability has been
	 * accepted and will timeout after PD_T_PS_TRANSITION if a PS_RDY
	 * Message has not been received.
	 */
	PE_TIMER_PS_TRANSITION,

	/*
	 * This timer is used to ensure that a Message requesting a response
	 * (e.g. Get_Source_Cap Message) is responded to within a bounded time
	 * of PD_T_SENDER_RESPONSE.
	 */
	PE_TIMER_SENDER_RESPONSE,

	/*
	 *  6.6.21 EPR Timers of PD R3.1 V1.6
	 *  This timer is used to ensure the EPR Mode entry process completes
	 *  within PD_T_ENTER_EPR.
	 */
	PE_TIMER_SINK_EPR_ENTER,
	PE_TIMER_SINK_EPR_KEEP_ALIVE,

	/*
	 * This timer is used to ensure that the time before the next Sink
	 * Request Message, after a Wait Message has been received from the
	 * Source in response to a Sink Request Message.
	 */
	PE_TIMER_SINK_REQUEST,

	/*
	 * Prior to a successful negotiation, a Source Shall use the
	 * SourceCapabilityTimer to periodically send out a
	 * Source_Capabilities Message.
	 */
	PE_TIMER_SOURCE_CAP,

	/*
	 * Used to wait for tSrcTransition between sending an Accept for a
	 * Request or receiving a GoToMin and transitioning the power supply.
	 * See PD 3.0, table 7-11 and table 7-22 This is not a named timer in
	 * the spec.
	 */
	PE_TIMER_SRC_TRANSITION,

	/*
	 * This timer is used by the new Source, after a Power Role Swap or
	 * Fast Role Swap, to ensure that it does not send Source_Capabilities
	 * Message before the new Sink is ready to receive the
	 * Source_Capabilities Message.
	 */
	PE_TIMER_SWAP_SOURCE_START,

	/* Temporary available timeout timer */
	PE_TIMER_TIMEOUT,

	/*
	 * The amount of timer that the DFP shall wait for the UFP to discharge
	 * VCONN (and send PS_RDY) during Data Reset. See PD 3.0, rev. 3.1, v.
	 * 1.2, section 6.6.10.1 VCONNDischargeTimer.
	 */
	PE_TIMER_VCONN_DISCHARGE,

	/*
	 * This timer is used during a VCONN Swap.
	 */
	PE_TIMER_VCONN_ON,

	/*
	 * The amount of time that VCONN shall remain off during the cable reset
	 * portion of a Data Reset. See PD 3.0, rev. 3.1, v. 1.2, section 7.1.15
	 * VCONN Power Cycle.
	 */
	PE_TIMER_VCONN_REAPPLIED,

	/*
	 * This timer is used by the Initiator’s Policy Engine to ensure that
	 * a Structured VDM Command request needing a response (e.g. Discover
	 * Identity Command request) is responded to within a bounded time of
	 * tVDMSenderResponse.
	 */
	PE_TIMER_VDM_RESPONSE,

	/*
	 * For PD2.0, this timer is used to wait 400ms and add some
	 * jitter of up to 100ms before sending a message.
	 * NOTE: This timer is not part of the TypeC/PD spec.
	 */
	PE_TIMER_WAIT_AND_ADD_JITTER,

	/* Chunk Sender Response timer */
	PR_TIMER_CHUNK_SENDER_RESPONSE,

	/* Chunk Sender Request timer */
	PR_TIMER_CHUNK_SENDER_REQUEST,

	/* Hard Reset Complete timer */
	PR_TIMER_HARD_RESET_COMPLETE,

	/* Sink TX timer */
	PR_TIMER_SINK_TX,

	/* timeout to limit waiting on TCPC response (not in spec) */
	PR_TIMER_TCPC_TX_TIMEOUT,

	/* Time a port shall wait before it can determine it is attached */
	TC_TIMER_CC_DEBOUNCE,

	/* Time to debounce exit low power mode */
	TC_TIMER_LOW_POWER_EXIT_TIME,

	/* Time to enter low power mode */
	TC_TIMER_LOW_POWER_TIME,

	/* Role toggle timer */
	TC_TIMER_NEXT_ROLE_SWAP,

	/*
	 * Time a Sink port shall wait before it can determine it is detached
	 * due to the potential for USB PD signaling on CC as described in
	 * the state definitions.
	 */
	TC_TIMER_PD_DEBOUNCE,

	/* Generic timer */
	TC_TIMER_TIMEOUT,

	/*
	 * Time a port shall wait before it can determine it is
	 * re-attached during the try-wait process.
	 */
	TC_TIMER_TRY_WAIT_DEBOUNCE,

	/*
	 * Time to ignore Vbus absence due to external IC debounce detection
	 * logic immediately after a power role swap.
	 */
	TC_TIMER_VBUS_DEBOUNCE,

	PD_TIMER_COUNT
};

enum pd_timer_range {
	DPM_TIMER_RANGE,
	PE_TIMER_RANGE,
	PR_TIMER_RANGE,
	TC_TIMER_RANGE,
};
#define DPM_TIMER_START DPM_TIMER_PD_BUTTON_LONG_PRESS
#define DPM_TIMER_END DPM_TIMER_PD_BUTTON_SHORT_PRESS

#define PE_TIMER_START PE_TIMER_BIST_CONT_MODE
#define PE_TIMER_END PE_TIMER_WAIT_AND_ADD_JITTER

#define PR_TIMER_START PR_TIMER_CHUNK_SENDER_RESPONSE
#define PR_TIMER_END PR_TIMER_TCPC_TX_TIMEOUT

#define TC_TIMER_START TC_TIMER_CC_DEBOUNCE
#define TC_TIMER_END TC_TIMER_VBUS_DEBOUNCE

/*
 * pd_timer_init
 * Initialize Power Delivery Timer module
 *
 * @param port USB-C port number
 */
void pd_timer_init(int port);

/*
 * pd_timer_enable
 * Initiate an enabled timer
 *
 * @param port USB-C port number
 * @param timer Requested pd_task_timer
 * @param expires_us Expiration time relative to "now"
 */
void pd_timer_enable(int port, enum pd_task_timer timer, uint32_t expires_us);

/*
 * pd_timer_disable
 * Disable a timer
 *
 * @param port USB-C port number
 * @param timer Requested pd_task_timer
 */
void pd_timer_disable(int port, enum pd_task_timer timer);

/*
 * pd_timer_disable_range
 * Disable all of the timers in a group range
 *
 * @param port USB-C port number
 * @param range Group range to disable
 */
void pd_timer_disable_range(int port, enum pd_timer_range range);

/*
 * pd_timer_is_disabled
 * Determine if a timer is currently disabled
 *
 * @param port USB-C port number
 * @param timer Requested pd_task_timer
 * @return True if the timer is disabled, otherwise false
 */
bool pd_timer_is_disabled(int port, enum pd_task_timer timer);

/*
 * pd_timer_is_expired
 * Determine if a timer is expired
 *
 * @param port USB-C port number
 * @param timer Requested pd_task_timer
 * @return True if the timer is enabled and expired, otherwise false
 */
bool pd_timer_is_expired(int port, enum pd_task_timer timer);

/*
 * pd_timer_manage_expired
 * Convert an active/expired timer to be inactive/expired. This will allow
 * the code to continue to check for expired without having this timer as
 * part of the pd_timer_next_expiration decision.
 *
 * @param port USB-C port number
 */
void pd_timer_manage_expired(int port);

/*
 * pd_timer_next_expiration
 * Retrieve the next active expiration time
 *
 * @param port USB-C port number
 * @return >= 0 is the number of uSeconds until we should wake up.
 *	   -1 no pending timeout
 */
int pd_timer_next_expiration(int port);

/*
 * pd_timer_dump
 * Debug display of the timers for a given port
 *
 * @param port USB-C port number
 */
void pd_timer_dump(int port);

#ifdef TEST_BUILD
/*****************************************************************************
 * TEST_BUILD section
 *
 * This is solely for the use of unit testing.  Most of the inner workings
 * of PD timer are internal static, so they have to be allowed access in
 * order to unit test the basics of the code.
 *
 * If you are interested in the workings of PD timers please refer to
 * common/usbc/usb_pd_timer.c
 */

/* exported: number of USB-C ports */
#define MAX_PD_PORTS CONFIG_USB_PD_PORT_MAX_COUNT

/* PD timers have three possible states: Active, Inactive and Disabled */
/* exported: timer_active indicates if a timer is currently active */
extern ATOMIC_DEFINE(timer_active, PD_TIMER_COUNT *MAX_PD_PORTS);
/* exported: timer_disabled indicates if a timer is currently disabled */
extern ATOMIC_DEFINE(timer_disabled, PD_TIMER_COUNT *MAX_PD_PORTS);

/* exported: set/clear/check the current timer_active for a timer */
#define PD_SET_ACTIVE(p, bit) \
	atomic_set_bit(timer_active, (p) * PD_TIMER_COUNT + (bit))
#define PD_CLR_ACTIVE(p, bit) \
	atomic_clear_bit(timer_active, (p) * PD_TIMER_COUNT + (bit))
#define PD_CHK_ACTIVE(p, bit) \
	atomic_test_bit(timer_active, (p) * PD_TIMER_COUNT + (bit))

/* exported: set/clear/check the current timer_disabled for a timer */
#define PD_SET_DISABLED(p, bit) \
	atomic_set_bit(timer_disabled, (p) * PD_TIMER_COUNT + (bit))
#define PD_CLR_DISABLED(p, bit) \
	atomic_clear_bit(timer_disabled, (p) * PD_TIMER_COUNT + (bit))
#define PD_CHK_DISABLED(p, bit) \
	atomic_test_bit(timer_disabled, (p) * PD_TIMER_COUNT + (bit))

#endif /* TEST_BUILD */

#endif /* __CROS_EC_USB_PD_TIMER_H */

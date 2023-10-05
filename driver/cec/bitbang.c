/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#include "cec_bitbang_chip.h"
#include "console.h"
#include "driver/cec/bitbang.h"
#include "gpio.h"
#include "printf.h"
#include "task.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_CEC, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CEC, format, ##args)

#ifdef CONFIG_CEC_DEBUG
#define DEBUG_CPRINTF(format, args...) cprintf(CC_CEC, format, ##args)
#define DEBUG_CPRINTS(format, args...) cprints(CC_CEC, format, ##args)
#else
#define DEBUG_CPRINTF(...)
#define DEBUG_CPRINTS(...)
#endif

/*
 * Free time timing (us). Our free-time is calculated from the end of
 * the last bit (not from the start). We compensate by having one
 * free-time period less than in the spec.
 */
#define NOMINAL_BIT_TICKS CEC_US_TO_TICKS(CEC_NOMINAL_BIT_PERIOD_US)
#define FREE_TIME_RS_TICKS \
	CEC_US_TO_TICKS(CEC_FREE_TIME_RS_US - CEC_NOMINAL_BIT_PERIOD_US)
#define FREE_TIME_NI_TICKS \
	CEC_US_TO_TICKS(CEC_FREE_TIME_NI_US - CEC_NOMINAL_BIT_PERIOD_US)
#define FREE_TIME_PI_TICKS \
	CEC_US_TO_TICKS(CEC_FREE_TIME_PI_US - CEC_NOMINAL_BIT_PERIOD_US)

/* Start bit timing */
#define START_BIT_LOW_TICKS CEC_US_TO_TICKS(3700)
#define START_BIT_MIN_LOW_TICKS CEC_US_TO_TICKS(3500)
#define START_BIT_MAX_LOW_TICKS CEC_US_TO_TICKS(3900)
#define START_BIT_HIGH_TICKS CEC_US_TO_TICKS(800)
#define START_BIT_MIN_DURATION_TICKS CEC_US_TO_TICKS(4300)
#define START_BIT_MAX_DURATION_TICKS CEC_US_TO_TICKS(5700)

/* Data bit timing */
#define DATA_ZERO_LOW_TICKS CEC_US_TO_TICKS(1500)
#define DATA_ZERO_MIN_LOW_TICKS CEC_US_TO_TICKS(1300)
#define DATA_ZERO_MAX_LOW_TICKS CEC_US_TO_TICKS(1700)
#define DATA_ZERO_HIGH_TICKS CEC_US_TO_TICKS(900)
#define DATA_ZERO_MIN_DURATION_TICKS CEC_US_TO_TICKS(2050)
#define DATA_ZERO_MAX_DURATION_TICKS CEC_US_TO_TICKS(2750)

#define DATA_ONE_LOW_TICKS CEC_US_TO_TICKS(600)
#define DATA_ONE_MIN_LOW_TICKS CEC_US_TO_TICKS(400)
#define DATA_ONE_MAX_LOW_TICKS CEC_US_TO_TICKS(800)
#define DATA_ONE_HIGH_TICKS CEC_US_TO_TICKS(1800)
#define DATA_ONE_MIN_DURATION_TICKS CEC_US_TO_TICKS(2050)
#define DATA_ONE_MAX_DURATION_TICKS CEC_US_TO_TICKS(2750)

/* Time from low that it should be safe to sample an ACK */
#define NOMINAL_SAMPLE_TIME_TICKS CEC_US_TO_TICKS(1050)

#define DATA_TIME(type, data) \
	((data) ? (DATA_ONE_##type##_TICKS) : (DATA_ZERO_##type##_TICKS))
#define DATA_HIGH(data) DATA_TIME(HIGH, data)
#define DATA_LOW(data) DATA_TIME(LOW, data)

/*
 * Number of short pulses seen before the debounce logic goes into ignoring
 * the bus for DEBOUNCE_WAIT_LONG instead of DEBOUNCE_WAIT_SHORT
 */
#define DEBOUNCE_CUTOFF 3

/* The limit how short a start-bit can be to trigger debounce logic */
#define DEBOUNCE_LIMIT_TICKS CEC_US_TO_TICKS(200)
/* The time we ignore the bus for the first three debounce cases */
#define DEBOUNCE_WAIT_SHORT_TICKS CEC_US_TO_TICKS(100)
/* The time we ignore the bus after the three initial debounce cases */
#define DEBOUNCE_WAIT_LONG_TICKS CEC_US_TO_TICKS(500)

/*
 * The variance in timing we allow outside of the CEC specification for
 * incoming signals. Our measurements aren't 100% accurate either, so this
 * gives some robustness.
 */
#define VALID_TOLERANCE_TICKS CEC_US_TO_TICKS(100)

/*
 * Defines used for setting capture timers to a point where we are
 * sure that if we get a timeout, something is wrong.
 */
#define CAP_START_LOW_TICKS (START_BIT_MAX_LOW_TICKS + VALID_TOLERANCE_TICKS)
#define CAP_START_HIGH_TICKS                                      \
	(START_BIT_MAX_DURATION_TICKS - START_BIT_MIN_LOW_TICKS + \
	 VALID_TOLERANCE_TICKS)
#define CAP_DATA_LOW_TICKS (DATA_ZERO_MAX_LOW_TICKS + VALID_TOLERANCE_TICKS)
#define CAP_DATA_HIGH_TICKS                                     \
	(DATA_ONE_MAX_DURATION_TICKS - DATA_ONE_MIN_LOW_TICKS + \
	 VALID_TOLERANCE_TICKS)

#define VALID_TIME(type, bit, t)                                          \
	((t) >= ((bit##_MIN_##type##_TICKS) - (VALID_TOLERANCE_TICKS)) && \
	 (t) <= (bit##_MAX_##type##_TICKS) + (VALID_TOLERANCE_TICKS))
#define VALID_LOW(bit, t) VALID_TIME(LOW, bit, t)
#define VALID_HIGH(bit, low_time, high_time)                   \
	(((low_time) + (high_time) <=                          \
	  bit##_MAX_DURATION_TICKS + VALID_TOLERANCE_TICKS) && \
	 ((low_time) + (high_time) >=                          \
	  bit##_MIN_DURATION_TICKS - VALID_TOLERANCE_TICKS))
#define VALID_DATA_HIGH(data, low_time, high_time)            \
	((data) ? VALID_HIGH(DATA_ONE, low_time, high_time) : \
		  VALID_HIGH(DATA_ZERO, low_time, high_time))

/*
 * CEC state machine states. Each state typically takes action on entry and
 * timeouts. INITIATIOR states are used for sending, FOLLOWER states are used
 *  for receiving.
 */
enum cec_state {
	CEC_STATE_DISABLED = 0,
	CEC_STATE_IDLE,
	CEC_STATE_INITIATOR_FREE_TIME,
	CEC_STATE_INITIATOR_START_LOW,
	CEC_STATE_INITIATOR_START_HIGH,
	CEC_STATE_INITIATOR_HEADER_INIT_LOW,
	CEC_STATE_INITIATOR_HEADER_INIT_HIGH,
	CEC_STATE_INITIATOR_HEADER_DEST_LOW,
	CEC_STATE_INITIATOR_HEADER_DEST_HIGH,
	CEC_STATE_INITIATOR_DATA_LOW,
	CEC_STATE_INITIATOR_DATA_HIGH,
	CEC_STATE_INITIATOR_EOM_LOW,
	CEC_STATE_INITIATOR_EOM_HIGH,
	CEC_STATE_INITIATOR_ACK_LOW,
	CEC_STATE_INITIATOR_ACK_HIGH,
	CEC_STATE_INITIATOR_ACK_VERIFY,
	CEC_STATE_FOLLOWER_START_LOW,
	CEC_STATE_FOLLOWER_START_HIGH,
	CEC_STATE_FOLLOWER_DEBOUNCE,
	CEC_STATE_FOLLOWER_HEADER_INIT_LOW,
	CEC_STATE_FOLLOWER_HEADER_INIT_HIGH,
	CEC_STATE_FOLLOWER_HEADER_DEST_LOW,
	CEC_STATE_FOLLOWER_HEADER_DEST_HIGH,
	CEC_STATE_FOLLOWER_EOM_LOW,
	CEC_STATE_FOLLOWER_EOM_HIGH,
	CEC_STATE_FOLLOWER_ACK_LOW,
	CEC_STATE_FOLLOWER_ACK_VERIFY,
	CEC_STATE_FOLLOWER_ACK_FINISH,
	CEC_STATE_FOLLOWER_DATA_LOW,
	CEC_STATE_FOLLOWER_DATA_HIGH,
};

/* Receive buffer and states */
struct cec_rx {
	/*
	 * The current incoming message being parsed. Copied to received_message
	 * on completion.
	 */
	struct cec_msg_transfer transfer;
	/* End of Message received from source? */
	uint8_t eom;
	/* A follower NAK:ed a broadcast transfer */
	uint8_t broadcast_nak;
	/*
	 * Keep track of pulse low time to be able to verify
	 * pulse duration
	 */
	int low_ticks;
	/* Number of too short pulses seen in a row */
	int debounce_count;
	/* Flag indicating whether received_message is available */
	uint8_t received_message_available;
	/*
	 * The transfer is copied here when complete. This allows us to start
	 * receiving a new message before the common code has read out the
	 * previous one.
	 */
	struct cec_msg_transfer received_message;
};

/* Transfer buffer and states */
struct cec_tx {
	/* Outgoing message */
	struct cec_msg_transfer transfer;
	/* Message length */
	uint8_t len;
	/* Number of resends attempted in current send */
	uint8_t resends;
	/* Acknowledge received from sink? */
	uint8_t ack;
	/*
	 * When sending multiple concurrent frames,
	 * the free-time is slightly higher
	 */
	int present_initiator;
};

struct cec_port_data {
	/* Single state for CEC. We are INITIATOR, FOLLOWER or IDLE */
	enum cec_state state;

	/* Parameters and buffers for follower (receiver) state */
	struct cec_rx rx;

	/* Parameters and buffer for initiator (sender) state */
	struct cec_tx tx;

	/*
	 * CEC address of ourself. We ack incoming packages on this address.
	 * However, the AP is responsible for writing the initiator address
	 * on writes. CEC_INVALID_ADDR means means that the address hasn't
	 * been set by the AP yet.
	 */
	uint8_t addr;
};

/* TODO(b/296813751): Implement a common data structure for CEC drivers */
static struct cec_port_data cec_port_data[CEC_PORT_COUNT];

static void enter_state(int port, enum cec_state new_state)
{
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;
	struct cec_port_data *port_data = &cec_port_data[port];
	int gpio = -1, timeout = -1;
	enum cec_cap_edge cap_edge = CEC_CAP_EDGE_NONE;
	uint8_t addr;

	port_data->state = new_state;
	switch (new_state) {
	case CEC_STATE_DISABLED:
		gpio = 1;
		memset(&port_data->rx, 0, sizeof(struct cec_rx));
		memset(&port_data->tx, 0, sizeof(struct cec_tx));
		break;
	case CEC_STATE_IDLE:
		port_data->tx.transfer.bit = 0;
		port_data->tx.transfer.byte = 0;
		port_data->rx.transfer.bit = 0;
		port_data->rx.transfer.byte = 0;
		if (port_data->tx.len > 0) {
			/* Execute a postponed send */
			enter_state(port, CEC_STATE_INITIATOR_FREE_TIME);
		} else {
			/* Wait for incoming command */
			gpio = 1;
			cap_edge = CEC_CAP_EDGE_FALLING;
			timeout = 0;
		}
		break;
	case CEC_STATE_INITIATOR_FREE_TIME:
		gpio = 1;
		cap_edge = CEC_CAP_EDGE_FALLING;
		if (port_data->tx.resends)
			timeout = FREE_TIME_RS_TICKS;
		else if (port_data->tx.present_initiator)
			timeout = FREE_TIME_PI_TICKS;
		else
			timeout = FREE_TIME_NI_TICKS;
		break;
	case CEC_STATE_INITIATOR_START_LOW:
		port_data->tx.present_initiator = 1;
		port_data->tx.transfer.bit = 0;
		port_data->tx.transfer.byte = 0;
		gpio = 0;
		timeout = START_BIT_LOW_TICKS;
		break;
	case CEC_STATE_INITIATOR_START_HIGH:
		gpio = 1;
		cap_edge = CEC_CAP_EDGE_FALLING;
		timeout = START_BIT_HIGH_TICKS;
		break;
	case CEC_STATE_INITIATOR_HEADER_INIT_LOW:
	case CEC_STATE_INITIATOR_HEADER_DEST_LOW:
	case CEC_STATE_INITIATOR_DATA_LOW:
		gpio = 0;
		timeout =
			DATA_LOW(cec_transfer_get_bit(&port_data->tx.transfer));
		break;
	case CEC_STATE_INITIATOR_HEADER_INIT_HIGH:
		gpio = 1;
		cap_edge = CEC_CAP_EDGE_FALLING;
		timeout = DATA_HIGH(
			cec_transfer_get_bit(&port_data->tx.transfer));
		break;
	case CEC_STATE_INITIATOR_HEADER_DEST_HIGH:
	case CEC_STATE_INITIATOR_DATA_HIGH:
		gpio = 1;
		timeout = DATA_HIGH(
			cec_transfer_get_bit(&port_data->tx.transfer));
		break;
	case CEC_STATE_INITIATOR_EOM_LOW:
		gpio = 0;
		timeout = DATA_LOW(cec_transfer_is_eom(&port_data->tx.transfer,
						       port_data->tx.len));
		break;
	case CEC_STATE_INITIATOR_EOM_HIGH:
		gpio = 1;
		timeout = DATA_HIGH(cec_transfer_is_eom(&port_data->tx.transfer,
							port_data->tx.len));
		break;
	case CEC_STATE_INITIATOR_ACK_LOW:
		gpio = 0;
		timeout = DATA_LOW(1);
		break;
	case CEC_STATE_INITIATOR_ACK_HIGH:
		gpio = 1;
		/* Aim for the middle of the safe sample time */
		timeout = (DATA_ONE_LOW_TICKS + DATA_ZERO_LOW_TICKS) / 2 -
			  DATA_ONE_LOW_TICKS;
		break;
	case CEC_STATE_INITIATOR_ACK_VERIFY:
		port_data->tx.ack = !gpio_get_level(drv_config->gpio_out);
		if ((port_data->tx.transfer.buf[0] & 0x0f) ==
		    CEC_BROADCAST_ADDR) {
			/*
			 * We are sending a broadcast. Any follower can
			 * can NAK a broadcast message the same way they
			 * would ACK a direct message
			 */
			port_data->tx.ack = !port_data->tx.ack;
		}
		/*
		 * We are at the safe sample time. Wait
		 * until the end of this bit
		 */
		timeout = NOMINAL_BIT_TICKS - NOMINAL_SAMPLE_TIME_TICKS;
		break;
	case CEC_STATE_FOLLOWER_START_LOW:
		port_data->tx.present_initiator = 0;
		cap_edge = CEC_CAP_EDGE_RISING;
		timeout = CAP_START_LOW_TICKS;
		break;
	case CEC_STATE_FOLLOWER_START_HIGH:
		port_data->rx.debounce_count = 0;
		cap_edge = CEC_CAP_EDGE_FALLING;
		timeout = CAP_START_HIGH_TICKS;
		break;
	case CEC_STATE_FOLLOWER_DEBOUNCE:
		cec_debounce_enable(port);
		if (port_data->rx.debounce_count >= DEBOUNCE_CUTOFF) {
			timeout = DEBOUNCE_WAIT_LONG_TICKS;
		} else {
			timeout = DEBOUNCE_WAIT_SHORT_TICKS;
			port_data->rx.debounce_count++;
		}
		break;
	case CEC_STATE_FOLLOWER_HEADER_INIT_LOW:
	case CEC_STATE_FOLLOWER_HEADER_DEST_LOW:
	case CEC_STATE_FOLLOWER_EOM_LOW:
		cap_edge = CEC_CAP_EDGE_RISING;
		timeout = CAP_DATA_LOW_TICKS;
		break;
	case CEC_STATE_FOLLOWER_HEADER_INIT_HIGH:
	case CEC_STATE_FOLLOWER_HEADER_DEST_HIGH:
	case CEC_STATE_FOLLOWER_EOM_HIGH:
		cap_edge = CEC_CAP_EDGE_FALLING;
		timeout = CAP_DATA_HIGH_TICKS;
		break;
	case CEC_STATE_FOLLOWER_ACK_LOW:
		addr = port_data->rx.transfer.buf[0] & 0x0f;
		if (addr == port_data->addr) {
			/* Destination is our address, so ACK the packet */
			gpio = 0;
		}
		/* Don't ack broadcast or packets whose destinations aren't us,
		 * but continue reading.
		 */
		timeout = NOMINAL_SAMPLE_TIME_TICKS;
		break;
	case CEC_STATE_FOLLOWER_ACK_VERIFY:
		/*
		 * We are at safe sample time. A broadcast frame is considered
		 * lost if any follower pulls the line low
		 */
		if ((port_data->rx.transfer.buf[0] & 0x0f) ==
		    CEC_BROADCAST_ADDR)
			port_data->rx.broadcast_nak =
				!gpio_get_level(drv_config->gpio_out);
		else
			port_data->rx.broadcast_nak = 0;

		/*
		 * We release the ACK at the end of data zero low
		 * period (ACK is technically a zero).
		 */
		timeout = DATA_ZERO_LOW_TICKS - NOMINAL_SAMPLE_TIME_TICKS;
		break;
	case CEC_STATE_FOLLOWER_ACK_FINISH:
		gpio = 1;
		if (port_data->rx.eom ||
		    port_data->rx.transfer.byte >= MAX_CEC_MSG_LEN) {
			addr = port_data->rx.transfer.buf[0] & 0x0f;
			if (addr == port_data->addr ||
			    addr == CEC_BROADCAST_ADDR) {
				/*
				 * If common code has not read the previous
				 * message yet, discard it and keep the most
				 * recent one.
				 */
				if (port_data->rx.received_message_available)
					DEBUG_CPRINTS(
						"CEC%d: received message not "
						"read out, discarding",
						port);
				memcpy(&port_data->rx.received_message,
				       &port_data->rx.transfer,
				       sizeof(port_data->rx.received_message));
				port_data->rx.received_message_available = 1;
				cec_task_set_event(
					port, CEC_TASK_EVENT_RECEIVED_DATA);
			}
			timeout = DATA_ZERO_HIGH_TICKS;
		} else {
			cap_edge = CEC_CAP_EDGE_FALLING;
			timeout = CAP_DATA_HIGH_TICKS;
		}
		break;
	case CEC_STATE_FOLLOWER_DATA_LOW:
		cap_edge = CEC_CAP_EDGE_RISING;
		timeout = CAP_DATA_LOW_TICKS;
		break;
	case CEC_STATE_FOLLOWER_DATA_HIGH:
		cap_edge = CEC_CAP_EDGE_FALLING;
		timeout = CAP_DATA_HIGH_TICKS;
		break;
		/* No default case, since all states must be handled explicitly
		 */
	}

	if (gpio >= 0) {
		gpio_set_level(drv_config->gpio_out, gpio);
		/*
		 * Changing the level of the output gpio triggers an unwanted
		 * interrupt on the input gpio.
		 */
		gpio_clear_pending_interrupt(drv_config->gpio_in);
	}
	if (timeout >= 0) {
		cec_tmr_cap_start(port, cap_edge, timeout);
	}
}

void cec_event_timeout(int port)
{
	struct cec_port_data *port_data = &cec_port_data[port];

	switch (port_data->state) {
	case CEC_STATE_DISABLED:
	case CEC_STATE_IDLE:
		break;
	case CEC_STATE_INITIATOR_FREE_TIME:
		enter_state(port, CEC_STATE_INITIATOR_START_LOW);
		break;
	case CEC_STATE_INITIATOR_START_LOW:
		enter_state(port, CEC_STATE_INITIATOR_START_HIGH);
		break;
	case CEC_STATE_INITIATOR_START_HIGH:
		enter_state(port, CEC_STATE_INITIATOR_HEADER_INIT_LOW);
		break;
	case CEC_STATE_INITIATOR_HEADER_INIT_LOW:
		enter_state(port, CEC_STATE_INITIATOR_HEADER_INIT_HIGH);
		break;
	case CEC_STATE_INITIATOR_HEADER_INIT_HIGH:
		cec_transfer_inc_bit(&port_data->tx.transfer);
		if (port_data->tx.transfer.bit == 4)
			enter_state(port, CEC_STATE_INITIATOR_HEADER_DEST_LOW);
		else
			enter_state(port, CEC_STATE_INITIATOR_HEADER_INIT_LOW);
		break;
	case CEC_STATE_INITIATOR_HEADER_DEST_LOW:
		enter_state(port, CEC_STATE_INITIATOR_HEADER_DEST_HIGH);
		break;
	case CEC_STATE_INITIATOR_HEADER_DEST_HIGH:
		cec_transfer_inc_bit(&port_data->tx.transfer);
		if (port_data->tx.transfer.byte == 1)
			enter_state(port, CEC_STATE_INITIATOR_EOM_LOW);
		else
			enter_state(port, CEC_STATE_INITIATOR_HEADER_DEST_LOW);
		break;
	case CEC_STATE_INITIATOR_EOM_LOW:
		enter_state(port, CEC_STATE_INITIATOR_EOM_HIGH);
		break;
	case CEC_STATE_INITIATOR_EOM_HIGH:
		enter_state(port, CEC_STATE_INITIATOR_ACK_LOW);
		break;
	case CEC_STATE_INITIATOR_ACK_LOW:
		enter_state(port, CEC_STATE_INITIATOR_ACK_HIGH);
		break;
	case CEC_STATE_INITIATOR_ACK_HIGH:
		enter_state(port, CEC_STATE_INITIATOR_ACK_VERIFY);
		break;
	case CEC_STATE_INITIATOR_ACK_VERIFY:
		if (port_data->tx.ack) {
			if (!cec_transfer_is_eom(&port_data->tx.transfer,
						 port_data->tx.len)) {
				/* More data in this frame */
				enter_state(port, CEC_STATE_INITIATOR_DATA_LOW);
			} else {
				/* Transfer completed successfully */
				port_data->tx.len = 0;
				port_data->tx.resends = 0;
				enter_state(port, CEC_STATE_IDLE);
				cec_task_set_event(port, CEC_TASK_EVENT_OKAY);
			}
		} else {
			if (port_data->tx.resends < CEC_MAX_RESENDS) {
				/* Resend */
				port_data->tx.resends++;
				enter_state(port,
					    CEC_STATE_INITIATOR_FREE_TIME);
			} else {
				/* Transfer failed */
				port_data->tx.len = 0;
				port_data->tx.resends = 0;
				enter_state(port, CEC_STATE_IDLE);
				cec_task_set_event(port, CEC_TASK_EVENT_FAILED);
			}
		}
		break;
	case CEC_STATE_INITIATOR_DATA_LOW:
		enter_state(port, CEC_STATE_INITIATOR_DATA_HIGH);
		break;
	case CEC_STATE_INITIATOR_DATA_HIGH:
		cec_transfer_inc_bit(&port_data->tx.transfer);
		if (port_data->tx.transfer.bit == 0)
			enter_state(port, CEC_STATE_INITIATOR_EOM_LOW);
		else
			enter_state(port, CEC_STATE_INITIATOR_DATA_LOW);
		break;
	case CEC_STATE_FOLLOWER_ACK_LOW:
		enter_state(port, CEC_STATE_FOLLOWER_ACK_VERIFY);
		break;
	case CEC_STATE_FOLLOWER_ACK_VERIFY:
		if (port_data->rx.broadcast_nak)
			enter_state(port, CEC_STATE_IDLE);
		else
			enter_state(port, CEC_STATE_FOLLOWER_ACK_FINISH);
		break;
	case CEC_STATE_FOLLOWER_DEBOUNCE:
		cec_debounce_disable(port);
		enter_state(port, CEC_STATE_IDLE);
		break;
	case CEC_STATE_FOLLOWER_START_LOW:
	case CEC_STATE_FOLLOWER_START_HIGH:
	case CEC_STATE_FOLLOWER_HEADER_INIT_LOW:
	case CEC_STATE_FOLLOWER_HEADER_INIT_HIGH:
	case CEC_STATE_FOLLOWER_HEADER_DEST_LOW:
	case CEC_STATE_FOLLOWER_HEADER_DEST_HIGH:
	case CEC_STATE_FOLLOWER_EOM_LOW:
	case CEC_STATE_FOLLOWER_EOM_HIGH:
	case CEC_STATE_FOLLOWER_ACK_FINISH:
	case CEC_STATE_FOLLOWER_DATA_LOW:
	case CEC_STATE_FOLLOWER_DATA_HIGH:
		enter_state(port, CEC_STATE_IDLE);
		break;
	}
}

void cec_event_cap(int port)
{
	struct cec_port_data *port_data = &cec_port_data[port];
	int t;
	int data;

	switch (port_data->state) {
	case CEC_STATE_IDLE:
		/* A falling edge during idle, likely a start bit */
		enter_state(port, CEC_STATE_FOLLOWER_START_LOW);
		break;
	case CEC_STATE_INITIATOR_FREE_TIME:
	case CEC_STATE_INITIATOR_START_HIGH:
	case CEC_STATE_INITIATOR_HEADER_INIT_HIGH:
		/*
		 * A falling edge during free-time, postpone
		 * this send and listen
		 */
		port_data->tx.transfer.bit = 0;
		port_data->tx.transfer.byte = 0;
		enter_state(port, CEC_STATE_FOLLOWER_START_LOW);
		break;
	case CEC_STATE_FOLLOWER_START_LOW:
		/* Rising edge of start bit, validate low time */
		t = cec_tmr_cap_get(port);
		if (VALID_LOW(START_BIT, t)) {
			port_data->rx.low_ticks = t;
			enter_state(port, CEC_STATE_FOLLOWER_START_HIGH);
		} else if (t < DEBOUNCE_LIMIT_TICKS) {
			/* Wait a bit if start-pulses are really short */
			enter_state(port, CEC_STATE_FOLLOWER_DEBOUNCE);
		} else {
			enter_state(port, CEC_STATE_IDLE);
		}
		break;
	case CEC_STATE_FOLLOWER_START_HIGH:
		if (VALID_HIGH(START_BIT, port_data->rx.low_ticks,
			       cec_tmr_cap_get(port)))
			enter_state(port, CEC_STATE_FOLLOWER_HEADER_INIT_LOW);
		else
			enter_state(port, CEC_STATE_IDLE);
		break;
	case CEC_STATE_FOLLOWER_HEADER_INIT_LOW:
	case CEC_STATE_FOLLOWER_HEADER_DEST_LOW:
	case CEC_STATE_FOLLOWER_DATA_LOW:
		t = cec_tmr_cap_get(port);
		if (VALID_LOW(DATA_ZERO, t)) {
			port_data->rx.low_ticks = t;
			cec_transfer_set_bit(&port_data->rx.transfer, 0);
			enter_state(port, port_data->state + 1);
		} else if (VALID_LOW(DATA_ONE, t)) {
			port_data->rx.low_ticks = t;
			cec_transfer_set_bit(&port_data->rx.transfer, 1);
			enter_state(port, port_data->state + 1);
		} else {
			enter_state(port, CEC_STATE_IDLE);
		}
		break;
	case CEC_STATE_FOLLOWER_HEADER_INIT_HIGH:
		t = cec_tmr_cap_get(port);
		data = cec_transfer_get_bit(&port_data->rx.transfer);
		if (VALID_DATA_HIGH(data, port_data->rx.low_ticks, t)) {
			cec_transfer_inc_bit(&port_data->rx.transfer);
			if (port_data->rx.transfer.bit == 4)
				enter_state(port,
					    CEC_STATE_FOLLOWER_HEADER_DEST_LOW);
			else
				enter_state(port,
					    CEC_STATE_FOLLOWER_HEADER_INIT_LOW);
		} else {
			enter_state(port, CEC_STATE_IDLE);
		}
		break;
	case CEC_STATE_FOLLOWER_HEADER_DEST_HIGH:
		t = cec_tmr_cap_get(port);
		data = cec_transfer_get_bit(&port_data->rx.transfer);
		if (VALID_DATA_HIGH(data, port_data->rx.low_ticks, t)) {
			cec_transfer_inc_bit(&port_data->rx.transfer);
			if (port_data->rx.transfer.bit == 0)
				enter_state(port, CEC_STATE_FOLLOWER_EOM_LOW);
			else
				enter_state(port,
					    CEC_STATE_FOLLOWER_HEADER_DEST_LOW);
		} else {
			enter_state(port, CEC_STATE_IDLE);
		}
		break;
	case CEC_STATE_FOLLOWER_EOM_LOW:
		t = cec_tmr_cap_get(port);
		if (VALID_LOW(DATA_ZERO, t)) {
			port_data->rx.low_ticks = t;
			port_data->rx.eom = 0;
			enter_state(port, CEC_STATE_FOLLOWER_EOM_HIGH);
		} else if (VALID_LOW(DATA_ONE, t)) {
			port_data->rx.low_ticks = t;
			port_data->rx.eom = 1;
			enter_state(port, CEC_STATE_FOLLOWER_EOM_HIGH);
		} else {
			enter_state(port, CEC_STATE_IDLE);
		}
		break;
	case CEC_STATE_FOLLOWER_EOM_HIGH:
		t = cec_tmr_cap_get(port);
		data = port_data->rx.eom;
		if (VALID_DATA_HIGH(data, port_data->rx.low_ticks, t))
			enter_state(port, CEC_STATE_FOLLOWER_ACK_LOW);
		else
			enter_state(port, CEC_STATE_IDLE);
		break;
	case CEC_STATE_FOLLOWER_ACK_LOW:
		enter_state(port, CEC_STATE_FOLLOWER_ACK_FINISH);
		break;
	case CEC_STATE_FOLLOWER_ACK_FINISH:
		enter_state(port, CEC_STATE_FOLLOWER_DATA_LOW);
		break;
	case CEC_STATE_FOLLOWER_DATA_HIGH:
		t = cec_tmr_cap_get(port);
		data = cec_transfer_get_bit(&port_data->rx.transfer);
		if (VALID_DATA_HIGH(data, port_data->rx.low_ticks, t)) {
			cec_transfer_inc_bit(&port_data->rx.transfer);
			if (port_data->rx.transfer.bit == 0)
				enter_state(port, CEC_STATE_FOLLOWER_EOM_LOW);
			else
				enter_state(port, CEC_STATE_FOLLOWER_DATA_LOW);
		} else {
			enter_state(port, CEC_STATE_IDLE);
		}
		break;
	default:
		break;
	}
}

void cec_event_tx(int port)
{
	/*
	 * If we have an ongoing receive, this transfer
	 * will start when transitioning to IDLE
	 */
	if (cec_port_data[port].state == CEC_STATE_IDLE) {
		/*
		 * Only update the interrupt time if it's idle, otherwise it
		 * will interfere with the timing of the current transfer.
		 */
		cec_update_interrupt_time(port);
		enter_state(port, CEC_STATE_INITIATOR_FREE_TIME);
	}
}

__overridable void cec_update_interrupt_time(int port)
{
}

static int bitbang_cec_init(int port)
{
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;

	cec_port_data[port].addr = CEC_INVALID_ADDR;

	cec_init_timer(port);

	/* If RO doesn't set it, RW needs to set it explicitly. */
	gpio_set_level(drv_config->gpio_pull_up, 1);

	/* Ensure the CEC bus is not pulled low by default on startup. */
	gpio_set_level(drv_config->gpio_out, 1);

	return EC_SUCCESS;
}

static int bitbang_cec_get_enable(int port, uint8_t *enable)
{
	*enable = cec_port_data[port].state == CEC_STATE_DISABLED ? 0 : 1;

	return EC_SUCCESS;
}

static int bitbang_cec_set_enable(int port, uint8_t enable)
{
	/* Enabling when already enabled? */
	if (enable && cec_port_data[port].state != CEC_STATE_DISABLED)
		return EC_SUCCESS;

	/* Disabling when already disabled? */
	if (!enable && cec_port_data[port].state == CEC_STATE_DISABLED)
		return EC_SUCCESS;

	if (enable) {
		enter_state(port, CEC_STATE_IDLE);

		cec_enable_timer(port);

		CPRINTS("CEC%d enabled", port);
	} else {
		cec_disable_timer(port);

		enter_state(port, CEC_STATE_DISABLED);

		CPRINTS("CEC%d disabled", port);
	}

	return EC_SUCCESS;
}

static int bitbang_cec_get_logical_addr(int port, uint8_t *logical_addr)
{
	*logical_addr = cec_port_data[port].addr;

	return EC_SUCCESS;
}

static int bitbang_cec_set_logical_addr(int port, uint8_t logical_addr)
{
	cec_port_data[port].addr = logical_addr;
	CPRINTS("CEC%d address set to: %u", port, logical_addr);

	return EC_SUCCESS;
}

static int bitbang_cec_send(int port, const uint8_t *msg, uint8_t len)
{
	char str_buf[hex_str_buf_size(len)];

	if (cec_port_data[port].state == CEC_STATE_DISABLED)
		return EC_ERROR_BUSY;

	if (cec_port_data[port].tx.len != 0)
		return EC_ERROR_BUSY;

	cec_port_data[port].tx.len = len;

	snprintf_hex_buffer(str_buf, sizeof(str_buf), HEX_BUF(msg, len));
	DEBUG_CPRINTS("CEC%d send: 0x%s", port, str_buf);

	memcpy(cec_port_data[port].tx.transfer.buf, msg, len);

	cec_trigger_send(port);

	return EC_SUCCESS;
}

static int bitbang_cec_get_received_message(int port, uint8_t **msg,
					    uint8_t *len)
{
	if (!cec_port_data[port].rx.received_message_available)
		return EC_ERROR_UNAVAILABLE;

	*msg = cec_port_data[port].rx.received_message.buf;
	*len = cec_port_data[port].rx.received_message.byte;
	cec_port_data[port].rx.received_message_available = 0;

	return EC_SUCCESS;
}

const struct cec_drv bitbang_cec_drv = {
	.init = &bitbang_cec_init,
	.get_enable = &bitbang_cec_get_enable,
	.set_enable = &bitbang_cec_set_enable,
	.get_logical_addr = &bitbang_cec_get_logical_addr,
	.set_logical_addr = &bitbang_cec_set_logical_addr,
	.send = &bitbang_cec_send,
	.get_received_message = &bitbang_cec_get_received_message,
};

/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "cec.h"
#include "clock.h"
#include "console.h"
#include "driver/cec/it83xx.h"
#include "gpio.h"
#include "hooks.h"
#include "printf.h"
#include "registers.h"
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

enum cec_state {
	CEC_STATE_DISABLED = 0,
	CEC_STATE_IDLE,
	CEC_STATE_FREE_TIME,
	CEC_STATE_TRANSMITTING,
	CEC_STATE_RECEIVING,
};

enum cec_event {
	/* Interrupt events. Values match bits in CECSTS/CECIE registers. */
	CEC_EVENT_BTE = BIT(0),
	CEC_EVENT_BLE = BIT(1),
	CEC_EVENT_CEN = BIT(2),
	CEC_EVENT_CLE = BIT(3),
	CEC_EVENT_DBD = BIT(4),
	CEC_EVENT_HDRCV = BIT(5),

	/* Other events */
	CEC_EVENT_TRANSMIT = BIT(8),
	CEC_EVENT_FREE_TIME_COMPLETE = BIT(9),
};

#define CEC_ALL_INTERRUPTS                                               \
	(CEC_EVENT_BTE | CEC_EVENT_BLE | CEC_EVENT_CEN | CEC_EVENT_CLE | \
	 CEC_EVENT_DBD | CEC_EVENT_HDRCV)

/* Receive buffer and states */
struct cec_rx {
	/*
	 * The message currently being received. Copied to received_message on
	 * completion.
	 */
	struct cec_msg_transfer transfer;
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
	/* The message currently being transmitted */
	struct cec_msg_transfer transfer;
	/* Message length */
	uint8_t len;
	/* Number of resends attempted in current send */
	uint8_t resends;
	/* When sending multiple concurrent frames, the free time is higher */
	int present_initiator;
};

static enum cec_state cec_state;

/* Events from enum cec_event */
static atomic_t cec_events;

static struct cec_rx cec_rx;
static struct cec_tx cec_tx;

static void cec_set_events(uint32_t event);

static void free_time_complete(void)
{
	cec_set_events(CEC_EVENT_FREE_TIME_COMPLETE);
}
DECLARE_DEFERRED(free_time_complete);

static void start_free_time(void)
{
	int free_time_us;

	cec_state = CEC_STATE_FREE_TIME;

	/*
	 * Our free-time is calculated from the end of the last bit (not from
	 * the start). We compensate by having one free-time period less than in
	 * the spec.
	 */
	if (cec_tx.resends)
		free_time_us = CEC_FREE_TIME_RS_US - CEC_NOMINAL_BIT_PERIOD_US;
	else if (cec_tx.present_initiator)
		free_time_us = CEC_FREE_TIME_PI_US - CEC_NOMINAL_BIT_PERIOD_US;
	else
		free_time_us = CEC_FREE_TIME_NI_US - CEC_NOMINAL_BIT_PERIOD_US;

	hook_call_deferred(&free_time_complete_data, free_time_us);
}

/*
 * This resets all state so that we're ready to receive or transmit again.
 * It can be called in response to any errors or invalid states.
 */
static void enter_idle_state(void)
{
	cec_tx.transfer.byte = 0;
	cec_rx.transfer.byte = 0;

	/* Clear FIFO */
	IT83XX_CEC_CECFSTS |= IT83XX_CEC_CECFSTS_FCLR;

	/* Ensure we're in follower mode */
	IT83XX_CEC_CECOPSTS &= ~IT83XX_CEC_CECOPSTS_DMS;

	cec_state = CEC_STATE_IDLE;

	if (cec_tx.len > 0) {
		/* Start a postponed send */
		start_free_time();
	}
}

static void write_byte(void)
{
	/* Add byte to FIFO */
	IT83XX_CEC_CECDR = cec_tx.transfer.buf[cec_tx.transfer.byte];

	/* Set EOM if this is the last byte, otherwise clear it */
	if (cec_tx.transfer.byte == cec_tx.len - 1)
		IT83XX_CEC_CECCTRL |= IT83XX_CEC_CECCTRL_EOM;
	else
		IT83XX_CEC_CECCTRL &= ~IT83XX_CEC_CECCTRL_EOM;
}

static void received_byte(int port, uint8_t data)
{
	cec_rx.transfer.buf[cec_rx.transfer.byte++] = data;

	/* Check if EOM is set */
	if (IT83XX_CEC_CECOPSTS & IT83XX_CEC_CECOPSTS_EB) {
		/* Message received successfully */
		memcpy(&cec_rx.received_message, &cec_rx.transfer,
		       sizeof(cec_rx.received_message));
		cec_rx.received_message_available = 1;
		cec_task_set_event(port, CEC_TASK_EVENT_RECEIVED_DATA);
		enter_idle_state();
		return;
	}

	/*
	 * If we've received the max number of bytes but EOM is not set, the
	 * message is invalid so discard it.
	 */
	if (cec_rx.transfer.byte >= MAX_CEC_MSG_LEN) {
		CPRINTS("CEC%d error: received message > %d bytes", port,
			MAX_CEC_MSG_LEN);
		enter_idle_state();
	}
}

static void cec_event_error(int port)
{
	switch (cec_state) {
	case CEC_STATE_DISABLED:
		break;
	case CEC_STATE_IDLE:
		/* Stay in idle state and reset */
	case CEC_STATE_FREE_TIME:
		/*
		 * There has been some (invalid) activity on the bus, so reset
		 * state and restart the free time.
		 */
	case CEC_STATE_TRANSMITTING:
	case CEC_STATE_RECEIVING:
		/* Abort the current transfer */
		enter_idle_state();
		break;
	}
}

static void cec_event_dbd(int port)
{
	bool ack_received;

	switch (cec_state) {
	case CEC_STATE_DISABLED:
		break;
	case CEC_STATE_IDLE:
	case CEC_STATE_FREE_TIME:
		/*
		 * It should be impossible to get a DBD if we're not currently
		 * transmitting or receiving.
		 */
		CPRINTS("CEC%d error: DBD in invalid state %d", port,
			cec_state);
		enter_idle_state();
		break;
	case CEC_STATE_TRANSMITTING:
		/* Previous byte transmitted successfully, move to next byte */
		cec_tx.transfer.byte++;

		/* ACK bit 0 means follower set the ACK bit */
		ack_received = !(IT83XX_CEC_CECOPSTS & IT83XX_CEC_CECOPSTS_AB);

		/* For broadcast messages the ACK bit is a NACK */
		if ((cec_tx.transfer.buf[0] & 0xf) == CEC_BROADCAST_ADDR)
			ack_received = !ack_received;

		if (ack_received) {
			if (cec_tx.transfer.byte == cec_tx.len) {
				/* Transfer completed successfully. */
				cec_tx.len = 0;
				cec_tx.resends = 0;
				enter_idle_state();
				cec_task_set_event(port, CEC_TASK_EVENT_OKAY);
			} else {
				/* Write the next byte */
				write_byte();
			}
		} else {
			if (cec_tx.resends < CEC_MAX_RESENDS) {
				/* Resend */
				cec_tx.resends++;
				start_free_time();
			} else {
				/* Transfer failed */
				cec_tx.len = 0;
				cec_tx.resends = 0;
				enter_idle_state();
				cec_task_set_event(port, CEC_TASK_EVENT_FAILED);
			}
		}
		break;
	case CEC_STATE_RECEIVING:
		received_byte(port, IT83XX_CEC_CECDR);
		break;
	}
}

static void cec_event_hdrcv(int port)
{
	uint8_t header = IT83XX_CEC_CECRH;
	uint8_t dest = header & 0x0f;

	switch (cec_state) {
	case CEC_STATE_DISABLED:
		break;
	case CEC_STATE_IDLE:
	case CEC_STATE_FREE_TIME:
		cec_tx.present_initiator = 0;

		/*
		 * If we receive a message not destined to us and not broadcast,
		 * the CEC peripheral will send a HDRCV interrupt for the
		 * header, but no DBD interrupts for the following data. So we
		 * should stop listening now and not enter the RECEIVING state.
		 */
		if (dest != (IT83XX_CEC_CECDLA & IT83XX_CEC_CECDLA_DLA) &&
		    dest != CEC_BROADCAST_ADDR)
			break;

		/* Start receiving */
		cec_state = CEC_STATE_RECEIVING;
		cec_rx.transfer.byte = 0;
		received_byte(port, header);
		break;
	case CEC_STATE_TRANSMITTING:
	case CEC_STATE_RECEIVING:
		/*
		 * It should be impossible to receive a header when we're
		 * already receiving or transmitting.
		 */
		CPRINTS("CEC%d error: HDRCV in invalid state %d", port,
			cec_state);
		enter_idle_state();
		break;
	}
}

static void cec_event_transmit(int port)
{
	switch (cec_state) {
	case CEC_STATE_DISABLED:
		break;
	case CEC_STATE_IDLE:
		/* Transmit */
		start_free_time();
		break;
	case CEC_STATE_FREE_TIME:
	case CEC_STATE_TRANSMITTING:
		/*
		 * Should be impossible since it83xx_cec_send() ensures we only
		 * have one transmission at a time.
		 */
		CPRINTS("CEC%d error: transmit event in invalid state %d", port,
			cec_state);
		enter_idle_state();
		break;
	case CEC_STATE_RECEIVING:
		/*
		 * Continue receiving. We'll start transmitting when the current
		 * receive finishes.
		 */
		break;
	}
}

static void cec_event_free_time_complete(int port)
{
	switch (cec_state) {
	case CEC_STATE_DISABLED:
		break;
	case CEC_STATE_IDLE:
		/* Should be impossible. Stay in idle state and reset. */
	case CEC_STATE_TRANSMITTING:
		/*
		 * Should be impossible since we only have one transmission at a
		 * time.
		 */
		CPRINTS("CEC%d error: free time complete in invalid state %d",
			port, cec_state);
		enter_idle_state();
		break;
	case CEC_STATE_FREE_TIME:
		/* Free time complete, so start transmitting */
		cec_state = CEC_STATE_TRANSMITTING;
		cec_tx.present_initiator = 1;

		/* Switch device to initiator mode */
		IT83XX_CEC_CECOPSTS |= IT83XX_CEC_CECOPSTS_DMS;

		/* Write the first byte */
		cec_tx.transfer.byte = 0;
		write_byte();

		/* Enable broadcast mode if broadcast, otherwise disable it */
		if ((cec_tx.transfer.buf[0] & 0xf) == CEC_BROADCAST_ADDR)
			IT83XX_CEC_CECOPSTS |= IT83XX_CEC_CECOPSTS_IBE;
		else
			IT83XX_CEC_CECOPSTS &= ~IT83XX_CEC_CECOPSTS_IBE;

		/* Set ICC (Issue CEC Cycle) to start the transmission */
		IT83XX_CEC_CECCTRL |= IT83XX_CEC_CECCTRL_ICC;
		break;
	case CEC_STATE_RECEIVING:
		/*
		 * Another device started transmitting during our free time.
		 * Continue receiving, and we'll try to transmit again when this
		 * receive completes.
		 */
		break;
	}
}

/* The CEC peripheral only supports one port */
static int get_port(void)
{
	int port;

	for (port = 0; port < CEC_PORT_COUNT; port++) {
		if (cec_config[port].drv == &it83xx_cec_drv)
			return port;
	}

	CPRINTS("CEC error: failed to find port using it83xx_cec_drv");
	return -1;
}

static void process_events(void)
{
	uint32_t events = atomic_clear(&cec_events);
	int port = get_port();

	if (port < 0)
		return;

	/* There are several types of error but we handle them all the same */
	if (events &
	    (CEC_EVENT_BTE | CEC_EVENT_BLE | CEC_EVENT_CEN | CEC_EVENT_CLE)) {
		CPRINTS("CEC%d error: events 0x%02x state %d", port, events,
			cec_state);
		cec_event_error(port);
	}

	if (events & CEC_EVENT_DBD)
		cec_event_dbd(port);

	if (events & CEC_EVENT_HDRCV)
		cec_event_hdrcv(port);

	if (events & CEC_EVENT_TRANSMIT)
		cec_event_transmit(port);

	if (events & CEC_EVENT_FREE_TIME_COMPLETE)
		cec_event_free_time_complete(port);
}
DECLARE_DEFERRED(process_events);

static void cec_set_events(uint32_t events)
{
	atomic_or(&cec_events, events);
	hook_call_deferred(&process_events_data, 0);
}

void cec_interrupt(void)
{
	uint8_t status = IT83XX_CEC_CECSTS;

	cec_set_events(status);

	/* Write 1 to clear */
	IT83XX_CEC_CECSTS = status;

	task_clear_pending_irq(IT83XX_IRQ_CEC);
}

static int it83xx_cec_init(int port)
{
	/* Initialisation is done when CEC is enabled */
	return EC_SUCCESS;
}

static int it83xx_cec_get_enable(int port, uint8_t *enable)
{
	*enable = cec_state == CEC_STATE_DISABLED ? 0 : 1;

	return EC_SUCCESS;
}

static int it83xx_cec_set_enable(int port, uint8_t enable)
{
	/* Enabling when already enabled? */
	if (enable && cec_state != CEC_STATE_DISABLED)
		return EC_SUCCESS;

	/* Disabling when already disabled? */
	if (!enable && cec_state == CEC_STATE_DISABLED)
		return EC_SUCCESS;

	if (enable) {
#ifndef CONFIG_ZEPHYR
		/* TODO: Implement these in zephyr */

		/* Enable CEC clock */
		clock_enable_peripheral(CGC_OFFSET_CEC, 0, 0);

		/* Set CECEN to select CEC alternate function */
		IT83XX_GPIO_GRC8 |= BIT(5);

		/* Enable alternate function */
		gpio_config_module(MODULE_CEC, 1);
#endif

		/* Set logical address to unregistered (default is 0 = TV) */
		IT83XX_CEC_CECDLA = CEC_UNREGISTERED_ADDR &
				    IT83XX_CEC_CECDLA_DLA;

		enter_idle_state();

		/* Enable all interrupts in interrupt enable register */
		IT83XX_CEC_CECIE |= CEC_ALL_INTERRUPTS;

		/* Enable CEC interrupt */
		task_clear_pending_irq(IT83XX_IRQ_CEC);
		task_enable_irq(IT83XX_IRQ_CEC);

		CPRINTS("CEC%d enabled", port);
	} else {
		/* Disable CEC interrupt */
		task_disable_irq(IT83XX_IRQ_CEC);
		task_clear_pending_irq(IT83XX_IRQ_CEC);

		/* Disable all interrupts in interrupt enable register */
		IT83XX_CEC_CECIE &= ~CEC_ALL_INTERRUPTS;

#ifndef CONFIG_ZEPHYR
		/* TODO: Implement these in zephyr */

		/* Configure pin back to GPIO */
		gpio_config_module(MODULE_CEC, 0);
		IT83XX_GPIO_GRC8 &= ~BIT(5);

		/* Disable CEC clock */
		clock_disable_peripheral(CGC_OFFSET_CEC, 0, 0);
#endif

		cec_state = CEC_STATE_DISABLED;

		cec_events = 0;
		memset(&cec_rx, 0, sizeof(cec_rx));
		memset(&cec_tx, 0, sizeof(cec_tx));

		CPRINTS("CEC%d disabled", port);
	}

	return EC_SUCCESS;
}

static int it83xx_cec_get_logical_addr(int port, uint8_t *logical_addr)
{
	*logical_addr = IT83XX_CEC_CECDLA & IT83XX_CEC_CECDLA_DLA;

	return EC_SUCCESS;
}

static int it83xx_cec_set_logical_addr(int port, uint8_t logical_addr)
{
	/* DLA field is only 4 bits */
	if (logical_addr == CEC_INVALID_ADDR)
		logical_addr = CEC_UNREGISTERED_ADDR;

	IT83XX_CEC_CECDLA = logical_addr & IT83XX_CEC_CECDLA_DLA;

	CPRINTS("CEC%d address set to: %u", port, logical_addr);

	return EC_SUCCESS;
}

static int it83xx_cec_send(int port, const uint8_t *msg, uint8_t len)
{
	char str_buf[hex_str_buf_size(len)];

	if (cec_state == CEC_STATE_DISABLED)
		return EC_ERROR_BUSY;

	if (cec_tx.len != 0)
		return EC_ERROR_BUSY;

	cec_tx.len = len;

	snprintf_hex_buffer(str_buf, sizeof(str_buf), HEX_BUF(msg, len));
	DEBUG_CPRINTS("CEC%d send: 0x%s", port, str_buf);

	memcpy(cec_tx.transfer.buf, msg, len);

	cec_set_events(CEC_EVENT_TRANSMIT);

	return EC_SUCCESS;
}

static int it83xx_cec_get_received_message(int port, uint8_t **msg,
					   uint8_t *len)
{
	if (!cec_rx.received_message_available)
		return EC_ERROR_UNAVAILABLE;

	*msg = cec_rx.received_message.buf;
	*len = cec_rx.received_message.byte;
	cec_rx.received_message_available = 0;

	return EC_SUCCESS;
}

const struct cec_drv it83xx_cec_drv = {
	.init = &it83xx_cec_init,
	.get_enable = &it83xx_cec_get_enable,
	.set_enable = &it83xx_cec_set_enable,
	.get_logical_addr = &it83xx_cec_get_logical_addr,
	.set_logical_addr = &it83xx_cec_set_logical_addr,
	.send = &it83xx_cec_send,
	.get_received_message = &it83xx_cec_get_received_message,
};

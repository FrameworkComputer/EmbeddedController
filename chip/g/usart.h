/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "consumer.h"
#include "producer.h"
#include "registers.h"
#include "task.h"

#ifndef __CROS_FORWARD_UART_H
#define __CROS_FORWARD_UART_H

struct usart_config {
	int uart;

	struct producer const producer;
	struct consumer const consumer;

	const struct deferred_data *deferred;
};

extern struct consumer_ops const uart_consumer_ops;
extern struct producer_ops const uart_producer_ops;
#define CONFIGURE_INTERRUPTS(NAME,					\
			     RXINT,					\
			     TXINT)					\
	void CONCAT2(NAME, _rx_int_)(void);				\
	void CONCAT2(NAME, _tx_int_)(void);				\
	DECLARE_IRQ(RXINT, CONCAT2(NAME, _rx_int_), 1);			\
	DECLARE_IRQ(TXINT, CONCAT2(NAME, _tx_int_), 1);			\
	void CONCAT2(NAME, _tx_int_)(void)				\
	{								\
		/* Clear transmit interrupt status */			\
		GR_UART_ISTATECLR(NAME.uart) =				\
			GC_UART_ISTATECLR_TX_MASK;			\
		/* Fill output FIFO */					\
		get_data_from_usb(&NAME);				\
	}								\
	void CONCAT2(NAME, _rx_int_)(void)				\
	{								\
		/* Clear receive interrupt status */			\
		GR_UART_ISTATECLR(NAME.uart) =				\
			GC_UART_ISTATECLR_RX_MASK;			\
		/* Read input FIFO until empty */			\
		hook_call_deferred(NAME.deferred, 0);			\
	}


#define USART_CONFIG(NAME,						\
		     UART,						\
		     RX_QUEUE,						\
		     TX_QUEUE)						\
	static void CONCAT2(NAME, _deferred_)(void);			\
	DECLARE_DEFERRED(CONCAT2(NAME, _deferred_));			\
	struct usart_config const NAME = {				\
		.uart      = UART,					\
		.consumer  = {						\
			.queue = &TX_QUEUE,				\
			.ops   = &uart_consumer_ops,			\
		},							\
		.producer  = {						\
			.queue = &RX_QUEUE,				\
			.ops   = &uart_producer_ops,			\
		},							\
		.deferred  = &CONCAT2(NAME, _deferred__data),		\
	};								\
	static void CONCAT2(NAME, _deferred_)(void)			\
	{								\
		send_data_to_usb(&NAME);				\
	}								\


/* Read data from UART and add it to the producer queue */
void send_data_to_usb(struct usart_config const *config);

/* Read data from the consumer queue and send it to the UART */
void get_data_from_usb(struct usart_config const *config);
#endif  /* __CROS_FORWARD_UART_H */

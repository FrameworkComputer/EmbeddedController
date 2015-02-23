/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef CHIP_STM32_USART_H
#define CHIP_STM32_USART_H

/* STM32 USART driver for Chrome EC */

#include "common.h"
#include "consumer.h"
#include "in_stream.h"
#include "out_stream.h"
#include "producer.h"
#include "queue.h"

#include <stdint.h>

/*
 * Per-USART state stored in RAM.  This structure will be zero initialized by
 * BSS init.
 */
struct usart_state {
	/*
	 * Counter of bytes receieved and then dropped because of lack of space
	 * in the RX queue.
	 */
	uint32_t rx_dropped;
};

struct usart_config;

struct usart_hw_ops {
	/*
	 * The generic USART initialization code calls this function to allow
	 * the variant HW specific code to perform any initialization.  This
	 * function is called before the USART is enabled, and should among
	 * other things enable the USARTs interrupt.
	 */
	void (*enable)(struct usart_config const *config);

	/*
	 * The generic USART shutdown code calls this function, allowing the
	 * variant specific code an opportunity to do any variant specific
	 * shutdown tasks.
	 */
	void (*disable)(struct usart_config const *config);
};

/*
 * Per-USART hardware configuration stored in flash.  Instances of this
 * structure are provided by each variants driver, one per physical USART.
 */
struct usart_hw_config {
	int      index;
	intptr_t base;
	int      irq;

	uint32_t volatile *clock_register;
	uint32_t           clock_enable;

	struct usart_hw_ops const *ops;
};

/*
 * Compile time Per-USART configuration stored in flash.  Instances of this
 * structure are provided by the user of the USART.  This structure binds
 * together all information required to operate a USART.
 */
struct usart_config {
	/*
	 * Pointer to USART HW configuration.  There is one HW configuration
	 * per physical USART.
	 */
	struct usart_hw_config const *hw;

	/*
	 * Pointer to USART state structure.  The state structure maintains per
	 * USART information.
	 */
	struct usart_state volatile *state;

	/*
	 * Baud rate for USART.
	 */
	int baud;

	struct consumer consumer;
	struct producer producer;
};

/*
 * These function tables are defined by the USART driver and are used to
 * initialize the consumer and producer in the usart_config.
 */
extern struct consumer_ops const usart_consumer_ops;
extern struct producer_ops const usart_producer_ops;

/*
 * Convenience macro for defining USARTs and their associated state and buffers.
 * NAME is used to construct the names of the usart_state struct, and
 * usart_config struct, the latter is just called NAME.
 *
 * HW is the name of the usart_hw_config provided by the variant specific code.
 *
 * RX_QUEUE and TX_QUEUE are the names of the RX and TX queues that this USART
 * should write to and read from respectively.  They must match the queues
 * that the CONSUMER and PRODUCER read from and write to respectively.
 *
 * CONSUMER and PRODUCER are the names of the consumer and producer objects at
 * the other ends of the RX and TX queues respectively.
 */
/*
 * The following assertions can not be made because they require access to
 * non-const fields, but should be kept in mind.
 *
 * BUILD_ASSERT(RX_QUEUE.unit_bytes == 1);
 * BUILD_ASSERT(TX_QUEUE.unit_bytes == 1);
 * BUILD_ASSERT(PRODUCER.queue == &TX_QUEUE);
 * BUILD_ASSERT(CONSUMER.queue == &RX_QUEUE);
 */
#define USART_CONFIG(NAME,					\
		     HW,					\
		     BAUD,					\
		     RX_QUEUE,					\
		     TX_QUEUE,					\
		     CONSUMER,					\
		     PRODUCER)					\
								\
	static struct usart_state CONCAT2(NAME, _state);	\
	struct usart_config const NAME = {			\
		.hw       = &HW,				\
		.state    = &CONCAT2(NAME, _state),		\
		.baud     = BAUD,				\
		.consumer = {					\
			.producer = &PRODUCER,			\
			.queue    = &TX_QUEUE,			\
			.ops      = &usart_consumer_ops,	\
		},						\
		.producer = {					\
			.consumer = &CONSUMER,			\
			.queue    = &RX_QUEUE,			\
			.ops      = &usart_producer_ops,	\
		},						\
	};

/*
 * Initialize the given USART.  Once init is finished the USART streams are
 * available for operating on.
 */
void usart_init(struct usart_config const *config);

/*
 * Shutdown the given USART.
 */
void usart_shutdown(struct usart_config const *config);

/*
 * Handle a USART interrupt.  The per-variant USART code creates bindings
 * for the variants interrupts to call this generic USART interrupt handler
 * with the appropriate usart_config.
 */
void usart_interrupt(struct usart_config const *config);

/*
 * These are HW specific baud rate calculation and setting functions that the
 * peripheral variant code uses during initialization and clock frequency
 * change.  The baud rate divisor input frequency is passed in Hertz.
 */
void usart_set_baud_f0_l(struct usart_config const *config, int frequency_hz);
void usart_set_baud_f(struct usart_config const *config, int frequency_hz);

#endif /* CHIP_STM32_USART_H */

/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef CHIP_STM32_USART_H
#define CHIP_STM32_USART_H

/* STM32 USART driver for Chrome EC */

#include "common.h"
#include "in_stream.h"
#include "out_stream.h"
#include "queue.h"

#include <stdint.h>

/*
 * Per-USART state stored in RAM.  This structure will be zero initialized by
 * BSS init.  Most importantly, irq_lock will be zero, ensuring that shared
 * interrupts don't cause problems.
 */
struct usart_state {
	struct queue_state rx;
	struct queue_state tx;

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
	 * USART information (head and tail pointers for the queues for
	 * instance).
	 */
	struct usart_state volatile *state;

	/*
	 * Baud rate for USART.
	 */
	int baud;

	/*
	 * TX and RX queue configs.  The state for the queue is stored
	 * separately in the usart_state structure.
	 */
	struct queue rx;
	struct queue tx;

	/*
	 * In and Out streams, these contain pointers to the virtual function
	 * tables that implement in and out streams.  They can be used by any
	 * code that wants to read or write to a stream interface.
	 */
	struct in_stream  in;
	struct out_stream out;
};

/*
 * These function tables are defined by the USART driver and are used to
 * initialize the in and out streams in the usart_config.
 */
extern struct in_stream_ops const usart_in_stream_ops;
extern struct out_stream_ops const usart_out_stream_ops;

/*
 * Convenience macro for defining USARTs and their associated state and buffers.
 * NAME is used to construct the names of the queue buffers, usart_state struct,
 * and usart_config struct, the latter is just called NAME.  RX_SIZE and TX_SIZE
 * are the size in bytes of the RX and TX buffers respectively.  RX_READY and
 * TX_READY are the callback functions for the in and out streams.  The USART
 * baud rate is specified by the BAUD parameter.
 *
 * If you want to share a queue with other code, you can manually initialize a
 * usart_config to use the shared queue, or you can use this macro and then get
 * the queue buffers as <NAME>_tx_buffer, and <NAME>_rx_buffer.
 */
#define USART_CONFIG(NAME,						\
		     HW,						\
		     BAUD,						\
		     RX_SIZE,						\
		     TX_SIZE,						\
		     RX_READY,						\
		     TX_READY)						\
	static uint8_t CONCAT2(NAME, _tx_buffer)[TX_SIZE];		\
	static uint8_t CONCAT2(NAME, _rx_buffer)[RX_SIZE];		\
									\
	static struct usart_state CONCAT2(NAME, _state);		\
	struct usart_config const NAME = {				\
		.hw    = &HW,						\
		.state = &CONCAT2(NAME, _state),			\
		.baud  = BAUD,						\
		.rx = {							\
			.state        = &CONCAT2(NAME, _state.rx),	\
			.buffer_units = RX_SIZE,			\
			.unit_bytes   = 1,				\
			.buffer       = CONCAT2(NAME, _rx_buffer),	\
		},							\
		.tx = {							\
			.state        = &CONCAT2(NAME, _state.tx),	\
			.buffer_units = TX_SIZE,			\
			.unit_bytes   = 1,				\
			.buffer       = CONCAT2(NAME, _tx_buffer),	\
		},							\
		.in  = {						\
			.ready = RX_READY,				\
			.ops   = &usart_in_stream_ops,			\
		},							\
		.out = {						\
			.ready = TX_READY,				\
			.ops   = &usart_out_stream_ops,			\
		},							\
	};

/*
 * Initialize the given USART.  Once init is finished the USART streams are
 * available for operating on, and the stream ready callbacks could be called
 * at any time.
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
 *
 * This function could also be called manually to poll the USART hardware.
 */
void usart_interrupt(struct usart_config const *config);

/*
 * These are HW specific baud rate calculation and setting functions that the
 * peripheral variant code uses during initialization and clock frequency
 * change.
 */
void usart_set_baud_f0_l(struct usart_config const *config);
void usart_set_baud_f(struct usart_config const *config);

#endif /* CHIP_STM32_USART_H */

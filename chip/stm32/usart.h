/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_USART_H
#define __CROS_EC_USART_H

/* STM32 USART driver for Chrome EC */

#include "common.h"
#include "consumer.h"
#include "producer.h"
#include "queue.h"

#include <stdint.h>

/*
 * Per-USART state stored in RAM.  This structure will be zero initialized by
 * BSS init.
 */
struct usart_state {
	/*
	 * Counter of bytes received and then dropped because of lack of space
	 * in the RX queue.
	 */
	uint32_t rx_dropped;

	/*
	 * Counter of the number of times an receive overrun condition is
	 * detected.  This will not usually be a count of the number of bytes
	 * that were lost due to overrun conditions.
	 */
	uint32_t rx_overrun;
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
 * The usart_rx/usart_tx structures contain functions pointers for the
 * interrupt handler and producer/consumer operations required to implement a
 * particular RX/TX strategy.
 *
 * These structures are defined by the various RX/TX implementations, and are
 * used to initialize the usart_config structure to configure the USART driver
 * for interrupt or DMA based transfer.
 */
struct usart_rx {
	void (*init)(struct usart_config const *config);
	void (*interrupt)(struct usart_config const *config);

	/*
	 * Print to the console any per-strategy diagnostic information, this
	 * is used by the usart_info command.  This can be NULL if there is
	 * nothing interesting to display.
	 */
	void (*info)(struct usart_config const *config);

	struct producer_ops producer_ops;
};

struct usart_tx {
	void (*init)(struct usart_config const *config);
	void (*interrupt)(struct usart_config const *config);

	/*
	 * Print to the console any per-strategy diagnostic information, this
	 * is used by the usart_info command.  This can be NULL if there is
	 * nothing interesting to display.
	 */
	void (*info)(struct usart_config const *config);

	struct consumer_ops consumer_ops;
};

extern struct usart_rx const usart_rx_interrupt;
extern struct usart_tx const usart_tx_interrupt;

/*
 * Per-USART hardware configuration stored in flash.  Instances of this
 * structure are provided by each variants driver, one per physical USART.
 */
struct usart_hw_config {
	int index;
	intptr_t base;
	int irq;

	uint32_t volatile *clock_register;
	uint32_t clock_enable;

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

	struct usart_rx const *rx;
	struct usart_tx const *tx;

	/*
	 * Pointer to USART state structure.  The state structure maintains per
	 * USART information.
	 */
	struct usart_state volatile *state;

	/*
	 * Baud rate for USART.
	 */
	int baud;

	/* Other flags (rx/tx inversion, half-duplex). */
#define USART_CONFIG_FLAG_RX_INV BIT(0)
#define USART_CONFIG_FLAG_TX_INV BIT(1)
#define USART_CONFIG_FLAG_HDSEL BIT(2)
	unsigned int flags;

	struct consumer consumer;
	struct producer producer;
};

/*
 * Convenience macro for defining USARTs and their associated state and buffers.
 * NAME is used to construct the names of the usart_state struct, and
 * usart_config struct, the latter is just called NAME.
 *
 * HW is the name of the usart_hw_config provided by the variant specific code.
 *
 * RX_QUEUE and TX_QUEUE are the names of the RX and TX queues that this USART
 * should write to and read from respectively.
 */
/*
 * The following assertions can not be made because they require access to
 * non-const fields, but should be kept in mind.
 *
 * BUILD_ASSERT(RX_QUEUE.unit_bytes == 1);
 * BUILD_ASSERT(TX_QUEUE.unit_bytes == 1);
 */
#define USART_CONFIG(HW, RX, TX, BAUD, FLAGS, RX_QUEUE, TX_QUEUE) \
	((struct usart_config const) {					\
		.hw       = &HW,					\
		.rx       = &RX,					\
		.tx       = &TX,					\
		.state    = &((struct usart_state){}),			\
		.baud     = BAUD,					\
		.flags    = FLAGS,					\
		.consumer = {						\
			.queue = &TX_QUEUE,				\
			.ops   = &TX.consumer_ops,			\
		},							\
		.producer = {						\
			.queue = &RX_QUEUE,				\
			.ops   = &RX.producer_ops,			\
		},							\
	})

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
 * Trigger tx interrupt to process tx data. Calling this function will set
 * TXIEIE of USART HW instance and trigger associated IRQ.
 */
void usart_tx_start(struct usart_config const *config);

/*
 * These are HW specific baud rate calculation and setting functions that the
 * peripheral variant code uses during initialization and clock frequency
 * change.  The baud rate divisor input frequency is passed in Hertz.
 */
void usart_set_baud_f0_l(struct usart_config const *config, int baud,
			 int frequency_hz);
void usart_set_baud_f(struct usart_config const *config, int baud,
		      int frequency_hz);
int usart_get_baud_f0_l(struct usart_config const *config, int frequency_hz);

/*
 * Allow specification of parity for this usart.
 * parity is 0: none, 1: odd, 2: even.
 */
void usart_set_parity(struct usart_config const *config, int parity);

/*
 * Check parity for this usart.
 * parity is 0: none, 1: odd, 2: even.
 */
int usart_get_parity(struct usart_config const *config);

/*
 * Set baud rate for this usart. Note that baud rate will get reset on
 * core frequency change, so this only makes sense if the board never
 * goes to deep idle.
 */
void usart_set_baud(struct usart_config const *config, int baud);

/*
 * Get the current baud rate for this uart.
 */
int usart_get_baud(struct usart_config const *config);

/*
 * Start or end "break condition" on the TX line of this uart.
 */
void usart_set_break(struct usart_config const *config, bool enable);

enum clear_which_fifo {
	CLEAR_RX_FIFO = 0x01,
	CLEAR_TX_FIFO = 0x02,
	CLEAR_BOTH_FIFOS = 0x03,
};

/*
 * For the families that support UART FIFO, this method will clear inbound
 * and/or outbound FIFO, discarding any characters.
 */
void usart_clear_fifos(struct usart_config const *config,
		       enum clear_which_fifo);

/*
 * Different families provide different ways of clearing the transmit complete
 * flag.  This function will be provided by the family specific implementation.
 */
void usart_clear_tc(struct usart_config const *config);

/*
 * Each family implementation provides the usart_get_configs function to access
 * a read only list of the configs that are currently enabled.
 */
struct usart_configs {
	/*
	 * The family's usart_config array, entries in the array for disabled
	 * configs will be NULL, enabled configs will point to the usart_config
	 * that was enabled.  And the following will be true:
	 *
	 * configs[i]->hw->index == i;
	 */
	struct usart_config const *const *configs;

	/*
	 * The total possible number of configs that this family supports.
	 * This will be the same as the number of usart_hw structs that the
	 * family provides in its family specific usart header.
	 */
	size_t count;
};

struct usart_configs usart_get_configs(void);

/*
 * This usart_tx structure contains function pointer to interrupt
 * handler implemented to send host response. Generic queue based
 * interrupt handler is not used for usart host transport.
 */
extern struct usart_tx const usart_host_command_tx_interrupt;

#endif /* __CROS_EC_USART_H */

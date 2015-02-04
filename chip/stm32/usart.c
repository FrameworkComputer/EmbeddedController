/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USART driver for Chrome EC */

#include "atomic.h"
#include "clock.h"
#include "common.h"
#include "gpio.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "usart.h"
#include "util.h"

static size_t usart_read(struct in_stream const *stream,
			 uint8_t *buffer,
			 size_t count)
{
	struct usart_config const *config =
		DOWNCAST(stream, struct usart_config, in);

	return QUEUE_REMOVE_UNITS(&config->rx, buffer, count);
}

static size_t usart_write(struct out_stream const *stream,
			  uint8_t const *buffer,
			  size_t count)
{
	struct usart_config const *config =
		DOWNCAST(stream, struct usart_config, out);

	size_t wrote = QUEUE_ADD_UNITS(&config->tx, buffer, count);

	/*
	 * Enable USART interrupt.  This causes the USART interrupt handler to
	 * start fetching from the TX queue if it wasn't already.
	 */
	if (wrote)
		STM32_USART_CR1(config->hw->base) |= STM32_USART_CR1_TXEIE;

	return wrote;
}

static void usart_flush(struct out_stream const *stream)
{
	struct usart_config const *config =
		DOWNCAST(stream, struct usart_config, out);

	while (queue_count(&config->tx))
		;
}

struct in_stream_ops const usart_in_stream_ops = {
	.read = usart_read,
};

struct out_stream_ops const usart_out_stream_ops = {
	.write = usart_write,
	.flush = usart_flush,
};

void usart_init(struct usart_config const *config)
{
	intptr_t base = config->hw->base;

	queue_init(&config->tx);
	queue_init(&config->rx);

	/*
	 * Enable clock to USART, this must be done first, before attempting
	 * to configure the USART.
	 */
	*(config->hw->clock_register) |= config->hw->clock_enable;

	/*
	 * For STM32F3, A delay of 1 APB clock cycles is needed before we
	 * can access any USART register. Fortunately, we have
	 * gpio_config_module() below and thus don't need to add the delay.
	 */

	/*
	 * Switch all GPIOs assigned to the USART module over to their USART
	 * alternate functions.
	 */
	gpio_config_module(MODULE_USART, 1);

	/*
	 * 8N1, 16 samples per bit, enable TX and RX (and associated RX
	 * interrupt) DMA, error interrupts, and special modes disabled.
	 */
	STM32_USART_CR1(base) = (STM32_USART_CR1_TE |
				 STM32_USART_CR1_RE |
				 STM32_USART_CR1_RXNEIE);
	STM32_USART_CR2(base) = 0x0000;
	STM32_USART_CR3(base) = STM32_USART_CR3_OVRDIS;

	/*
	 * Enable the variant specific HW.
	 */
	config->hw->ops->enable(config);

	/*
	 * Enable the USART, this must be done last since most of the
	 * configuration bits require that the USART be disabled for writes to
	 * succeed.
	 */
	STM32_USART_CR1(base) |= STM32_USART_CR1_UE;
}

void usart_shutdown(struct usart_config const *config)
{
	STM32_USART_CR1(config->hw->base) &= ~STM32_USART_CR1_UE;

	config->hw->ops->disable(config);
}

void usart_set_baud_f0_l(struct usart_config const *config)
{
	int      div  = DIV_ROUND_NEAREST(clock_get_freq(), config->baud);
	intptr_t base = config->hw->base;

	if (div / 16 > 0) {
		/*
		 * CPU clock is high enough to support x16 oversampling.
		 * BRR = (div mantissa)<<4 | (4-bit div fraction)
		 */
		STM32_USART_CR1(base) &= ~STM32_USART_CR1_OVER8;
		STM32_USART_BRR(base) = div;
	} else {
		/*
		 * CPU clock is low; use x8 oversampling.
		 * BRR = (div mantissa)<<4 | (3-bit div fraction)
		 */
		STM32_USART_BRR(base) = ((div / 8) << 4) | (div & 7);
		STM32_USART_CR1(base) |= STM32_USART_CR1_OVER8;
	}
}

void usart_set_baud_f(struct usart_config const *config)
{
	int div = DIV_ROUND_NEAREST(clock_get_freq(), config->baud);

	/* STM32F only supports x16 oversampling */
	STM32_USART_BRR(config->hw->base) = div;
}

static void usart_interrupt_tx(struct usart_config const *config)
{
	intptr_t base = config->hw->base;
	uint8_t  byte;

	if (queue_remove_unit(&config->tx, &byte)) {
		STM32_USART_TDR(base) = byte;

		out_stream_ready(&config->out);

		/*
		 * Make sure the TXE interrupt is enabled and that we won't go
		 * into deep sleep.  This invocation of the USART interrupt
		 * handler may have been manually triggered to start
		 * transmission.
		 */
		disable_sleep(SLEEP_MASK_UART);

		STM32_USART_CR1(base) |= STM32_USART_CR1_TXEIE;
	} else {
		/*
		 * The TX queue is empty, disable the TXE interrupt and enable
		 * deep sleep mode. The TXE interrupt will remain disabled
		 * until a write call happens.
		 */
		enable_sleep(SLEEP_MASK_UART);

		STM32_USART_CR1(base) &= ~STM32_USART_CR1_TXEIE;
	}
}

static void usart_interrupt_rx(struct usart_config const *config)
{
	intptr_t base    = config->hw->base;
	uint8_t  byte    = STM32_USART_RDR(base);
	uint32_t dropped = 1 - queue_add_unit(&config->rx, &byte);

	atomic_add((uint32_t *) &config->state->rx_dropped, dropped);

	/*
	 * Wake up whoever is listening on the other end of the queue.  The
	 * queue_add_units call above may have failed due to a full queue, but
	 * it doesn't really matter to the ready callback because there will be
	 * something in the queue to consume either way.
	 */
	in_stream_ready(&config->in);
}

void usart_interrupt(struct usart_config const *config)
{
	intptr_t base = config->hw->base;

	if (STM32_USART_SR(base) & STM32_USART_SR_TXE)
		usart_interrupt_tx(config);

	if (STM32_USART_SR(base) & STM32_USART_SR_RXNE)
		usart_interrupt_rx(config);
}

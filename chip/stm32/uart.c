/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USART driver for Chrome EC */

#include "common.h"
#include "clock.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "uart.h"
#include "util.h"

/* Console USART index */
#define UARTN CONFIG_UART_CONSOLE

static int init_done;    /* Initialization done? */
static int should_stop;  /* Last TX control action */

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_start(void)
{
	disable_sleep(SLEEP_MASK_UART);
	STM32_USART_CR1(UARTN) |= STM32_USART_CR1_TXEIE;
	should_stop = 0;
	task_trigger_irq(STM32_IRQ_USART(UARTN));
}

void uart_tx_stop(void)
{
	STM32_USART_CR1(UARTN) &= ~STM32_USART_CR1_TXEIE;
	should_stop = 1;
	enable_sleep(SLEEP_MASK_UART);
}

int uart_tx_stopped(void)
{
	return !(STM32_USART_CR1(UARTN) & STM32_USART_CR1_TXEIE);
}

void uart_tx_flush(void)
{
	while (!(STM32_USART_SR(UARTN) & STM32_USART_SR_TXE))
		;
}

int uart_tx_ready(void)
{
	return STM32_USART_SR(UARTN) & STM32_USART_SR_TXE;
}

int uart_rx_available(void)
{
	return STM32_USART_SR(UARTN) & STM32_USART_SR_RXNE;
}

void uart_write_char(char c)
{
	/* we normally never wait here since uart_write_char is normally called
	 * when the buffer is ready, excepted when we insert a carriage return
	 * before a line feed in the interrupt routine.
	 */
	while (!uart_tx_ready()) ;
	STM32_USART_DR(UARTN) = c;
}

int uart_read_char(void)
{
	return STM32_USART_DR(UARTN);
}

void uart_disable_interrupt(void)
{
	task_disable_irq(STM32_IRQ_USART(UARTN));
}

void uart_enable_interrupt(void)
{
	task_enable_irq(STM32_IRQ_USART(UARTN));
}

/* Interrupt handler for console USART */
static void uart_interrupt(void)
{
	/*
	 * Disable the TX empty interrupt before filling the TX buffer since it
	 * needs an actual write to DR to be cleared.
	 */
	STM32_USART_CR1(UARTN) &= ~STM32_USART_CR1_TXEIE;

	/* Read input FIFO until empty, then fill output FIFO */
	uart_process_input();
	uart_process_output();

	/*
	 * Re-enable TX empty interrupt only if it was not disabled by
	 * uart_process.
	 */
	if (!should_stop)
		STM32_USART_CR1(UARTN) |= STM32_USART_CR1_TXEIE;
}
DECLARE_IRQ(STM32_IRQ_USART(UARTN), uart_interrupt, 2);

/**
 * Handle clock frequency changes
 */
static void uart_freq_change(void)
{
	int div = DIV_ROUND_NEAREST(clock_get_freq(), CONFIG_UART_BAUD_RATE);

#ifdef CHIP_FAMILY_stm32l
	if (div / 16 > 0) {
		/*
		 * CPU clock is high enough to support x16 oversampling.
		 * BRR = (div mantissa)<<4 | (4-bit div fraction)
		 */
		STM32_USART_CR1(UARTN) &= ~STM32_USART_CR1_OVER8;
		STM32_USART_BRR(UARTN) = div;
	} else {
		/*
		 * CPU clock is low; use x8 oversampling.
		 * BRR = (div mantissa)<<4 | (3-bit div fraction)
		 */
		STM32_USART_BRR(UARTN) = ((div / 8) << 4) | (div & 7);
		STM32_USART_CR1(UARTN) |= STM32_USART_CR1_OVER8;
	}
#else
	/* STM32F only supports x16 oversampling */
	STM32_USART_BRR(UARTN) = div;
#endif

}
DECLARE_HOOK(HOOK_FREQ_CHANGE, uart_freq_change, HOOK_PRIO_DEFAULT);

void uart_init(void)
{
	/* Enable USART clock */
#if (UARTN == 1)
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_USART1;
#else
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_USART ## UARTN;
#endif

	/* Configure GPIOs */
	gpio_config_module(MODULE_UART, 1);

	/*
	 * UART enabled, 8 Data bits, oversampling x16, no parity,
	 * RXNE interrupt, TX and RX enabled.
	 */
	STM32_USART_CR1(UARTN) =
		STM32_USART_CR1_UE | STM32_USART_CR1_RXNEIE |
		STM32_USART_CR1_TE | STM32_USART_CR1_RE;

	/* 1 stop bit, no fancy stuff */
	STM32_USART_CR2(UARTN) = 0x0000;

	/* DMA disabled, special modes disabled, error interrupt disabled */
	STM32_USART_CR3(UARTN) = 0x0000;

#ifdef CHIP_FAMILY_stm32l
	/* Use single-bit sampling */
	STM32_USART_CR3(UARTN) |= STM32_USART_CR3_ONEBIT;
#endif

	/* Set initial baud rate */
	uart_freq_change();

	/* Enable interrupts */
	task_enable_irq(STM32_IRQ_USART(UARTN));

	init_done = 1;
}

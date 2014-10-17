/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USART driver for Chrome EC */

#include "common.h"
#include "clock.h"
#include "dma.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

/* Console USART index */
#define UARTN      CONFIG_UART_CONSOLE
#define UARTN_BASE STM32_USART_BASE(CONFIG_UART_CONSOLE)

#ifdef CONFIG_UART_TX_DMA
#define UART_TX_INT_ENABLE STM32_USART_CR1_TCIE

#ifndef CONFIG_UART_TX_DMA_CH
#define CONFIG_UART_TX_DMA_CH STM32_DMAC_USART1_TX
#endif

/* DMA channel options; assumes UART1 */
static const struct dma_option dma_tx_option = {
	CONFIG_UART_TX_DMA_CH, (void *)&STM32_USART_TDR(UARTN_BASE),
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
};

#else
#define UART_TX_INT_ENABLE STM32_USART_CR1_TXEIE
#endif

#ifdef CONFIG_UART_RX_DMA

#ifndef CONFIG_UART_RX_DMA_CH
#define CONFIG_UART_RX_DMA_CH STM32_DMAC_USART1_RX
#endif
/* DMA channel options; assumes UART1 */
static const struct dma_option dma_rx_option = {
	CONFIG_UART_RX_DMA_CH, (void *)&STM32_USART_RDR(UARTN_BASE),
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT |
	STM32_DMA_CCR_CIRC
};

static int dma_rx_len;   /* Size of receive DMA circular buffer */
#endif

static int init_done;    /* Initialization done? */
static int should_stop;  /* Last TX control action */

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_start(void)
{
	/* If interrupt is already enabled, nothing to do */
	if (STM32_USART_CR1(UARTN_BASE) & UART_TX_INT_ENABLE)
		return;

	disable_sleep(SLEEP_MASK_UART);
	should_stop = 0;
	STM32_USART_CR1(UARTN_BASE) |= UART_TX_INT_ENABLE;
	task_trigger_irq(STM32_IRQ_USART(UARTN));
}

void uart_tx_stop(void)
{
	STM32_USART_CR1(UARTN_BASE) &= ~UART_TX_INT_ENABLE;
	should_stop = 1;
	enable_sleep(SLEEP_MASK_UART);
}

void uart_tx_flush(void)
{
	while (!(STM32_USART_SR(UARTN_BASE) & STM32_USART_SR_TXE))
		;
}

int uart_tx_ready(void)
{
	return STM32_USART_SR(UARTN_BASE) & STM32_USART_SR_TXE;
}

#ifdef CONFIG_UART_TX_DMA

int uart_tx_dma_ready(void)
{
	return STM32_USART_SR(UARTN_BASE) & STM32_USART_SR_TC;
}

void uart_tx_dma_start(const char *src, int len)
{
	/* Prepare DMA */
	dma_prepare_tx(&dma_tx_option, len, src);

	/* Force clear TC so we don't re-interrupt */
	STM32_USART_SR(UARTN_BASE) &= ~STM32_USART_SR_TC;

	/* Enable TCIE (chrome-os-partner:28837) */
	STM32_USART_CR1(UARTN_BASE) |= STM32_USART_CR1_TCIE;

	/* Start DMA */
	dma_go(dma_get_channel(dma_tx_option.channel));
}

#endif /* CONFIG_UART_TX_DMA */

int uart_rx_available(void)
{
	return STM32_USART_SR(UARTN_BASE) & STM32_USART_SR_RXNE;
}

#ifdef CONFIG_UART_RX_DMA

void uart_rx_dma_start(char *dest, int len)
{
	/* Start receiving */
	dma_rx_len = len;
	dma_start_rx(&dma_rx_option, len, dest);
}

int uart_rx_dma_head(void)
{
	return dma_bytes_done(dma_get_channel(STM32_DMAC_USART1_RX),
			      dma_rx_len);
}

#endif

void uart_write_char(char c)
{
	/* Wait for space */
	while (!uart_tx_ready())
		;

	STM32_USART_TDR(UARTN_BASE) = c;
}

int uart_read_char(void)
{
	return STM32_USART_RDR(UARTN_BASE);
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
void uart_interrupt(void)
{
#ifdef CONFIG_UART_TX_DMA
	/* Disable transmission complete interrupt if DMA done */
	if (STM32_USART_SR(UARTN_BASE) & STM32_USART_SR_TC)
		STM32_USART_CR1(UARTN_BASE) &= ~STM32_USART_CR1_TCIE;
#else
	/*
	 * Disable the TX empty interrupt before filling the TX buffer since it
	 * needs an actual write to DR to be cleared.
	 */
	STM32_USART_CR1(UARTN_BASE) &= ~STM32_USART_CR1_TXEIE;
#endif

#ifndef CONFIG_UART_RX_DMA
	/*
	 * Read input FIFO until empty.  DMA-based receive does this from a
	 * hook in the UART buffering module.
	 */
	uart_process_input();
#endif

	/* Fill output FIFO */
	uart_process_output();

#ifndef CONFIG_UART_TX_DMA
	/*
	 * Re-enable TX empty interrupt only if it was not disabled by
	 * uart_process_output().
	 */
	if (!should_stop)
		STM32_USART_CR1(UARTN_BASE) |= STM32_USART_CR1_TXEIE;
#endif
}
DECLARE_IRQ(STM32_IRQ_USART(UARTN), uart_interrupt, 2);

/**
 * Handle clock frequency changes
 */
static void uart_freq_change(void)
{
	int freq;
	int div;

#if (defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)) && \
	(UARTN <= 2)
	/*
	 * UART is clocked from HSI (8MHz) to allow it to work when waking
	 * up from sleep
	 */
	freq = 8000000;
#else
	/* UART clocked from the main clock */
	freq = clock_get_freq();
#endif
	div = DIV_ROUND_NEAREST(freq, CONFIG_UART_BAUD_RATE);

#if defined(CHIP_FAMILY_STM32L) || defined(CHIP_FAMILY_STM32F0) || \
	defined(CHIP_FAMILY_STM32F3)
	if (div / 16 > 0) {
		/*
		 * CPU clock is high enough to support x16 oversampling.
		 * BRR = (div mantissa)<<4 | (4-bit div fraction)
		 */
		STM32_USART_CR1(UARTN_BASE) &= ~STM32_USART_CR1_OVER8;
		STM32_USART_BRR(UARTN_BASE) = div;
	} else {
		/*
		 * CPU clock is low; use x8 oversampling.
		 * BRR = (div mantissa)<<4 | (3-bit div fraction)
		 */
		STM32_USART_BRR(UARTN_BASE) = ((div / 8) << 4) | (div & 7);
		STM32_USART_CR1(UARTN_BASE) |= STM32_USART_CR1_OVER8;
	}
#else
	/* STM32F only supports x16 oversampling */
	STM32_USART_BRR(UARTN_BASE) = div;
#endif

}
DECLARE_HOOK(HOOK_FREQ_CHANGE, uart_freq_change, HOOK_PRIO_DEFAULT);

void uart_init(void)
{
	/* Enable USART clock */
#if defined(CHIP_FAMILY_STM32F0) || defined(CHIP_FAMILY_STM32F3)
#if (UARTN == 1)
	STM32_RCC_CFGR3 |= 0x0003;   /* USART1 clock source from HSI(8MHz) */
#elif (UARTN == 2)
	STM32_RCC_CFGR3 |= 0x030000; /* USART2 clock source from HSI(8MHz) */
#endif /* UARTN */
#endif /* CHIP_FAMILY_STM32F0 || CHIP_FAMILY_STM32F3 */

#if (UARTN == 1)
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_USART1;
#else
	STM32_RCC_APB1ENR |= CONCAT2(STM32_RCC_PB1_USART, UARTN);
#endif

	/* Configure GPIOs */
	gpio_config_module(MODULE_UART, 1);

	/*
	 * UART enabled, 8 Data bits, oversampling x16, no parity,
	 * TX and RX enabled.
	 */
	STM32_USART_CR1(UARTN_BASE) =
		STM32_USART_CR1_UE | STM32_USART_CR1_TE | STM32_USART_CR1_RE;

	/* 1 stop bit, no fancy stuff */
	STM32_USART_CR2(UARTN_BASE) = 0x0000;

#ifdef CONFIG_UART_TX_DMA
	/* Enable DMA transmitter */
	STM32_USART_CR3(UARTN_BASE) |= STM32_USART_CR3_DMAT;
#else
	/* DMA disabled, special modes disabled, error interrupt disabled */
	STM32_USART_CR3(UARTN_BASE) = 0x0000;
#endif

#ifdef CONFIG_UART_RX_DMA
	/* Enable DMA receiver */
	STM32_USART_CR3(UARTN_BASE) |= STM32_USART_CR3_DMAR;
#else
	/* Enable receive-not-empty interrupt */
	STM32_USART_CR1(UARTN_BASE) |= STM32_USART_CR1_RXNEIE;
#endif

#ifdef CHIP_FAMILY_STM32L
	/* Use single-bit sampling */
	STM32_USART_CR3(UARTN_BASE) |= STM32_USART_CR3_ONEBIT;
#endif

	/* Set initial baud rate */
	uart_freq_change();

	/* Enable interrupts */
	task_enable_irq(STM32_IRQ_USART(UARTN));

	init_done = 1;
}

/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Synchronous UART debug printf */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "printf.h"
#include "registers.h"
#include "util.h"

static int debug_txchar(void *context, int c)
{
	if (c == '\n') {
		while (!(STM32_USART_SR(UARTN_BASE) & STM32_USART_SR_TXE))
			;
		STM32_USART_TDR(UARTN_BASE) = '\r';
	}

	/* Wait for space to transmit */
	while (!(STM32_USART_SR(UARTN_BASE) & STM32_USART_SR_TXE))
		;
	STM32_USART_TDR(UARTN_BASE) = c;

	return 0;
}

void debug_printf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfnprintf(debug_txchar, NULL, format, args);
	va_end(args);
}

#ifdef CONFIG_COMMON_RUNTIME
void cflush(void)
{
	/* Wait for transmit complete */
	while (!(STM32_USART_SR(UARTN_BASE) & STM32_USART_SR_TC))
		;
}

int cputs(enum console_channel channel, const char *outstr)
{
	debug_printf(outstr);

	return 0;
}

void panic_puts(const char *outstr)
{
	debug_printf(outstr);
	cflush();
}

int cvprintf(enum console_channel channel, const char *format, va_list args)
{
	vfnprintf(debug_txchar, NULL, format, args);

	return 0;
}

int cprintf(enum console_channel channel, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	cvprintf(channel, format, args);
	va_end(args);

	return 0;
}

void panic_printf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfnprintf(debug_txchar, NULL, format, args);
	va_end(args);

	cflush();
}

int cvprints(enum console_channel channel, const char *format, va_list args)
{
	cvprintf(channel, format, args);
	debug_printf("\n");

	return 0;
}

int cprints(enum console_channel channel, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	cvprints(channel, format, args);
	va_end(args);

	return 0;
}

void uart_init(void)
{
	/* Enable USART1 clock */
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_USART1;
	/* set baudrate */
	STM32_USART_BRR(UARTN_BASE) =
		DIV_ROUND_NEAREST(CPU_CLOCK, CONFIG_UART_BAUD_RATE);
	/* UART enabled, 8 Data bits, oversampling x16, no parity */
	STM32_USART_CR1(UARTN_BASE) = STM32_USART_CR1_UE | STM32_USART_CR1_TE |
				      STM32_USART_CR1_RE;
	/* 1 stop bit, no fancy stuff */
	STM32_USART_CR2(UARTN_BASE) = 0x0000;
	/* DMA disabled, special modes disabled, error interrupt disabled */
	STM32_USART_CR3(UARTN_BASE) = 0x0000;

	/* Configure GPIOs */
	gpio_config_module(MODULE_UART, 1);
}
#endif

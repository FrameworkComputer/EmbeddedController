/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* STM32L-discovery board configuration */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "queue_policies.h"
#include "registers.h"
#include "task.h"
#include "usart-stm32f0.h"
#include "usart_rx_dma.h"
#include "usart_tx_dma.h"
#include "util.h"

void button_event(enum gpio_signal signal);

#include "gpio_list.h"

void button_event(enum gpio_signal signal)
{
	static int count;

	gpio_set_level(GPIO_LED_GREEN, ++count & 0x02);
}

void usb_gpio_tick(void)
{
	static int count;

	gpio_set_level(GPIO_LED_BLUE, ++count & 0x01);
}
DECLARE_HOOK(HOOK_TICK, usb_gpio_tick, HOOK_PRIO_DEFAULT);

/******************************************************************************
 * Setup USART2 as a loopback device, it just echo's back anything sent to it.
 */
static struct usart_config const loopback_usart;

static struct queue const loopback_queue =
	QUEUE_DIRECT(64, uint8_t,
		     loopback_usart.producer,
		     loopback_usart.consumer);

static struct usart_rx_dma const loopback_rx_dma =
	USART_RX_DMA(STM32_DMAC_CH6, 32);

static struct usart_tx_dma const loopback_tx_dma =
	USART_TX_DMA(STM32_DMAC_CH7, 16);

static struct usart_config const loopback_usart =
	USART_CONFIG(usart2_hw,
		     loopback_rx_dma.usart_rx,
		     loopback_tx_dma.usart_tx,
		     115200,
		     0,
		     loopback_queue,
		     loopback_queue);

/******************************************************************************
 * Initialize board.
 */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_USER_BUTTON);

	usart_init(&loopback_usart);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

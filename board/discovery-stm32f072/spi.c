/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "gpio.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "usb_spi.h"

void usb_spi_ready(struct usb_spi_config const *config)
{
	task_wake(TASK_ID_USB_SPI);
}

USB_SPI_CONFIG(usb_spi, USB_IFACE_SPI, USB_EP_SPI, usb_spi_ready)

void usb_spi_task(void)
{
	/* Remap SPI2 to DMA channels 6 and 7 */
	STM32_SYSCFG_CFGR1 |= (1 << 24);

	gpio_config_module(MODULE_SPI_MASTER, 1);

	/* Set all four SPI pins to high speed */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xff000000;

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR |= STM32_RCC_PB1_SPI2;
	STM32_RCC_APB1RSTR &= ~STM32_RCC_PB1_SPI2;

	spi_enable(1);

	while (1) {
		task_wait_event(-1);

		while (usb_spi_service_request(&usb_spi))
			;
	}
}

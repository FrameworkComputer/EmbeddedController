/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* STM32F072-discovery board configuration */

#include "common.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "usb_gpio.h"
#include "usb_spi.h"
#include "util.h"

void button_event(enum gpio_signal signal);

#include "gpio_list.h"

void button_event(enum gpio_signal signal)
{
	static int count = 0;

	gpio_set_level(GPIO_LED_U, (count & 0x03) == 0);
	gpio_set_level(GPIO_LED_R, (count & 0x03) == 1);
	gpio_set_level(GPIO_LED_D, (count & 0x03) == 2);
	gpio_set_level(GPIO_LED_L, (count & 0x03) == 3);

	count++;
}

static enum gpio_signal const usb_gpio_list[] = {
	GPIO_USER_BUTTON,
	GPIO_LED_U,
	GPIO_LED_D,
	GPIO_LED_L,
	GPIO_LED_R,
};

USB_GPIO_CONFIG(usb_gpio,
		usb_gpio_list,
		USB_IFACE_GPIO,
		USB_EP_GPIO)

const void *const usb_strings[] = {
	[USB_STR_DESC]         = usb_string_desc,
	[USB_STR_VENDOR]       = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]      = USB_STRING_DESC("discovery-stm32f072"),
	[USB_STR_VERSION]      = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_STREAM_NAME]  = USB_STRING_DESC("Echo"),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Shell"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

void usb_spi_board_enable(struct usb_spi_config const *config)
{
	/* Remap SPI2 to DMA channels 6 and 7 */
	STM32_SYSCFG_CFGR1 |= (1 << 24);

	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI_MASTER, 1);

	/* Set all four SPI pins to high speed */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xff000000;

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR |= STM32_RCC_PB1_SPI2;
	STM32_RCC_APB1RSTR &= ~STM32_RCC_PB1_SPI2;

	spi_enable(1);
}

void usb_spi_board_disable(struct usb_spi_config const *config)
{
	spi_enable(0);

	/* Disable clocks to SPI2 module */
	STM32_RCC_APB1ENR &= ~STM32_RCC_PB1_SPI2;

	/* Release SPI GPIOs */
	gpio_config_module(MODULE_SPI_MASTER, 0);
}

USB_SPI_CONFIG(usb_spi, USB_IFACE_SPI, USB_EP_SPI);

/* Initialize board. */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_USER_BUTTON);

	usb_spi_enable(&usb_spi, 1);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

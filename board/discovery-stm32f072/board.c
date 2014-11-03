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
#include "task.h"
#include "usb_gpio.h"
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

/* Initialize board. */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_USER_BUTTON);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

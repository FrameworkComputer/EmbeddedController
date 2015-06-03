/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "usb.h"
#include "usb_hid.h"
#include "util.h"

/*
 * There's no way to trigger on both rising and falling edges, so force a
 * compiler error if we try. The workaround is to use the pinmux to connect
 * two GPIOs to the same input and configure each one for a separate edge.
 */
#undef GPIO_INT_BOTH
#define GPIO_INT_BOTH NOT_SUPPORTED_ON_CR50

#include "gpio_list.h"

#ifdef CONFIG_USB_HID
static void send_hid_event(void)
{
#if !defined(CHIP_VARIANT_CR50_A1)
	uint64_t rpt = 0;
	uint8_t *key_ptr = (void *)&rpt + 2;
	/* Convert SW_N/SW_S/SW_W/SW_E to A,B,C,D keys */
	if (gpio_get_level(GPIO_SW_N))
		*key_ptr++ = 0x04; /* A keycode */
	if (gpio_get_level(GPIO_SW_S))
		*key_ptr++ = 0x05; /* B keycode */
	if (gpio_get_level(GPIO_SW_W))
		*key_ptr++ = 0x06; /* C keycode */
	if (gpio_get_level(GPIO_SW_E))
		*key_ptr++ = 0x07; /* D keycode */
	/* send the keyboard state over USB HID */
	set_keyboard_report(rpt);
	/* check release in the future */
	hook_call_deferred(send_hid_event, 40);
#endif
}
DECLARE_DEFERRED(send_hid_event);
#endif

/* Interrupt handler for button pushes */
void button_event(enum gpio_signal signal)
{
	int v;

	/* We have two GPIOs on the same input (one rising edge, one falling
	 * edge), so de-alias them */
	if (signal >= GPIO_SW_N_)
		signal -= (GPIO_SW_N_ - GPIO_SW_N);

	v = gpio_get_level(signal);
#ifdef CONFIG_USB_HID
	send_hid_event();
#endif
	ccprintf("Button %d = %d\n", signal, v);
	gpio_set_level(signal - GPIO_SW_N + GPIO_LED_4, v);
}

/* Initialize board. */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_SW_N);
	gpio_enable_interrupt(GPIO_SW_S);
	gpio_enable_interrupt(GPIO_SW_W);
	gpio_enable_interrupt(GPIO_SW_E);
	gpio_enable_interrupt(GPIO_SW_N_);
	gpio_enable_interrupt(GPIO_SW_S_);
	gpio_enable_interrupt(GPIO_SW_W_);
	gpio_enable_interrupt(GPIO_SW_E_);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

#if !defined(CHIP_VARIANT_CR50_A1)
const void * const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Cr50"),
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Shell"),
};
BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);
#endif

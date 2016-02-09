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
#include "trng.h"
#include "usb_descriptor.h"
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

static void init_pmu(void)
{
	/* This boot sequence may be a result of previous soft reset,
	 * in which case the PMU low power sequence register needs to
	 * be reset. */
	GREG32(PMU, LOW_POWER_DIS) = 0;
}

static void init_timers(void)
{
	/* Cancel low speed timers that may have
	 * been initialized prior to soft reset. */
	GREG32(TIMELS, TIMER0_CONTROL) = 0;
	GREG32(TIMELS, TIMER0_LOAD) = 0;
	GREG32(TIMELS, TIMER1_CONTROL) = 0;
	GREG32(TIMELS, TIMER1_LOAD) = 0;
}

static void init_interrupts(void)
{
	int i;
	static const enum gpio_signal gpio_signals[] = {
		GPIO_SW_N, GPIO_SW_S, GPIO_SW_W, GPIO_SW_E,
		GPIO_SW_N_, GPIO_SW_S_, GPIO_SW_W_, GPIO_SW_E_
	};

	for (i = 0; i < ARRAY_SIZE(gpio_signals); i++)
		gpio_enable_interrupt(gpio_signals[i]);
}

enum permission_level {
	PERMISSION_LOW = 0x00,
	PERMISSION_MEDIUM = 0x33,    /* APPS run at medium */
	PERMISSION_HIGH = 0x3C,
	PERMISSION_HIGHEST = 0x55
};

/* Drop run level to at least medium. */
static void init_runlevel(const enum permission_level desired_level)
{
	volatile uint32_t *const reg_addrs[] = {
		GREG32_ADDR(GLOBALSEC, CPU0_S_PERMISSION),
		GREG32_ADDR(GLOBALSEC, DDMA0_PERMISSION),
	};
	int i;

	/* Permission registers drop by 1 level (e.g. HIGHEST -> HIGH)
	 * each time a write is encountered (the value written does
	 * not matter).  So we repeat writes and reads, until the
	 * desired level is reached.
	 */
	for (i = 0; i < ARRAY_SIZE(reg_addrs); i++) {
		uint32_t current_level;

		while (1) {
			current_level = *reg_addrs[i];
			if (current_level <= desired_level)
				break;
			*reg_addrs[i] = desired_level;
		}
	}
}

/* Initialize board. */
static void board_init(void)
{
	init_pmu();
	init_timers();
	init_interrupts();
	init_trng();
	init_runlevel(PERMISSION_MEDIUM);

	/*
	 * SPS is hardwired, all we need to do is enable input mode on the
	 * appropriate pins.
	 */
	GWRITE_FIELD(PINMUX, DIOA2_CTL, IE, 1);   /* MOSI */
	GWRITE_FIELD(PINMUX, DIOA6_CTL, IE, 1);   /* CLK */
	GWRITE_FIELD(PINMUX, DIOA12_CTL, IE, 1);  /* CS */
}

DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

#if defined(CONFIG_USB)
const void * const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Cr50"),
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Shell"),
	[USB_STR_BLOB_NAME] = USB_STRING_DESC("Blob"),
	[USB_STR_HID_NAME] = USB_STRING_DESC("PokeyPokey"),
};
BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);
#endif

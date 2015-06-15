/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* STM32F072-discovery board based USB PD evaluation configuration */

#include "common.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "usb.h"
#include "usb_pd.h"
#include "util.h"

void button_event(enum gpio_signal signal);

void alert_event(enum gpio_signal signal)
{
	/* Exchange status with PD MCU. */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
}

#include "gpio_list.h"

const void *const usb_strings[] = {
	[USB_STR_DESC]         = usb_string_desc,
	[USB_STR_VENDOR]       = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]      = USB_STRING_DESC("PDeval-stm32f072"),
	[USB_STR_VERSION]      = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Shell"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/* Initialize board. */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_USER_BUTTON);
	gpio_enable_interrupt(GPIO_PD_MCU_INT);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_reset_pd_mcu(void)
{
}

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"tcpc", I2C_PORT_TCPC, 100 /* kHz */, GPIO_I2C0_SCL, GPIO_I2C0_SDA}
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

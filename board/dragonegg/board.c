/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* DragonEgg board-specific configuration */

#include "common.h"
#include "console.h"
#include "driver/ppc/sn5s330.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "intc.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm_chip.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "uart.h"
#include "util.h"

static void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_USB_C0_TCPPC_INT_L)
		sn5s330_interrupt(0);
}

#include "gpio_list.h" /* Must come after other header files. */

/******************************************************************************/
/* SPI devices */
/* TODO(b/110880394): Fill out correctly (SPI FLASH) */
const struct spi_device_t spi_devices[] = {
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = { .channel = 0, .flags = 0, .freq_hz = 100 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* GPIO to enable/disable the USB Type-A port. */
const int usb_port_enable[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT] = {
	GPIO_EN_USB_A_5V,
};

void board_overcurrent_event(int port)
{
	if (port == 0) {
		/* TODO(b/111281797): When does this get set high again? */
		gpio_set_level(GPIO_USB_OC_ODL, 0);
		cprints(CC_USBPD, "p%d: overcurrent!", port);
	}
}

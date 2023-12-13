/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "dma.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "stm32-dma.h"
#include "usb_descriptor.h"
#include "usb_dwc_console.h"
#include "usb_hw.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */
const void *const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google LLC"),
	[USB_STR_PRODUCT] = USB_STRING_DESC("stm32f446-eval"),
	[USB_STR_SERIALNO] = USB_STRING_DESC("1234-a"),
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("EC Shell"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

struct dwc_usb usb_ctl = {
	.ep = {
		&ep0_ctl,
		&ep_console_ctl,
	},
	.speed = USB_SPEED_FS,
	.phy_type = USB_PHY_ULPI,
	.dma_en = 1,
	.irq = STM32_IRQ_OTG_HS,
};

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "i2c1",
	  .port = I2C_PORT_0,
	  .kbps = 100,
	  .scl = GPIO_I2C1_SCL,
	  .sda = GPIO_I2C1_SDA },
	{ .name = "fmpi2c4",
	  .port = FMPI2C_PORT_3,
	  .kbps = 100,
	  .scl = GPIO_FMPI2C_SCL,
	  .sda = GPIO_FMPI2C_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#define GPIO_SET_HS(bank, number) \
	(STM32_GPIO_OSPEEDR(GPIO_##bank) |= (0x3 << ((number) * 2)))

void board_config_post_gpio_init(void)
{
	/* We use MCO2 clock passthrough to provide a clock to USB HS */
	gpio_config_module(MODULE_MCO, 1);
	/* GPIO PC9 to high speed */
	GPIO_SET_HS(C, 9);

	/* Set USB GPIO to high speed */
	GPIO_SET_HS(A, 11);
	GPIO_SET_HS(A, 12);

	GPIO_SET_HS(C, 3);
	GPIO_SET_HS(C, 2);
	GPIO_SET_HS(C, 0);
	GPIO_SET_HS(A, 5);

	GPIO_SET_HS(B, 5);
	GPIO_SET_HS(B, 13);
	GPIO_SET_HS(B, 12);
	GPIO_SET_HS(B, 2);
	GPIO_SET_HS(B, 10);
	GPIO_SET_HS(B, 1);
	GPIO_SET_HS(B, 0);
	GPIO_SET_HS(A, 3);

	/* Set I2C GPIO to HS */
	GPIO_SET_HS(B, 6);
	GPIO_SET_HS(B, 7);
	GPIO_SET_HS(F, 1);
	GPIO_SET_HS(F, 0);
	GPIO_SET_HS(A, 8);
	GPIO_SET_HS(B, 4);
	GPIO_SET_HS(C, 6);
	GPIO_SET_HS(C, 7);
}

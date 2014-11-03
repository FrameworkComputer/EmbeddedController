/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Twinkie dongle configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ina2xx.h"
#include "registers.h"
#include "task.h"
#include "usb.h"
#include "util.h"

void cc2_event(enum gpio_signal signal)
{
	ccprintf("INA!\n");
}

void vbus_event(enum gpio_signal signal)
{
	ccprintf("INA!\n");
}

#include "gpio_list.h"

/* Initialize board. */
void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= 1 << 0;
	/* Remap USART DMA to match the USART driver and TIM2 DMA */
	STM32_SYSCFG_CFGR1 |= (1 << 9) | (1 << 10) /* Remap USART1 RX/TX DMA */
			   |  (1 << 29);/* Remap TIM2 DMA */
	/* 40 MHz pin speed on UART PA9/PA10 */
	STM32_GPIO_OSPEEDR(GPIO_A) |= 0x003C0000;
	/* 40 MHz pin speed on TX clock out PB9 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x000C0000;
}

static void board_init(void)
{
	/* Enable interrupts for INAs. */
	gpio_enable_interrupt(GPIO_CC2_ALERT_L);
	gpio_enable_interrupt(GPIO_VBUS_ALERT_L);

	/* Calibrate INA0 (VBUS) with 1mA/LSB scale */
	ina2xx_init(0, 0x8000, INA2XX_CALIB_1MA(15 /*mOhm*/));
	/* Disable INA1 (VCONN2) to avoid leaking current */
	ina2xx_init(1, 0, INA2XX_CALIB_1MA(15 /*mOhm*/));
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_CH_CC1_PD] = {"CC1_PD", 3300, 4096, 0, STM32_AIN(1)},
	[ADC_CH_CC2_PD] = {"CC2_PD", 3300, 4096, 0, STM32_AIN(3)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100, GPIO_I2C_SCL, GPIO_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const void * const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Twinkie"),
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_SNIFFER] = USB_STRING_DESC("USB-PD Sniffer"),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Shell"),
};
BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

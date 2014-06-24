/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Twinkie dongle configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ina231.h"
#include "registers.h"
#include "task.h"
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
	/* Remap USART DMA to match the USART driver */
	STM32_SYSCFG_CFGR1 |= (1 << 9) | (1 << 10);/* Remap USART1 RX/TX DMA */
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
	ina231_init(0, 0x8000, INA231_CALIB_1MA(15 /*mOhm*/));
	/* Disable INA1 (VCONN2) to avoid leaking current */
	ina231_init(1, 0, INA231_CALIB_1MA(15 /*mOhm*/));
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	{GPIO_A, 0x0020, 0, MODULE_USB_PD},/* SPI1: SCK(PA5) */
	{GPIO_B, 0x0200, 2, MODULE_USB_PD},/* TIM17_CH1: PB9 */
	{GPIO_A, 0x0600, 1, MODULE_UART, GPIO_PULL_UP},  /* USART1: PA9/PA10 */
	{GPIO_B, 0x00C0, 1, MODULE_I2C},   /* I2C1 MASTER:PB6/7 */
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

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

/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Firefly board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "usb_pd.h"
#include "util.h"

void button_event(enum gpio_signal signal)
{
	int mv;
	switch (signal) {
	case GPIO_SW_PP20000:
		mv = 20000;
		break;
	case GPIO_SW_PP12000:
		mv = 12000;
		break;
	case GPIO_SW_PP5000:
		mv = 5000;
		break;
	default:
		mv = -1;
	}
	pd_request_source_voltage(mv);
	ccprintf("Button %d = %d => Vout=%d mV\n",
		 signal, gpio_get_level(signal), mv);
}

void vbus_event(enum gpio_signal signal)
{
	ccprintf("VBUS! =%d\n", gpio_get_level(signal));
}

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= 1 << 0;
	/* Remap USART DMA to match the USART driver */
	/*
	 * the DMA mapping is :
	 *  Chan 2 : TIM1_CH1  (PD RX)
	 *  Chan 3 : SPI1_TX   (PD TX)
	 *  Chan 4 : USART1_TX
	 *  Chan 5 : USART1_RX
	 */
	STM32_SYSCFG_CFGR1 |= (1 << 9) | (1 << 10);/* Remap USART1 RX/TX DMA */
}

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[] = {
	{"VBUS_WAKE",         GPIO_C, (1<<13), GPIO_INT_BOTH, vbus_event},
	/* Buttons */
	{"SW_PP20000",        GPIO_B, (1<<10), GPIO_INT_FALLING, button_event},
	{"SW_PP12000",        GPIO_B, (1<<11), GPIO_INT_FALLING, button_event},
	{"SW_PP5000",         GPIO_B, (1<<12), GPIO_INT_FALLING, button_event},

	/* PD RX/TX */
	{"USB_CC1_PD",        GPIO_A, (1<<0),  GPIO_ANALOG, NULL},
	{"PD_REF1",           GPIO_A, (1<<1),  GPIO_ANALOG, NULL},
	{"USB_CC2_PD",        GPIO_A, (1<<2),  GPIO_ANALOG, NULL},
	{"PD_REF2",           GPIO_A, (1<<3),  GPIO_ANALOG, NULL},
	{"PD_CC1_TX_EN",      GPIO_A, (1<<4),  GPIO_ODR_HIGH, NULL},
	{"PD_CC2_TX_EN",      GPIO_A, (1<<15), GPIO_ODR_HIGH, NULL},
	{"PD_CLK_OUT",        GPIO_B, (1<<9),  GPIO_OUT_LOW, NULL},
	{"PD_CC1_TX_DATA",    GPIO_A, (1<<6),  GPIO_INPUT, NULL},
	{"PD_CC2_TX_DATA",    GPIO_B, (1<<4),  GPIO_INPUT, NULL},
	{"PD_CLK_IN",         GPIO_B, (1<<3),  GPIO_INPUT, NULL},

	/* CCx device pull-downs */
	{"PD_CC1_DEVICE",     GPIO_B, (1<<13), GPIO_ODR_LOW, NULL},
	{"PD_CC2_DEVICE",     GPIO_B, (1<<14), GPIO_ODR_LOW, NULL},

	/* ADC */
	{"VBUS_SENSE",        GPIO_A, (1<<5),  GPIO_ANALOG, NULL},

	/* LEDs control */
	{"LED_PP20000",       GPIO_B, (1<<0),  GPIO_OUT_LOW, NULL},
	{"LED_PP12000",       GPIO_B, (1<<1),  GPIO_OUT_LOW, NULL},
	{"LED_PP5000",        GPIO_B, (1<<2),  GPIO_OUT_LOW, NULL},

	/* Slave I2C port */
	{"I2C_INT_L",   GPIO_B, (1<<8),  GPIO_ODR_HIGH, NULL},
	/*
	 * I2C pins should be configured as inputs until I2C module is
	 * initialized. This will avoid driving the lines unintentionally.
	 */
	{"I2C_SCL",     GPIO_B, (1<<6),  GPIO_INPUT, NULL},
	{"I2C_SDA",     GPIO_B, (1<<7),  GPIO_INPUT, NULL},

	/* Test points */
	{"TP_A8",             GPIO_A, (1<<8),  GPIO_ODR_HIGH, NULL},
	{"TP_A13",            GPIO_A, (1<<13), GPIO_ODR_HIGH, NULL},
	{"TP_A14",            GPIO_A, (1<<14), GPIO_ODR_HIGH, NULL},
	{"TP_B15",            GPIO_B, (1<<15), GPIO_ODR_HIGH, NULL},
	{"TP_C14",            GPIO_C, (1<<14), GPIO_ODR_HIGH, NULL},
	{"TP_C15",            GPIO_C, (1<<15), GPIO_ODR_HIGH, NULL},
	{"TP_F0",             GPIO_F, (1<<0),  GPIO_ODR_HIGH, NULL},
	{"TP_F1",             GPIO_F, (1<<1),  GPIO_ODR_HIGH, NULL},

	/* Unimplemented signals which we need to emulate for now */
	GPIO_SIGNAL_NOT_IMPLEMENTED("ENTERING_RW"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("WP_L"),
};
BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	{GPIO_B, 0x0008, 0, MODULE_USB_PD},/* SPI1: SCK(PB3) */
	{GPIO_B, 0x0200, 2, MODULE_USB_PD},/* TIM17_CH1: PB9) */
	{GPIO_A, 0x0600, 1, MODULE_UART, GPIO_PULL_UP},/* USART1: PA9/PA10 */
	{GPIO_B, 0x00c0, 1, MODULE_I2C},   /* I2C SLAVE:PB6/7 */
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_CH_CC1_PD] = {"CC1_PD", 3300, 4096, 0, STM32_AIN(0)},
	[ADC_CH_CC2_PD] = {"CC2_PD", 3300, 4096, 0, STM32_AIN(2)},
	/* VBUS voltage sensing is behind a 14.3K/100K voltage divider */
	[ADC_CH_VBUS_SENSE] = {"VBUS", 26377, 4096, 0, STM32_AIN(5)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

static void board_init(void)
{
	gpio_enable_interrupt(GPIO_SW_PP20000);
	gpio_enable_interrupt(GPIO_SW_PP12000);
	gpio_enable_interrupt(GPIO_SW_PP5000);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static int command_volt(int argc, char **argv)
{
	int millivolt = -1;
	if (argc >= 2) {
		char *e;
		millivolt = strtoi(argv[1], &e, 10) * 1000;
	}
	ccprintf("Request Vout=%d mV\n", millivolt);
	pd_request_source_voltage(millivolt);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(volt, command_volt,
			"[5|12|20]",
			"set voltage through USB PD",
			NULL);

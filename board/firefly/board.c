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
#include "timer.h"
#include "usb_pd.h"
#include "util.h"

/* Debounce time for voltage buttons */
#define BUTTON_DEBOUNCE_US (100 * MSEC)

static enum gpio_signal button_pressed;

/* Handle debounced button press */
static void button_deferred(void)
{
	int mv;

	/* bounce ? */
	if (gpio_get_level(button_pressed) != 0)
		return;

	switch (button_pressed) {
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
	pd_request_source_voltage(0, mv);
	ccprintf("Button %d = %d => Vout=%d mV\n",
		 button_pressed, gpio_get_level(button_pressed), mv);
}
DECLARE_DEFERRED(button_deferred);

void button_event(enum gpio_signal signal)
{
	button_pressed = signal;
	/* reset debounce time */
	hook_call_deferred(button_deferred, BUTTON_DEBOUNCE_US);
}

void vbus_event(enum gpio_signal signal)
{
	ccprintf("VBUS! =%d\n", gpio_get_level(signal));
	task_wake(TASK_ID_PD);
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

#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_CH_CC1_PD] = {"CC1_PD", 3300, 4096, 0, STM32_AIN(0)},
	[ADC_CH_CC2_PD] = {"CC2_PD", 3300, 4096, 0, STM32_AIN(2)},
	/* VBUS voltage sensing is behind a 10K/100K voltage divider */
	[ADC_CH_VBUS_SENSE] = {"VBUS", 36300, 4096, 0, STM32_AIN(5)},
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

	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_VBUS_WAKE);
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
	pd_request_source_voltage(0, millivolt);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(volt, command_volt,
			"[5|12|20]",
			"set voltage through USB PD",
			NULL);

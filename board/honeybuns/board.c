/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Honeybuns board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "usb.h"
#include "usb_pd.h"
#include "util.h"



void vbus_event(enum gpio_signal signal)
{
	ccprintf("VBUS!\n");
}


#include "gpio_list.h"


static void honeybuns_test_led_update(void)
{
	static int toggle_count;
	toggle_count++;

	gpio_set_level(GPIO_TP6, toggle_count&1);
}
DECLARE_HOOK(HOOK_TICK, honeybuns_test_led_update, HOOK_PRIO_DEFAULT);

/* Initialize board. */
void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= 1 << 0;

	/*
	 * the DMA mapping is :
	 *  Chan 2 : TIM1_CH1  (C0 RX)
	 *  Chan 3 : SPI1_TX   (C0 TX)
	 *  Chan 4 : USART1_TX
	 *  Chan 5 : USART1_RX
	 *  Chan 6 :
	 *  Chan 7 :
	 */
	/* Remap USART DMA to match the USART driver */
	STM32_SYSCFG_CFGR1 |= (1 << 9) | (1 << 10);
}


/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_CH_CC1_PD] = {"CC1_PD", 3300, 4096, 0, STM32_AIN(1)},
	/* VBUS sense via 100k/8.8k voltage divder 3.3V -> 40.8V */
	[ADC_CH_VIN_DIV_P] = {"VIN_DIV_P", 40800, 4096, 0, STM32_AIN(5)},
	[ADC_CH_VIN_DIV_N] = {"VIN_DIV_N", 40800, 4096, 0, STM32_AIN(6)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const void * const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Honeybuns"),
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_BB_URL] = USB_STRING_DESC(USB_GOOGLE_TYPEC_URL),
};
BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);




void board_set_usb_mux(int port, enum typec_mux mux,
		       enum usb_switch usb, int polarity)
{

	if (mux == TYPEC_MUX_NONE) {
		/* put the mux in the high impedance state */
		gpio_set_level(GPIO_SS_MUX_OE_L, 1);
		return;
	}

	if ((mux == TYPEC_MUX_DOCK) || (mux == TYPEC_MUX_USB)) {
		/* Low selects USB Dock */
		gpio_set_level(GPIO_SS_MUX_SEL, 0);
	} else if (mux == TYPEC_MUX_DP) {
		/* high selects display port */
		gpio_set_level(GPIO_SS_MUX_SEL, 1);
	}

	/* clear OE line to make mux active */
	gpio_set_level(GPIO_SS_MUX_OE_L, 0);
}

int board_get_usb_mux(int port, const char **dp_str, const char **usb_str)
{
	int oe_disabled = gpio_get_level(GPIO_SS_MUX_OE_L);
	int dp_4lanes = gpio_get_level(GPIO_SS_MUX_SEL);

	if (oe_disabled) {
		*usb_str = NULL;
		*dp_str = NULL;
		return 0;
	}

	if (dp_4lanes) {
		*dp_str = "DP_4LANE";
		*usb_str = NULL;
	} else {
		*dp_str = "DP_2LANE";
		*usb_str = "DOCK";
	}
	return 1;
}

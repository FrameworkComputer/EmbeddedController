/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Fruitpie board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "util.h"

void rohm_event(enum gpio_signal signal)
{
	ccprintf("ROHM!\n");
}

void vbus_event(enum gpio_signal signal)
{
	ccprintf("VBUS!\n");
}

void tsu_event(enum gpio_signal signal)
{
	ccprintf("TSU!\n");
}

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[] = {
	{"VBUS_WAKE",         GPIO_B, (1<<5),  GPIO_INT_BOTH, vbus_event},
	{"MASTER_I2C_INT_L",  GPIO_C, (1<<13), GPIO_INT_FALLING, tsu_event},

	/* PD RX/TX */
	{"USB_CC1_PD",        GPIO_A, (1<<0),  GPIO_ANALOG, NULL},
	{"PD_REF1",           GPIO_A, (1<<1),  GPIO_ANALOG, NULL},
	{"PD_REF2",           GPIO_A, (1<<3),  GPIO_ANALOG, NULL},
	{"USB_CC2_PD",        GPIO_A, (1<<4),  GPIO_ANALOG, NULL},
	{"PD_CLK_OUT",        GPIO_B, (1<<9),  GPIO_OUT_LOW, NULL},
	{"PD_TX_EN",          GPIO_B, (1<<12), GPIO_OUT_LOW, NULL},
#if 0
	{"PD_CLK_IN",         GPIO_B, (1<<13), GPIO_OUT_LOW, NULL},
	{"PD_TX_DATA",        GPIO_B, (1<<14), GPIO_OUT_LOW, NULL},
#endif

	/* Power and muxes control */
	{"PP5000_EN",         GPIO_A, (1<<5),  GPIO_OUT_HIGH, NULL},
	{"CC_HOST",           GPIO_A, (1<<6),  GPIO_OUT_LOW, NULL},
	{"CHARGE_EN_L",       GPIO_A, (1<<8),  GPIO_OUT_LOW, NULL},
	{"USB_C_5V_EN",       GPIO_A, (1<<10), GPIO_OUT_LOW, NULL},
	{"VCONN1_EN",         GPIO_B, (1<<15), GPIO_OUT_LOW, NULL},
	{"VCONN2_EN",         GPIO_C, (1<<14), GPIO_OUT_LOW, NULL},
	{"SS1_EN_L",          GPIO_A, (1<<9),  GPIO_OUT_HIGH, NULL},
	{"SS2_EN_L",          GPIO_B, (1<<4),  GPIO_OUT_HIGH, NULL},
	{"SS2_USB_MODE_L",    GPIO_B, (1<<3),  GPIO_OUT_HIGH, NULL},
	{"SS1_USB_MODE_L",    GPIO_B, (1<<8),  GPIO_OUT_HIGH, NULL},
	{"DP_MODE",           GPIO_C, (1<<15), GPIO_OUT_LOW, NULL},
	{"DP_POLARITY_L",     GPIO_A, (1<<7),  GPIO_OUT_HIGH, NULL},

	/* Not used : no host on that bus */
	{"SLAVE_I2C_INT_L",   GPIO_B, (1<<2),  GPIO_ODR_HIGH, NULL},

	/* Alternate functions */
#if 0
	{"USB_DM",            GPIO_A, (1<<11), GPIO_ANALOG, NULL},
	{"USB_DP",            GPIO_A, (1<<12), GPIO_ANALOG, NULL},
	{"UART_TX",           GPIO_A, (1<<14), GPIO_OUT_LOW, NULL},
	{"UART_RX",           GPIO_A, (1<<15), GPIO_OUT_LOW, NULL},
#endif
	/*
	 * I2C pins should be configured as inputs until I2C module is
	 * initialized. This will avoid driving the lines unintentionally.
	 */
	{"SLAVE_I2C_SCL",     GPIO_B, (1<<6),  GPIO_INPUT, NULL},
	{"SLAVE_I2C_SDA",     GPIO_B, (1<<7),  GPIO_INPUT, NULL},
	{"MASTER_I2C_SCL",    GPIO_B, (1<<10), GPIO_INPUT, NULL},
	{"MASTER_I2C_SDA",    GPIO_B, (1<<11), GPIO_INPUT, NULL},

	/* Rohm BD92104 connections */
	{"ALERT_L",           GPIO_A, (1<<2),  GPIO_INT_FALLING, rohm_event},
	{"USBPD_RST",         GPIO_B, (1<<0),  GPIO_OUT_LOW, NULL},
	{"USBPD_FORCE_OTG",   GPIO_B, (1<<1),  GPIO_OUT_LOW, NULL},
	{"USBPD_VIN_EN_L",    GPIO_F, (1<<0),  GPIO_OUT_HIGH, NULL},

	/* Test points */
	{"TP9",               GPIO_A, (1<<13), GPIO_ODR_HIGH, NULL},
	{"TP11",              GPIO_F, (1<<1),  GPIO_ODR_HIGH, NULL},

	/* Unimplemented signals which we need to emulate for now */
	GPIO_SIGNAL_NOT_IMPLEMENTED("ENTERING_RW"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("WP_L"),
};
BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/* Initialize board. */
static void board_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	{GPIO_B, 0x6000, 0, MODULE_USB_PD},/* SPI2: MISO(PB14) SCK(PB13) */
	{GPIO_B, 0x0200, 2, MODULE_USB_PD},/* TIM17_CH1: PB9) */
	{GPIO_A, 0xC000, 1, MODULE_UART},  /* USART2: PA14/PA15 */
	{GPIO_B, 0x0cc0, 1, MODULE_I2C},   /* I2C SLAVE:PB6/7 MASTER:PB10/11 */
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_CH_CC1_PD] = {"CC1_PD", 3300, 4096, 0, STM32_AIN(0)},
	[ADC_CH_CC2_PD] = {"CC2_PD", 3300, 4096, 0, STM32_AIN(4)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

void board_set_usb_mux(enum typec_mux mux)
{
	/* reset everything */
	gpio_set_level(GPIO_SS1_EN_L, 1);
	gpio_set_level(GPIO_SS2_EN_L, 1);
	gpio_set_level(GPIO_DP_MODE, 0);
	gpio_set_level(GPIO_SS1_USB_MODE_L, 1);
	gpio_set_level(GPIO_SS2_USB_MODE_L, 1);
	switch (mux) {
	case TYPEC_MUX_NONE:
		/* everything is already disabled, we can return */
		return;
	case TYPEC_MUX_USB1:
		gpio_set_level(GPIO_SS1_USB_MODE_L, 0);
		break;
	case TYPEC_MUX_USB2:
		gpio_set_level(GPIO_SS2_USB_MODE_L, 0);
		break;
	case TYPEC_MUX_DP1:
		gpio_set_level(GPIO_DP_POLARITY_L, 1);
		gpio_set_level(GPIO_DP_MODE, 1);
		break;
	case TYPEC_MUX_DP2:
		gpio_set_level(GPIO_DP_POLARITY_L, 0);
		gpio_set_level(GPIO_DP_MODE, 1);
		break;
	}
	gpio_set_level(GPIO_SS1_EN_L, 0);
	gpio_set_level(GPIO_SS2_EN_L, 0);
}

static int command_typec(int argc, char **argv)
{
	const char * const mux_name[] = {"none", "usb1", "usb2", "dp1", "dp2"};

	if (argc < 2) {
		/* dump current state */
		ccprintf("CC1 %d mV  CC2 %d mV\n",
			adc_read_channel(ADC_CH_CC1_PD),
			adc_read_channel(ADC_CH_CC2_PD));
		ccprintf("DP %d Polarity %d\n", gpio_get_level(GPIO_DP_MODE),
			!!gpio_get_level(GPIO_DP_POLARITY_L) + 1);
		ccprintf("Superspeed %s\n",
			gpio_get_level(GPIO_SS1_EN_L) ? "None" :
			(gpio_get_level(GPIO_DP_MODE) ? "DP" :
			(!gpio_get_level(GPIO_SS1_USB_MODE_L) ? "USB1" : "USB2")
			));
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "mux")) {
		enum typec_mux mux = TYPEC_MUX_NONE;
		int i;

		if (argc < 3)
			return EC_ERROR_PARAM2;

		for (i = 0; i < ARRAY_SIZE(mux_name); i++)
			if (!strcasecmp(argv[2], mux_name[i]))
				mux = i;
		board_set_usb_mux(mux);
		return EC_SUCCESS;
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_ERROR_UNKNOWN;
}
DECLARE_CONSOLE_COMMAND(typec, command_typec,
			"[mux none|usb1|usb2|dp1|d2]",
			"Control type-C connector",
			NULL);

/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* samus_pd board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "usb_pd_config.h"
#include "util.h"

void vbus_evt(enum gpio_signal signal)
{
	ccprintf("VBUS %d, %d!\n", signal, gpio_get_level(signal));
	task_wake(TASK_ID_PD);
}

void bc12_evt(enum gpio_signal signal)
{
	ccprintf("PERICOM %d!\n", signal);
}

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
	 *  Chan 6 : TIM3_CH1  (C1_RX)
	 *  Chan 7 : SPI2_TX   (C1 TX)
	 */

	/*
	 * Remap USART1 RX/TX DMA to match uart driver. Remap SPI2 RX/TX and
	 * TIM3_CH1 for unique DMA channels.
	 */
	STM32_SYSCFG_CFGR1 |= (1 << 9) | (1 << 10) | (1 << 24) | (1 << 30);
}

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[] = {
	/* Interrupts */
	{"USB_C0_VBUS_WAKE",       GPIO_E, (1<<6),  GPIO_INT_BOTH, vbus_evt},
	{"USB_C1_VBUS_WAKE",       GPIO_F, (1<<2),  GPIO_INT_BOTH, vbus_evt},
	{"USB_C0_BC12_INT_L",      GPIO_B, (1<<0),  GPIO_INT_FALLING, bc12_evt},
	{"USB_C1_BC12_INT_L",      GPIO_C, (1<<1),  GPIO_INT_FALLING, bc12_evt},

	/* PD RX/TX */
	{"USB_C0_CC1_PD",          GPIO_A, (1<<0),  GPIO_ANALOG,   NULL},
	{"USB_C0_REF",             GPIO_A, (1<<1),  GPIO_ANALOG,   NULL},
	{"USB_C1_CC1_PD",          GPIO_A, (1<<2),  GPIO_ANALOG,   NULL},
	{"USB_C1_REF",             GPIO_A, (1<<3),  GPIO_ANALOG,   NULL},
	{"USB_C0_CC2_PD",          GPIO_A, (1<<4),  GPIO_ANALOG,   NULL},
	{"USB_C1_CC2_PD",          GPIO_A, (1<<5),  GPIO_ANALOG,   NULL},
	{"USB_C0_REF_PD_ODL",      GPIO_A, (1<<6),  GPIO_ODR_LOW,  NULL},
	{"USB_C1_REF_PD_ODL",      GPIO_A, (1<<7),  GPIO_ODR_LOW,  NULL},

	{"USB_C_CC_EN",            GPIO_C, (1<<10), GPIO_OUT_LOW,  NULL},
	{"USB_C0_CC1_TX_EN",       GPIO_A, (1<<15), GPIO_OUT_LOW,  NULL},
	{"USB_C0_CC2_TX_EN",       GPIO_E, (1<<12), GPIO_OUT_LOW,  NULL},
	{"USB_C1_CC1_TX_EN",       GPIO_B, (1<<9),  GPIO_OUT_LOW,  NULL},
	{"USB_C1_CC2_TX_EN",       GPIO_B, (1<<12), GPIO_OUT_LOW,  NULL},
	{"USB_C0_CC1_TX_DATA",     GPIO_B, (1<<4),  GPIO_OUT_LOW,  NULL},
	{"USB_C1_CC1_TX_DATA",     GPIO_B, (1<<14), GPIO_OUT_LOW,  NULL},
	{"USB_C0_CC2_TX_DATA",     GPIO_E, (1<<14), GPIO_OUT_LOW,  NULL},
	{"USB_C1_CC2_TX_DATA",     GPIO_D, (1<<3),  GPIO_OUT_LOW,  NULL},

#if 0
	/* Alternate functions */
	{"USB_C0_TX_CLKOUT",       GPIO_B, (1<<1),  GPIO_OUT_LOW,  NULL},
	{"USB_C1_TX_CLKOUT",       GPIO_E, (1<<1),  GPIO_OUT_LOW,  NULL},
	{"USB_C0_TX_CLKIN",        GPIO_B, (1<<3),  GPIO_OUT_LOW,  NULL},
	{"USB_C1_TX_CLKIN",        GPIO_B, (1<<13), GPIO_OUT_LOW,  NULL},
#endif

	/* Power and muxes control */
	{"PP3300_USB_PD_EN",       GPIO_A, (1<<8),  GPIO_OUT_HIGH, NULL},
	{"USB_C0_CHARGE_EN_L",     GPIO_D, (1<<12), GPIO_OUT_LOW, NULL},
	{"USB_C1_CHARGE_EN_L",     GPIO_D, (1<<13), GPIO_OUT_HIGH, NULL},
	{"USB_C0_5V_EN",           GPIO_D, (1<<14), GPIO_OUT_LOW,  NULL},
	{"USB_C1_5V_EN",           GPIO_D, (1<<15), GPIO_OUT_HIGH, NULL},
	{"USB_C0_CC1_VCONN1_EN_L", GPIO_D, (1<<8),  GPIO_OUT_HIGH, NULL},
	{"USB_C0_CC2_VCONN1_EN_L", GPIO_D, (1<<9),  GPIO_OUT_HIGH, NULL},
	{"USB_C1_CC1_VCONN1_EN_L", GPIO_D, (1<<10), GPIO_OUT_HIGH, NULL},
	{"USB_C1_CC2_VCONN1_EN_L", GPIO_D, (1<<11), GPIO_OUT_HIGH, NULL},

	{"USB_C0_CC1_ODL",         GPIO_B, (1<<8),  GPIO_ODR_LOW,  NULL},
	{"USB_C0_CC2_ODL",         GPIO_E, (1<<0),  GPIO_ODR_LOW,  NULL},
	{"USB_C1_CC1_ODL",         GPIO_F, (1<<9),  GPIO_ODR_LOW,  NULL},
	{"USB_C1_CC2_ODL",         GPIO_F, (1<<10), GPIO_ODR_LOW,  NULL},

	{"USB_C_BC12_SEL",         GPIO_C, (1<<0),  GPIO_OUT_LOW,  NULL},
	{"USB_C0_SS1_EN_L",        GPIO_E, (1<<2),  GPIO_OUT_HIGH, NULL},
	{"USB_C0_SS2_EN_L",        GPIO_E, (1<<3),  GPIO_OUT_HIGH, NULL},
	{"USB_C1_SS1_EN_L",        GPIO_E, (1<<9),  GPIO_OUT_HIGH, NULL},
	{"USB_C1_SS2_EN_L",        GPIO_E, (1<<10), GPIO_OUT_HIGH, NULL},
	{"USB_C0_SS1_DP_MODE_L",   GPIO_E, (1<<4),  GPIO_OUT_HIGH, NULL},
	{"USB_C0_SS2_DP_MODE_L",   GPIO_E, (1<<5),  GPIO_OUT_HIGH, NULL},
	{"USB_C1_SS1_DP_MODE_L",   GPIO_E, (1<<11), GPIO_OUT_HIGH, NULL},
	{"USB_C1_SS2_DP_MODE_L",   GPIO_E, (1<<13), GPIO_OUT_HIGH, NULL},
	{"USB_C0_DP_MODE_L",       GPIO_E, (1<<8),  GPIO_OUT_HIGH, NULL},
	{"USB_C1_DP_MODE_L",       GPIO_F, (1<<6),  GPIO_OUT_LOW,  NULL},
	{"USB_C0_DP_POLARITY_L",   GPIO_E, (1<<7),  GPIO_OUT_HIGH, NULL},
	{"USB_C1_DP_POLARITY_L",   GPIO_F, (1<<3),  GPIO_OUT_HIGH, NULL},

#if 0
	/* Alternate functions */
	{"USB_DM",                 GPIO_A, (1<<11), GPIO_ANALOG,   NULL},
	{"USB_DP",                 GPIO_A, (1<<12), GPIO_ANALOG,   NULL},
	{"UART_TX",                GPIO_A, (1<<9),  GPIO_OUT_LOW,  NULL},
	{"UART_RX",                GPIO_A, (1<<10), GPIO_OUT_LOW,  NULL},
	{"TP64_SWDIO",             GPIO_A, (1<<13), GPIO_ODR_HIGH, NULL},
	{"TP71_SWCLK",             GPIO_A, (1<<14), GPIO_ODR_HIGH, NULL},
#endif

	/*
	 * I2C pins should be configured as inputs until I2C module is
	 * initialized. This will avoid driving the lines unintentionally.
	 */
	{"SLAVE_I2C_SCL",          GPIO_B, (1<<6),  GPIO_INPUT,    NULL},
	{"SLAVE_I2C_SDA",          GPIO_B, (1<<7),  GPIO_INPUT,    NULL},
	{"MASTER_I2C_SCL",         GPIO_B, (1<<10), GPIO_INPUT,    NULL},
	{"MASTER_I2C_SDA",         GPIO_B, (1<<11), GPIO_INPUT,    NULL},

	/* Test points */
	{"TP60",                   GPIO_C, (1<<11), GPIO_ODR_HIGH, NULL},

	/* Case closed debugging. */
	{"SPI_FLASH_WP_L",         GPIO_D, (1<<2),  GPIO_INPUT,    NULL},
	{"EC_INT_L",               GPIO_B, (1<<2),  GPIO_ODR_HIGH, NULL},
	{"EC_IN_RW",               GPIO_C, (1<<12), GPIO_INPUT,    NULL},
	{"EC_RST_L",               GPIO_C, (1<<13), GPIO_OUT_HIGH, NULL},
	{"SPI_FLASH_CS_L",         GPIO_D, (1<<0),  GPIO_INPUT,    NULL},
	{"SPI_FLASH_CLK",          GPIO_D, (1<<1),  GPIO_INPUT,    NULL},
	{"SPI_FLASH_MOSI",         GPIO_C, (1<<3),  GPIO_INPUT,    NULL},
	{"SPI_FLASH_MISO",         GPIO_C, (1<<2),  GPIO_INPUT,    NULL},
	{"EC_JTAG_TMS",            GPIO_C, (1<<6),  GPIO_INPUT,    NULL},
	{"EC_JTAG_TCK",            GPIO_C, (1<<7),  GPIO_INPUT,    NULL},
	{"EC_JTAG_TDO",            GPIO_C, (1<<8),  GPIO_INPUT,    NULL},
	{"EC_JTAG_TDI",            GPIO_C, (1<<9),  GPIO_INPUT,    NULL},
#if 0
	/* Alternate functions */
	{"EC_UART_TX",             GPIO_C, (1<<4),  GPIO_OUT_LOW,  NULL},
	{"EC_UART_RX",             GPIO_C, (1<<5),  GPIO_INPUT,    NULL},
	{"AP_UART_TX",             GPIO_D, (1<<5),  GPIO_OUT_LOW,  NULL},
	{"AP_UART_RX",             GPIO_D, (1<<6),  GPIO_INPUT,    NULL},
#endif

	/* Unimplemented signals which we need to emulate for now */
	GPIO_SIGNAL_NOT_IMPLEMENTED("ENTERING_RW"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("WP_L"),
};
BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/* Initialize board. */
static void board_init(void)
{
	/*
	 * Enable CC lines after all GPIO have been initialized. Note, it is
	 * important that this is enabled after the CC_ODL lines are set low
	 * to specify device mode.
	 */
	gpio_set_level(GPIO_USB_C_CC_EN, 1);

	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	{GPIO_B, 0x0008, 0, MODULE_USB_PD},/* SPI1: SCK(PB3) */
	{GPIO_B, 0x2000, 0, MODULE_USB_PD},/* SPI2: SCK(PB13) */
	{GPIO_B, 0x0002, 0, MODULE_USB_PD},/* TIM14_CH1: PB1) */
	{GPIO_E, 0x0002, 0, MODULE_USB_PD},/* TIM17_CH1: PE1) */
	{GPIO_A, 0x0600, 1, MODULE_UART},  /* USART1: PA9/PA10 */
	{GPIO_D, 0x0060, 0, MODULE_UART},  /* USART2: PD5/PD6 */
	{GPIO_C, 0x0030, 1, MODULE_UART},  /* USART3: PC4/PC5 */
	{GPIO_B, 0x0cc0, 1, MODULE_I2C},   /* I2C SLAVE:PB6/7 MASTER:PB10/11 */
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_C0_CC1_PD] = {"C0_CC1_PD", 3300, 4096, 0, STM32_AIN(0)},
	[ADC_C1_CC1_PD] = {"C1_CC1_PD", 3300, 4096, 0, STM32_AIN(2)},
	[ADC_C0_CC2_PD] = {"C0_CC2_PD", 3300, 4096, 0, STM32_AIN(4)},
	[ADC_C1_CC2_PD] = {"C1_CC2_PD", 3300, 4096, 0, STM32_AIN(5)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
	{"slave",  I2C_PORT_SLAVE, 100,
		GPIO_SLAVE_I2C_SCL, GPIO_SLAVE_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

void board_set_usb_mux(enum typec_mux mux)
{
	/* reset everything */
	gpio_set_level(GPIO_USB_C0_SS1_EN_L, 1);
	gpio_set_level(GPIO_USB_C0_SS2_EN_L, 1);
	gpio_set_level(GPIO_USB_C0_DP_MODE_L, 1);
	gpio_set_level(GPIO_USB_C0_SS1_DP_MODE_L, 1);
	gpio_set_level(GPIO_USB_C0_SS2_DP_MODE_L, 1);
	switch (mux) {
	case TYPEC_MUX_NONE:
		/* everything is already disabled, we can return */
		return;
	case TYPEC_MUX_USB1:
		gpio_set_level(GPIO_USB_C0_SS1_DP_MODE_L, 0);
		break;
	case TYPEC_MUX_USB2:
		gpio_set_level(GPIO_USB_C0_SS2_DP_MODE_L, 0);
		break;
	case TYPEC_MUX_DP1:
		gpio_set_level(GPIO_USB_C0_DP_POLARITY_L, 1);
		gpio_set_level(GPIO_USB_C0_DP_MODE_L, 0);
		break;
	case TYPEC_MUX_DP2:
		gpio_set_level(GPIO_USB_C0_DP_POLARITY_L, 0);
		gpio_set_level(GPIO_USB_C0_DP_MODE_L, 0);
		break;
	}
	gpio_set_level(GPIO_USB_C0_SS1_EN_L, 0);
	gpio_set_level(GPIO_USB_C0_SS2_EN_L, 0);
}

static int command_typec(int argc, char **argv)
{
	const char * const mux_name[] = {"none", "usb1", "usb2", "dp1", "dp2"};

	if (argc < 2) {
		/* dump current state */
		ccprintf("CC1 %d mV  CC2 %d mV\n",
			pd_adc_read(0),
			pd_adc_read(1));
		ccprintf("DP %d Polarity %d\n",
			!gpio_get_level(GPIO_USB_C0_DP_MODE_L),
			!!gpio_get_level(GPIO_USB_C0_DP_POLARITY_L) + 1);
		ccprintf("Superspeed %s\n",
			gpio_get_level(GPIO_USB_C0_SS1_EN_L) ? "None" :
			(!gpio_get_level(GPIO_USB_C0_DP_MODE_L) ? "DP" :
			(!gpio_get_level(GPIO_USB_C0_SS1_DP_MODE_L) ?
					"USB1" : "USB2")));
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

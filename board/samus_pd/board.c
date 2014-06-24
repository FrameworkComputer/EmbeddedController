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

void pch_evt(enum gpio_signal signal)
{
	ccprintf("PCH change %d!\n", signal);
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

#include "gpio_list.h"

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

	/* Vbus sensing. Converted to mV, full ADC is equivalent to 25.774V. */
	[ADC_BOOSTIN] = {"V_BOOSTIN",  25774, 4096, 0, STM32_AIN(11)},
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

void board_set_usb_mux(int port, enum typec_mux mux)
{
	if (port == 0) {
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
	} else {
		/* reset everything */
		gpio_set_level(GPIO_USB_C1_SS1_EN_L, 1);
		gpio_set_level(GPIO_USB_C1_SS2_EN_L, 1);
		gpio_set_level(GPIO_USB_C1_DP_MODE_L, 1);
		gpio_set_level(GPIO_USB_C1_SS1_DP_MODE_L, 1);
		gpio_set_level(GPIO_USB_C1_SS2_DP_MODE_L, 1);
		switch (mux) {
		case TYPEC_MUX_NONE:
			/* everything is already disabled, we can return */
			return;
		case TYPEC_MUX_USB1:
			gpio_set_level(GPIO_USB_C1_SS1_DP_MODE_L, 0);
			break;
		case TYPEC_MUX_USB2:
			gpio_set_level(GPIO_USB_C1_SS2_DP_MODE_L, 0);
			break;
		case TYPEC_MUX_DP1:
			gpio_set_level(GPIO_USB_C1_DP_POLARITY_L, 1);
			gpio_set_level(GPIO_USB_C1_DP_MODE_L, 0);
			break;
		case TYPEC_MUX_DP2:
			gpio_set_level(GPIO_USB_C1_DP_POLARITY_L, 0);
			gpio_set_level(GPIO_USB_C1_DP_MODE_L, 0);
			break;
		}
		gpio_set_level(GPIO_USB_C1_SS1_EN_L, 0);
		gpio_set_level(GPIO_USB_C1_SS2_EN_L, 0);
	}
}

static int command_typec(int argc, char **argv)
{
	const char * const mux_name[] = {"none", "usb1", "usb2", "dp1", "dp2"};
	char *e;
	int port;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 10);
	if (*e || port >= 2)
		return EC_ERROR_PARAM1;

	if (argc < 3) {
		/* dump current state */
		if (port == 0) {
			ccprintf("Port C%d: CC1 %d mV  CC2 %d mV\n",
				port,
				pd_adc_read(0),
				pd_adc_read(1));
			ccprintf("DP %d Polarity %d\n",
				!gpio_get_level(GPIO_USB_C0_DP_MODE_L),
				!!gpio_get_level(GPIO_USB_C0_DP_POLARITY_L)
					+ 1);
			ccprintf("Superspeed %s\n",
				gpio_get_level(GPIO_USB_C0_SS1_EN_L) ? "None" :
				(!gpio_get_level(GPIO_USB_C0_DP_MODE_L) ? "DP" :
				(!gpio_get_level(GPIO_USB_C0_SS1_DP_MODE_L) ?
						"USB1" : "USB2")));
		} else {
			/* TODO: add param to pd_adc_read() to read C1 ADCs */
			ccprintf("Port C%d: CC1 %d mV  CC2 %d mV\n",
				port,
				adc_read_channel(ADC_C1_CC1_PD),
				adc_read_channel(ADC_C1_CC2_PD));
			ccprintf("DP %d Polarity %d\n",
				!gpio_get_level(GPIO_USB_C1_DP_MODE_L),
				!!gpio_get_level(GPIO_USB_C1_DP_POLARITY_L)
					+ 1);
			ccprintf("Superspeed %s\n",
				gpio_get_level(GPIO_USB_C1_SS1_EN_L) ? "None" :
				(!gpio_get_level(GPIO_USB_C1_DP_MODE_L) ? "DP" :
				(!gpio_get_level(GPIO_USB_C1_SS1_DP_MODE_L) ?
						"USB1" : "USB2")));
		}
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[2], "mux")) {
		enum typec_mux mux = TYPEC_MUX_NONE;
		int i;

		if (argc < 3)
			return EC_ERROR_PARAM3;

		for (i = 0; i < ARRAY_SIZE(mux_name); i++)
			if (!strcasecmp(argv[3], mux_name[i]))
				mux = i;
		board_set_usb_mux(port, mux);
		return EC_SUCCESS;
	} else {
		return EC_ERROR_PARAM2;
	}

	return EC_ERROR_UNKNOWN;
}
DECLARE_CONSOLE_COMMAND(typec, command_typec,
			"port [mux none|usb1|usb2|dp1|d2]",
			"Control type-C connector",
			NULL);

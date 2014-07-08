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
#include "power.h"
#include "registers.h"
#include "task.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "util.h"

/* Chipset power state */
static enum power_state ps;

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
	/* Determine new chipset state, trigger corresponding hook */
	switch (ps) {
	case POWER_S5:
		if (gpio_get_level(GPIO_PCH_SLP_S5_L)) {
			hook_notify(HOOK_CHIPSET_STARTUP);
			ps = POWER_S3;
		}
		break;
	case POWER_S3:
		if (gpio_get_level(GPIO_PCH_SLP_S3_L)) {
			hook_notify(HOOK_CHIPSET_RESUME);
			ps = POWER_S0;
		} else if (!gpio_get_level(GPIO_PCH_SLP_S5_L)) {
			hook_notify(HOOK_CHIPSET_SHUTDOWN);
			ps = POWER_S5;
		}
		break;
	case POWER_S0:
		if (!gpio_get_level(GPIO_PCH_SLP_S3_L)) {
			hook_notify(HOOK_CHIPSET_SUSPEND);
			ps = POWER_S3;
		}
		break;
	default:
		break;
	}
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
	int slp_s5 = gpio_get_level(GPIO_PCH_SLP_S5_L);
	int slp_s3 = gpio_get_level(GPIO_PCH_SLP_S3_L);

	/*
	 * Enable CC lines after all GPIO have been initialized. Note, it is
	 * important that this is enabled after the CC_ODL lines are set low
	 * to specify device mode.
	 */
	gpio_set_level(GPIO_USB_C_CC_EN, 1);

	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE);

	/* Determine initial chipset state */
	if (slp_s5 && slp_s3) {
		hook_notify(HOOK_CHIPSET_RESUME);
		ps = POWER_S0;
	} else if (slp_s5 && !slp_s3) {
		hook_notify(HOOK_CHIPSET_STARTUP);
		ps = POWER_S3;
	} else {
		hook_notify(HOOK_CHIPSET_SHUTDOWN);
		ps = POWER_S5;
	}

	/* Enable interrupts on PCH state change */
	gpio_enable_interrupt(GPIO_PCH_SLP_S3_L);
	gpio_enable_interrupt(GPIO_PCH_SLP_S5_L);
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

void board_set_usb_mux(int port, enum typec_mux mux, int polarity)
{
	if (port == 0) {
		/* reset everything */
		gpio_set_level(GPIO_USB_C0_SS1_EN_L, 1);
		gpio_set_level(GPIO_USB_C0_SS2_EN_L, 1);
		gpio_set_level(GPIO_USB_C0_DP_MODE_L, 1);
		gpio_set_level(GPIO_USB_C0_DP_POLARITY, 1);
		gpio_set_level(GPIO_USB_C0_SS1_DP_MODE, 1);
		gpio_set_level(GPIO_USB_C0_SS2_DP_MODE, 1);

		if (mux == TYPEC_MUX_NONE)
			/* everything is already disabled, we can return */
			return;

		if (mux == TYPEC_MUX_USB || mux == TYPEC_MUX_DOCK) {
			/* USB 3.0 uses 2 superspeed lanes */
			gpio_set_level(polarity ? GPIO_USB_C0_SS2_DP_MODE :
						  GPIO_USB_C0_SS1_DP_MODE, 0);
		}

		if (mux == TYPEC_MUX_DP || mux == TYPEC_MUX_DOCK) {
			/* DP uses available superspeed lanes (x2 or x4) */
			gpio_set_level(GPIO_USB_C0_DP_POLARITY, polarity);
			gpio_set_level(GPIO_USB_C0_DP_MODE_L, 0);
		}
		/* switch on superspeed lanes */
		gpio_set_level(GPIO_USB_C0_SS1_EN_L, 0);
		gpio_set_level(GPIO_USB_C0_SS2_EN_L, 0);
	} else {
		/* reset everything */
		gpio_set_level(GPIO_USB_C1_SS1_EN_L, 1);
		gpio_set_level(GPIO_USB_C1_SS2_EN_L, 1);
		gpio_set_level(GPIO_USB_C1_DP_MODE_L, 1);
		gpio_set_level(GPIO_USB_C1_DP_POLARITY, 1);
		gpio_set_level(GPIO_USB_C1_SS1_DP_MODE, 1);
		gpio_set_level(GPIO_USB_C1_SS2_DP_MODE, 1);

		if (mux == TYPEC_MUX_NONE)
			/* everything is already disabled, we can return */
			return;

		if (mux == TYPEC_MUX_USB || mux == TYPEC_MUX_DOCK) {
			/* USB 3.0 uses 2 superspeed lanes */
			gpio_set_level(polarity ? GPIO_USB_C1_SS2_DP_MODE :
						  GPIO_USB_C1_SS1_DP_MODE, 0);
		}

		if (mux == TYPEC_MUX_DP || mux == TYPEC_MUX_DOCK) {
			/* DP uses available superspeed lanes (x2 or x4) */
			gpio_set_level(GPIO_USB_C1_DP_POLARITY, polarity);
			gpio_set_level(GPIO_USB_C1_DP_MODE_L, 0);
		}
		/* switch on superspeed lanes */
		gpio_set_level(GPIO_USB_C1_SS1_EN_L, 0);
		gpio_set_level(GPIO_USB_C1_SS2_EN_L, 0);
	}
}

/* PD Port polarity as detected by the common PD code */
extern uint8_t pd_polarity;

static int command_typec(int argc, char **argv)
{
	const char * const mux_name[] = {"none", "usb", "dp", "dock"};
	char *e;
	int port;
	enum typec_mux mux = TYPEC_MUX_NONE;
	int i;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 10);
	if (*e || port >= 2)
		return EC_ERROR_PARAM1;

	if (argc < 3) {
		/* dump current state */
		int has_usb, has_dp, has_ss;
		const char *dp_str, *usb_str;
		if (port == 0) {
			has_ss = !gpio_get_level(GPIO_USB_C0_SS1_EN_L);
			has_usb = !gpio_get_level(GPIO_USB_C0_SS1_DP_MODE)
				|| !gpio_get_level(GPIO_USB_C0_SS2_DP_MODE);
			has_dp = !gpio_get_level(GPIO_USB_C0_DP_MODE_L);
			dp_str = gpio_get_level(GPIO_USB_C0_DP_POLARITY) ?
					"DP2" : "DP1";
			usb_str = gpio_get_level(GPIO_USB_C0_SS1_DP_MODE) ?
					"USB2" : "USB1";
		} else {
			has_ss = !gpio_get_level(GPIO_USB_C1_SS1_EN_L);
			has_usb = !gpio_get_level(GPIO_USB_C1_SS1_DP_MODE)
				|| !gpio_get_level(GPIO_USB_C1_SS2_DP_MODE);
			has_dp = !gpio_get_level(GPIO_USB_C1_DP_MODE_L);
			dp_str = gpio_get_level(GPIO_USB_C1_DP_POLARITY) ?
					"DP2" : "DP1";
			usb_str = gpio_get_level(GPIO_USB_C1_SS1_DP_MODE) ?
					"USB2" : "USB1";
		}
		/* TODO: add param to pd_adc_read() to read C1 ADCs */
		ccprintf("Port C%d: CC1 %d mV  CC2 %d mV (polarity:CC%d)\n",
			port,
			port ? adc_read_channel(ADC_C1_CC1_PD) : pd_adc_read(0),
			port ? adc_read_channel(ADC_C1_CC2_PD) : pd_adc_read(1),
			port ? 1 /*TODO: polarity on Port1*/ : pd_polarity + 1);
		if (has_ss)
			ccprintf("Superspeed %s%s%s\n",
				 has_dp ? dp_str : "",
				 has_dp && has_usb ? "+" : "",
				 has_usb ? usb_str : "");
		else
			ccprintf("No Superspeed connection\n");

		return EC_SUCCESS;
	}

	for (i = 0; i < ARRAY_SIZE(mux_name); i++)
		if (!strcasecmp(argv[2], mux_name[i]))
			mux = i;
	board_set_usb_mux(port, mux, pd_polarity);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(typec, command_typec,
			"<port> [none|usb|dp|dock]",
			"Control type-C connector muxing",
			NULL);

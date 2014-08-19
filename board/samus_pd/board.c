/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* samus_pd board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "battery.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "power.h"
#include "registers.h"
#include "switch.h"
#include "task.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "util.h"

/* Chipset power state */
static enum power_state ps;

/* Battery state of charge */
int batt_soc;

void vbus0_evt(enum gpio_signal signal)
{
	ccprintf("VBUS %d, %d!\n", signal, gpio_get_level(signal));
	task_wake(TASK_ID_PD_C0);
}

void vbus1_evt(enum gpio_signal signal)
{
	ccprintf("VBUS %d, %d!\n", signal, gpio_get_level(signal));
	task_wake(TASK_ID_PD_C1);
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
	 *  Chan 6 : TIM3_CH1  (C1 RX)
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
	int pd_enable;
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
	gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE);

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

	/* TODO(crosbug.com/p/31125): remove #if and keep #else for EVT */
#if 1
	/* Enable PD communication */
	pd_enable = 1;
#else
	/*
	 * Do not enable PD communication in RO as a security measure.
	 * We don't want to allow communication to outside world until
	 * we jump to RW. This can by overridden with the removal of
	 * the write protect screw to allow for easier testing, and for
	 * booting without a battery.
	 */
	if (system_get_image_copy() != SYSTEM_IMAGE_RW
	    && !gpio_get_level(GPIO_WP_L)) {
		CPRINTF("[%T PD communication disabled]\n");
		pd_enable = 0;
	} else {
		pd_enable = 1;
	}
#endif
	pd_comm_enable(pd_enable);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

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

int board_get_usb_mux(int port, const char **dp_str, const char **usb_str)
{
	int has_ss, has_usb, has_dp;
	const char *dp, *usb;

	if (port == 0) {
		has_ss = !gpio_get_level(GPIO_USB_C0_SS1_EN_L);
		has_usb = !gpio_get_level(GPIO_USB_C0_SS1_DP_MODE) ||
			  !gpio_get_level(GPIO_USB_C0_SS2_DP_MODE);
		has_dp = !gpio_get_level(GPIO_USB_C0_DP_MODE_L);
		dp = gpio_get_level(GPIO_USB_C0_DP_POLARITY) ?
				"DP2" : "DP1";
		usb = gpio_get_level(GPIO_USB_C0_SS1_DP_MODE) ?
				"USB2" : "USB1";
	} else {
		has_ss = !gpio_get_level(GPIO_USB_C1_SS1_EN_L);
		has_usb = !gpio_get_level(GPIO_USB_C1_SS1_DP_MODE) ||
			  !gpio_get_level(GPIO_USB_C1_SS2_DP_MODE);
		has_dp = !gpio_get_level(GPIO_USB_C1_DP_MODE_L);
		dp = gpio_get_level(GPIO_USB_C1_DP_POLARITY) ?
				"DP2" : "DP1";
		usb = gpio_get_level(GPIO_USB_C1_SS1_DP_MODE) ?
				"USB2" : "USB1";
	}

	*dp_str = has_dp ? dp : NULL;
	*usb_str = has_usb ? usb : NULL;

	return has_ss;
}

void board_update_battery_soc(int soc)
{
	batt_soc = soc;
}

int board_get_battery_soc(void)
{
	return batt_soc;
}

enum battery_present battery_is_present(void)
{
	if (batt_soc >= 0)
		return BP_YES;
	return BP_NOT_SURE;
}

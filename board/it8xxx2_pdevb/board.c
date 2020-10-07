/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* IT8xxx2 PD development board configuration */

#include "adc_chip.h"
#include "battery.h"
#include "console.h"
#include "it83xx_pd.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "timer.h"
#include "usb_pd_tcpm.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define USB_PD_PORT_ITE_0   0
#define USB_PD_PORT_ITE_1   1
#define USB_PD_PORT_ITE_2   2
#define RESISTIVE_DIVIDER   11

int board_get_battery_soc(void)
{
	CPRINTS("%s", __func__);
	return 100;
}

enum battery_present battery_is_present(void)
{
	CPRINTS("%s", __func__);
	return BP_NO;
}

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_ITE_0] = {
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so needn't i2c config */
		.drv = &it83xx_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
	[USB_PD_PORT_ITE_1] = {
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so needn't i2c config */
		.drv = &it83xx_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
};

void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	int cc1_enabled = 0, cc2_enabled = 0;

	if (cc_pin != USBPD_CC_PIN_1)
		cc2_enabled = enabled;
	else
		cc1_enabled = enabled;

	if (port == USBPD_PORT_A) {
		gpio_set_level(GPIO_USBPD_PORTA_CC2_VCONN, cc2_enabled);
		gpio_set_level(GPIO_USBPD_PORTA_CC1_VCONN, cc1_enabled);
	} else if (port == USBPD_PORT_B) {
		gpio_set_level(GPIO_USBPD_PORTB_CC2_VCONN, cc2_enabled);
		gpio_set_level(GPIO_USBPD_PORTB_CC1_VCONN, cc1_enabled);
	} else if (port == USBPD_PORT_C) {
		gpio_set_level(GPIO_USBPD_PORTC_CC2_VCONN, cc2_enabled);
		gpio_set_level(GPIO_USBPD_PORTC_CC1_VCONN, cc1_enabled);
	}

	CPRINTS("p%d Vconn cc1 %d, cc2 %d (On/Off)", port, cc1_enabled,
		cc2_enabled);
}

void board_pd_vbus_ctrl(int port, int enabled)
{
	CPRINTS("p%d Vbus %d(En/Dis)", port, enabled);

	if (port == USBPD_PORT_A) {
		gpio_set_level(GPIO_USBPD_PORTA_VBUS_INPUT, !enabled);
		gpio_set_level(GPIO_USBPD_PORTA_VBUS_OUTPUT, enabled);
		if (!enabled) {
			gpio_set_level(GPIO_USBPD_PORTA_VBUS_DROP, 1);
			udelay(10*MSEC); /* 10ms is a try and error value */
		}
		gpio_set_level(GPIO_USBPD_PORTA_VBUS_DROP, 0);
	} else if (port == USBPD_PORT_B) {
		gpio_set_level(GPIO_USBPD_PORTB_VBUS_INPUT, !enabled);
		gpio_set_level(GPIO_USBPD_PORTB_VBUS_OUTPUT, enabled);
		if (!enabled) {
			gpio_set_level(GPIO_USBPD_PORTB_VBUS_DROP, 1);
			udelay(10*MSEC); /* 10ms is a try and error value */
		}
		gpio_set_level(GPIO_USBPD_PORTB_VBUS_DROP, 0);
	} else if (port == USBPD_PORT_C) {
		gpio_set_level(GPIO_USBPD_PORTC_VBUS_INPUT, !enabled);
		gpio_set_level(GPIO_USBPD_PORTC_VBUS_OUTPUT, enabled);
		if (!enabled) {
			gpio_set_level(GPIO_USBPD_PORTC_VBUS_DROP, 1);
			udelay(10*MSEC); /* 10ms is a try and error value */
		}
		gpio_set_level(GPIO_USBPD_PORTC_VBUS_DROP, 0);
	}

	if (enabled)
		udelay(10*MSEC); /* 10ms is a try and error value */
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	CPRINTS("p%d %s", port, __func__);
}

/*
 * PWM channels. Must be in the exactly same order as in enum pwm_channel.
 * There total three 16 bits clock prescaler registers for all pwm channels,
 * so use the same frequency and prescaler register setting is required if
 * number of pwm channel greater than three.
 */
const struct pwm_t pwm_channels[] = {
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	/*
	 * The register value of ADC reading convert to mV (= register value *
	 * reading max mV / 10 bit solution 1024).
	 * NOTE: If the ADC channel measure VBUS:
	 *       the max reading mv value is the result of resistive divider,
	 *       so VBUS = reading max mv * resistive divider
	 *       (check HW schematic).
	 */
	[ADC_VBUSSA] = {
		.name = "ADC_VBUSSA",
		.factor_mul = ADC_MAX_MVOLT * RESISTIVE_DIVIDER,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = CHIP_ADC_CH7, /* GPI7, ADC7 */
	},
	[ADC_VBUSSB] = {
		.name = "ADC_VBUSSB",
		.factor_mul = ADC_MAX_MVOLT * RESISTIVE_DIVIDER,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = CHIP_ADC_CH3, /* GPI3, ADC3 */
	},
	[ADC_VBUSSC] = {
		.name = "ADC_VBUSSC",
		.factor_mul = ADC_MAX_MVOLT * RESISTIVE_DIVIDER,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = CHIP_ADC_CH16, /* GPL0, ADC16 */
	},
	[ADC_EVB_CH_13] = {
		.name = "ADC_EVB_CH_13",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = CHIP_ADC_CH13, /* GPL1, ADC13 */
	},
	[ADC_EVB_CH_14] = {
		.name = "ADC_EVB_CH_14",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = CHIP_ADC_CH14, /* GPL2, ADC14 */
	},
	[ADC_EVB_CH_15] = {
		.name = "ADC_EVB_CH_15",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = CHIP_ADC_CH15, /* GPL3, ADC15 */
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

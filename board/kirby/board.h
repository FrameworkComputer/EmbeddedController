/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kirby board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#define CONFIG_BATTERY_BQ27541
#define CONFIG_CHARGER
#define CONFIG_CHARGER_BQ24192
#define CONFIG_CHARGER_EN_GPIO
#define CONFIG_CHARGER_EN_ACTIVE_LOW
#define CONFIG_ADC
#define CONFIG_ADC_CLOCK
#define CONFIG_CHIPSET_GAIA
#undef CONFIG_CHIPSET_HAS_PP1350
#undef CONFIG_CHIPSET_HAS_PP5000
#define CONFIG_EXTPOWER_KIRBY
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_I2C
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_SPI
#define CONFIG_PWM
#define CONFIG_USB_SWITCH_TSU6721

#ifndef __ASSEMBLER__

/* Keyboard output port list */
#define KB_OUT_PORT_LIST GPIO_D

/* Single I2C port, where the EC is the master. */
#define I2C_PORT_MASTER 0
#define I2C_PORT_BATTERY I2C_PORT_MASTER
#define I2C_PORT_CHARGER I2C_PORT_MASTER

/* Timer selection */
#define TIM_CLOCK_MSB 2
#define TIM_CLOCK_LSB 9
#define TIM_WATCHDOG  4

/* PWM signal */
enum pwm_channel {
	/* Y, G, R charging LEDs */
	PWM_CH_CHG_Y = 0,
	PWM_CH_CHG_G,
	PWM_CH_CHG_R,

	/* Number of PWM channels */
	PWM_CH_COUNT
};

/* ADC signals */
enum adc_channel {
	ADC_CH_USB_VBUS_SNS = 0,
	ADC_CH_USB_DP_SNS,
	ADC_CH_USB_DN_SNS,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* GPIO signal list */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_KB_PWR_ON_L = 0,
	GPIO_SOC1V8_XPSHOLD,
	GPIO_CHARGER_INT_L,
	GPIO_USB_CHG_INT,
	GPIO_USBPD_INT_L,
	GPIO_LID_OPEN,
	GPIO_SUSPEND_L,
	GPIO_SPI1_NSS,
	GPIO_AC_PRESENT_L,
	/* Keyboard inputs */
	GPIO_KB_IN00,
	GPIO_KB_IN01,
	GPIO_KB_IN02,
	GPIO_KB_IN03,
	GPIO_KB_IN04,
	GPIO_KB_IN05,
	GPIO_KB_IN06,
	GPIO_KB_IN07,
	/* Other inputs */
	GPIO_WP_L,
	/* Outputs */
	GPIO_CHARGER_EN_L,
	GPIO_EC_INT,
	GPIO_EC_INT2,
	GPIO_ID_MUX,
	GPIO_BCHGR_OTG,
	GPIO_BCHGR_PSEL,
	GPIO_EN_PP3300,
	GPIO_ENTERING_RW,
	GPIO_BST_LED_EN,
	GPIO_I2C1_SCL,
	GPIO_I2C1_SDA,
	GPIO_CHG_LED_Y,
	GPIO_CHG_LED_G,
	GPIO_CHG_LED_R,
	GPIO_PMIC_PWRON,
	GPIO_KB_OUT00,
	GPIO_KB_OUT01,
	GPIO_KB_OUT02,
	GPIO_KB_OUT03,
	GPIO_KB_OUT04,
	GPIO_KB_OUT05,
	GPIO_KB_OUT06,
	GPIO_KB_OUT07,
	GPIO_KB_OUT08,
	GPIO_KB_OUT09,
	GPIO_KB_OUT10,
	GPIO_KB_OUT11,
	GPIO_KB_OUT12,
	/* Unimplemented */
	GPIO_I2C2_SCL,
	GPIO_I2C2_SDA,
	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */

/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Firefly board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#define CONFIG_UART_CONSOLE 1

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_ADC
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_BOARD_PRE_INIT
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_TASK_PROFILING

/* I2C ports configuration */
#define I2C_PORT_SLAVE  0

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC     3

/* GPIO signal list */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_VBUS_WAKE = 0,
	GPIO_SW_PP20000,
	GPIO_SW_PP12000,
	GPIO_SW_PP5000,

	/* PD RX/TX */
	GPIO_USB_CC1_PD,
	GPIO_PD_REF1,
	GPIO_USB_CC2_PD,
	GPIO_PD_REF2,
	GPIO_PD_CC1_TX_EN,
	GPIO_PD_CC2_TX_EN,
	GPIO_PD_CLK_OUT,
	GPIO_PD_CC1_TX_DATA,
	GPIO_PD_CC2_TX_DATA,
	GPIO_PD_CLK_IN,

	/* CCx device pull-downs */
	GPIO_PD_CC1_DEVICE,
	GPIO_PD_CC2_DEVICE,

	/* ADCs */
	GPIO_VBUS_SENSE,

	/* LEDs control */
	GPIO_LED_PP20000,
	GPIO_LED_PP12000,
	GPIO_LED_PP5000,

	/* Slave I2C */
	GPIO_I2C_INT_L,
	GPIO_I2C_SCL,
	GPIO_I2C_SDA,

	/* Test points */
	GPIO_TP_A8,
	GPIO_TP_A13,
	GPIO_TP_A14,
	GPIO_TP_B15,
	GPIO_TP_C14,
	GPIO_TP_C15,
	GPIO_TP_F0,
	GPIO_TP_F1,

	/* Unimplemented signals we emulate */
	GPIO_ENTERING_RW,
	GPIO_WP_L,
	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

/* ADC signal */
enum adc_channel {
	ADC_CH_CC1_PD = 0,
	ADC_CH_CC2_PD,
	ADC_CH_VBUS_SENSE,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */

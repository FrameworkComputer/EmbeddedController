/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Snoball board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#define CONFIG_UART_CONSOLE 1
/* Use DMA channels 2 + 3 (rather than default 4 + 5) */
#define CONFIG_UART_TX_DMA_CH STM32_DMAC_CH2
#define CONFIG_UART_RX_DMA_CH STM32_DMAC_CH3

#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
/* TODO: Consider disabling PD communication in RO */
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_DYNAMIC_SRC_CAP
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_LOG_SIZE 1024
#define CONFIG_USB_PD_PORT_COUNT 3
#define CONFIG_USB_PD_TCPM_FUSB302

#define CONFIG_ADC
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_PWM
#undef CONFIG_LID_SWITCH
#undef CONFIG_WATCHDOG_HELP

/* USB configuration */
#define CONFIG_USB_PID 0x5019
#define CONFIG_USB_BCD_DEV 0x0001 /* v 0.01 */

#define CONFIG_HIBERNATE
#define CONFIG_HIBERNATE_WAKEUP_PINS STM32_PWR_CSR_EWUP6

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK_MSB 3
#define TIM_CLOCK_LSB 1
#define TIM_ADC 14

#include "gpio_signal.h"

/* ADC signals */
enum adc_channel {
	ADC_C0_CS,
	ADC_C1_CS,
	ADC_C2_CS,
	ADC_C0_VS,
	ADC_C1_VS,
	ADC_C2_VS,
	ADC_VBUCK,
	ADC_TEMP,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_PD1 = 0,
	PWM_PD2,
	PWM_PD3,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum board_src_cap {
	SRC_CAP_5V = 0,
	SRC_CAP_12V,
	SRC_CAP_20V,
};

#define PD_DEFAULT_STATE PD_STATE_SRC_DISCONNECTED

/* delay necessary for the voltage transition on the power supply */
/* TODO: Tune these parameters appropriately for snoball */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  50000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 50000 /* us */

void board_reset_pd_mcu(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */

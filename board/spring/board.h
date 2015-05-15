/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Spring board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 16 MHz SYSCLK clock frequency */
#define CPU_CLOCK 16000000


/* Debug features */
#undef  CONFIG_TASK_PROFILING

/* Optional features */
#define CONFIG_ADC
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_CHARGER_TPS65090
#define CONFIG_CHIPSET_GAIA
#define CONFIG_CMD_BATDEBUG
#define CONFIG_CMD_ILIM
#define CONFIG_CMD_PMU
#define CONFIG_CONSOLE_RESTRICTED_INPUT
#define CONFIG_EXTPOWER_SPRING
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_I2C
#define CONFIG_I2C_PASSTHROUGH
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_LOW_POWER_IDLE	/* Use STOP mode when we have nothing to do */
#define CONFIG_LED_DRIVER_LP5562
#define CONFIG_PMU_HARD_RESET
#define CONFIG_PMU_TPS65090
#define CONFIG_PWM
#define CONFIG_USB_SWITCH_TSU6721
#define CONFIG_VBOOT_HASH

#ifndef __ASSEMBLER__

/* Keyboard output port list */
#define KB_OUT_PORT_LIST GPIO_B, GPIO_C

/* Charging */
#define I2C_PORT_MASTER 0
#define I2C_PORT_BATTERY I2C_PORT_MASTER
#define I2C_PORT_CHARGER I2C_PORT_MASTER
#define I2C_PORT_SLAVE 1

/* Low battery threshold. In mAh. */
#define BATTERY_AP_OFF_LEVEL 1

/* Timer selection */
#define TIM_CLOCK_MSB 2
#define TIM_CLOCK_LSB 4
#define TIM_WATCHDOG  1

/* ADC signal */
enum adc_channel {
	ADC_CH_USB_VBUS_SNS = 0,
	ADC_CH_USB_DP_SNS,
	ADC_CH_USB_DN_SNS,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* PWM signal */
enum pwm_channel {
	PWM_CH_ILIM = 0,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */

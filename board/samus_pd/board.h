/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* samus_pd board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#undef  CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1

/* Optional features */
#define CONFIG_ADC
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_HIBERNATE_WAKEUP_PINS (STM32_PWR_CSR_EWUP3|STM32_PWR_CSR_EWUP8)
#define CONFIG_HW_CRC
#define CONFIG_I2C
#undef  CONFIG_LID_SWITCH
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_STM_HWTIMER32
#undef  CONFIG_TASK_PROFILING
#define CONFIG_USB_POWER_DELIVERY
#undef  CONFIG_USB_PD_COMM_ENABLED
#define CONFIG_USB_PD_COMM_ENABLED 0
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_FLASH_ERASE_CHECK
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USBC_SS_MUX
#define CONFIG_USB_SWITCH_TSU6721
#define CONFIG_VBOOT_HASH
#undef  CONFIG_WATCHDOG_HELP

/* I2C ports configuration */
#define I2C_PORT_MASTER 1
#define I2C_PORT_SLAVE  0
#define I2C_PORT_EC I2C_PORT_SLAVE

/* slave address for host commands */
#ifdef HAS_TASK_HOSTCMD
#define CONFIG_HOSTCMD_I2C_SLAVE_ADDR CONFIG_USB_PD_I2C_SLAVE_ADDR
#endif

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC     3

#include "gpio_signal.h"

/* ADC signal */
enum adc_channel {
	ADC_C0_CC1_PD = 0,
	ADC_C1_CC1_PD,
	ADC_C0_CC2_PD,
	ADC_C1_CC2_PD,
	ADC_BOOSTIN,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* Called when we receive battery level info from the EC. */
void board_update_battery_soc(int soc);

/* Get the last received battery level. */
int board_get_battery_soc(void);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */

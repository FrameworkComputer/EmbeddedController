/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* lucid board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#undef  CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1

/* Optional features */
#define CONFIG_ADC
#undef  CONFIG_ADC_WATCHDOG
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_BQ24773
#define CONFIG_CHARGER_ILIM_PIN_DISABLED
#define CONFIG_CHARGER_INPUT_CURRENT 500
#define CONFIG_CHARGER_PROFILE_OVERRIDE
#define CONFIG_CHARGER_SENSE_RESISTOR 5
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#undef  CONFIG_CMD_I2C_SCAN
#undef  CONFIG_CMD_IDLE_STATS
#undef  CONFIG_CMD_SHMEM
#undef  CONFIG_CMD_TIMERINFO
#undef  CONFIG_CONSOLE_CMDHELP
#undef  CONFIG_CONSOLE_HISTORY
#undef  CONFIG_DEBUG_ASSERT
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_HIBERNATE_WAKEUP_PINS (STM32_PWR_CSR_EWUP2)
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_I2C_SLAVE
#define CONFIG_LED_COMMON
#undef  CONFIG_LID_SWITCH
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LTO
#define CONFIG_RSA
#define CONFIG_RWSIG
#define CONFIG_SHA256
#define CONFIG_STM_HWTIMER32
#define CONFIG_STM32_CHARGER_DETECT
#undef  CONFIG_TASK_PROFILING
#define CONFIG_TEMP_SENSOR
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_PORT_COUNT 1
#define CONFIG_USB_PD_TCPC
#define CONFIG_USB_PD_TCPM_STUB
#define CONFIG_USB_PD_VBUS_DETECT_GPIO
#undef  CONFIG_WATCHDOG_HELP

/* Use PSTATE embedded in the RO image, not in its own erase block */
#undef  CONFIG_FLASH_PSTATE_BANK
#undef  CONFIG_FW_PSTATE_SIZE
#define CONFIG_FW_PSTATE_SIZE 0

/* I2C ports configuration */
#define I2C_PORT_MASTER 1
#define I2C_PORT_SLAVE  0
#define I2C_PORT_EC I2C_PORT_SLAVE
#define I2C_PORT_CHARGER I2C_PORT_MASTER
#define I2C_PORT_BATTERY I2C_PORT_MASTER

/* slave address for host commands */
#define CONFIG_HOSTCMD_I2C_SLAVE_ADDR 0x3c

/* Allow dangerous commands */
#define CONFIG_SYSTEM_UNLOCKED

/* No Write-protect GPIO, force the write-protection */
#define CONFIG_WP_ALWAYS

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC     3

#include "gpio_signal.h"

/* ADC signal */
enum adc_channel {
	ADC_C0_CC1_PD = 0,
	ADC_C0_CC2_PD,
	ADC_VBUS,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY,

	TEMP_SENSOR_COUNT
};

#define PD_DEFAULT_STATE PD_STATE_SNK_DISCONNECTED

/* we are never a source : don't care about power supply */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  0 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 0 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 10000
#define PD_MAX_POWER_MW       60000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_VOLTAGE_MV     20000

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */

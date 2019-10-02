/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* samus_pd board configuration */

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
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGE_RAMP_SW
#undef  CONFIG_CMD_ADC
#undef  CONFIG_CMD_CHARGE_SUPPLIER_INFO
#undef  CONFIG_CMD_CRASH
#undef  CONFIG_CMD_HASH
#undef  CONFIG_CMD_HCDEBUG
#undef  CONFIG_CMD_I2C_SCAN
#undef  CONFIG_CMD_I2C_XFER
/* Minimum ilim = 500 mA */
#define CONFIG_CHARGER_INPUT_CURRENT PWM_0_MA
#undef  CONFIG_CMD_IDLE_STATS
#undef  CONFIG_CMD_MD
#undef  CONFIG_CMD_SHMEM
#undef  CONFIG_CMD_TIMERINFO
#define CONFIG_COMMON_GPIO_SHORTNAMES
#undef  CONFIG_CONSOLE_CMDHELP
#undef  CONFIG_CONSOLE_HISTORY
#undef  CONFIG_DEBUG_ASSERT
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_HIBERNATE_WAKEUP_PINS (STM32_PWR_CSR_EWUP3|STM32_PWR_CSR_EWUP8)
#define CONFIG_HOSTCMD_ALIGNED
#undef  CONFIG_HOSTCMD_EVENTS
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_I2C_SLAVE
#undef  CONFIG_LID_SWITCH
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LTO
#undef  CONFIG_PWM
#define CONFIG_STM_HWTIMER32
#undef  CONFIG_TASK_PROFILING
#define CONFIG_USB_CHARGER
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CHECK_MAX_REQUEST_ALLOWED
#define CONFIG_USB_PD_COMM_LOCKED
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_FLASH_ERASE_CHECK
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_TCPC
#define CONFIG_USB_PD_TCPM_STUB
#define CONFIG_USB_PD_VBUS_DETECT_GPIO
#define CONFIG_BC12_DETECT_PI3USB9281
#define CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT 2
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_VBOOT_HASH
#undef  CONFIG_WATCHDOG_HELP

/* Use PSTATE embedded in the RO image, not in its own erase block */
#undef  CONFIG_FLASH_PSTATE_BANK
#undef  CONFIG_FW_PSTATE_SIZE
#define CONFIG_FW_PSTATE_SIZE 0

/* I2C ports configuration */
#define I2C_PORT_MASTER 1
#define I2C_PORT_SLAVE  0
#define I2C_PORT_EC I2C_PORT_SLAVE
#define I2C_PORT_PERICOM I2C_PORT_MASTER

/* slave address for host commands */
#ifdef HAS_TASK_HOSTCMD
#define CONFIG_HOSTCMD_I2C_SLAVE_ADDR_FLAGS CONFIG_USB_PD_I2C_SLAVE_ADDR_FLAGS
#endif

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
	ADC_VBUS,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_ILIM = 0,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

/* Standard-current Rp */
#define PD_SRC_VNC           PD_SRC_DEF_VNC_MV
#define PD_SRC_RD_THRESHOLD  PD_SRC_DEF_RD_THRESH_MV

/*
 * delay to turn on the power supply max is ~16ms.
 * delay to turn off the power supply max is about ~180ms.
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* delay to turn on/off vconn */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW       60000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_VOLTAGE_MV     20000

/* Charge current limit min / max, based on PWM duty cycle */
#define PWM_0_MA	500
#define PWM_100_MA	4000

/* Map current in milli-amps to PWM duty cycle percentage */
#define MA_TO_PWM(curr) (((curr) - PWM_0_MA) * 100 / (PWM_100_MA - PWM_0_MA))

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */

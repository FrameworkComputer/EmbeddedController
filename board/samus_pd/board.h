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
#define CONFIG_CHARGE_MANAGER
#undef CONFIG_CMD_HASH
#undef CONFIG_CMD_TYPEC
/* Minimum ilim = 500 mA */
#define CONFIG_CHARGER_INPUT_CURRENT PWM_0_MA
#undef CONFIG_CMD_IDLE_STATS
#define CONFIG_COMMON_GPIO_SHORTNAMES
#undef  CONFIG_CONSOLE_CMDHELP
#undef  CONFIG_CONSOLE_HISTORY
#undef CONFIG_DEBUG_ASSERT
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_HIBERNATE_WAKEUP_PINS (STM32_PWR_CSR_EWUP3|STM32_PWR_CSR_EWUP8)
#undef  CONFIG_HOSTCMD_EVENTS
#define CONFIG_HW_CRC
#define CONFIG_I2C
#undef  CONFIG_LID_SWITCH
#define CONFIG_LOW_POWER_IDLE
#undef  CONFIG_PWM
#define CONFIG_STM_HWTIMER32
#undef  CONFIG_TASK_PROFILING
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CHECK_MAX_REQUEST_ALLOWED
#undef  CONFIG_USB_PD_COMM_ENABLED
#define CONFIG_USB_PD_COMM_ENABLED 0
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_FLASH_ERASE_CHECK
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_LOG_SIZE 512
#define CONFIG_USB_SWITCH_PI3USB9281
#undef  CONFIG_USB_SWITCH_PI3USB9281_MUX_GPIO
#define CONFIG_USB_SWITCH_PI3USB9281_MUX_GPIO GPIO_USB_C_BC12_SEL
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_VCONN
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

/* Maximum number of deferrable functions */
#undef  DEFERRABLE_MAX_COUNT
#define DEFERRABLE_MAX_COUNT 9

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

/* Charge suppliers */
enum charge_supplier {
	CHARGE_SUPPLIER_PD,
	CHARGE_SUPPLIER_TYPEC,
	CHARGE_SUPPLIER_BC12_DCP,
	CHARGE_SUPPLIER_BC12_CDP,
	CHARGE_SUPPLIER_BC12_SDP,
	CHARGE_SUPPLIER_PROPRIETARY,
	CHARGE_SUPPLIER_OTHER,
	CHARGE_SUPPLIER_COUNT
};

/* supplier_priority table defined in board.c */
extern const int supplier_priority[];

/* Charge current limit min / max, based on PWM duty cycle */
#define PWM_0_MA	500
#define PWM_100_MA	4000

/* Map current in milli-amps to PWM duty cycle percentage */
#define MA_TO_PWM(curr) (((curr) - PWM_0_MA) * 100 / (PWM_100_MA - PWM_0_MA))

/* Called when we receive battery level info from the EC. */
void board_update_battery_soc(int soc);

/* Get the last received battery level. */
int board_get_battery_soc(void);

/* Send host event to AP */
void pd_send_host_event(int mask);

/* Update the state of the USB data switches */
void set_usb_switches(int port, int open);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */

/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Honeybuns board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#define CONFIG_UART_CONSOLE 1

/* Optional features */
#define CONFIG_ADC
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#undef CONFIG_LID_SWITCH
#define CONFIG_RSA
#define CONFIG_RWSIG
#define CONFIG_SHA256
#define CONFIG_STM_HWTIMER32
#undef CONFIG_TASK_PROFILING
#define CONFIG_USB
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#undef CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CUSTOM_VDM
#undef CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_HW_DEV_ID_BOARD_MAJOR USB_PD_HW_DEV_ID_HONEYBUNS
#define CONFIG_USB_PD_HW_DEV_ID_BOARD_MINOR 0
#define CONFIG_USB_PD_IDENTITY_HW_VERS 1
#define CONFIG_USB_PD_IDENTITY_SW_VERS 1
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_PORT_COUNT 1
#define CONFIG_USB_PD_TCPC
#define CONFIG_USB_PD_TCPM_STUB
#define CONFIG_USB_PD_VBUS_DETECT_GPIO
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_VCONN
#undef CONFIG_WATCHDOG_HELP

/* I2C ports configuration */
#define I2C_PORT_MASTER 0

/* USB configuration */
#define CONFIG_USB_PID 0x5015
#define CONFIG_USB_BCD_DEV 0x0001 /* v 0.01 */
/* By default, enable all console messages excepted USB */
#define CC_DEFAULT     (CC_ALL & ~CC_MASK(CC_USB))

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
	ADC_CH_CC1_PD = 0,
	ADC_CH_VIN_DIV_P,
	ADC_CH_VIN_DIV_N,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_VERSION,
	USB_STR_BB_URL,

	USB_STR_COUNT
};

/* 3.0A Rp */
#define PD_SRC_VNC            PD_SRC_3_0_VNC_MV
#define PD_SRC_RD_THRESHOLD   PD_SRC_3_0_RD_THRESH_MV

/* we are acting only as a source */
#define PD_DEFAULT_STATE PD_STATE_SRC_DISCONNECTED

/* delay necessary for the voltage transition on the power supply */
/* TODO (code.google.com/p/chrome-os-partner/issues/detail?id=37078)
 * Need to measure these and adjust for honeybuns.
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  50000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 50000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 1000
#define PD_MAX_POWER_MW       60000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_VOLTAGE_MV     20000

/* Enable/disable USB Hub */
void hx3_enable(int enable);

/* DisplayPort hotplug detection interrupt */
void hpd_event(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_COUNT        0

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL		0
#define USB_EP_COUNT		1

#endif /* __CROS_EC_BOARD_H */

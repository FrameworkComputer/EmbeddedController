/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32F072-discovery board based USB PD evaluation configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART2 (PA14/PA15) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2

#define CONFIG_LTO

/* Optional features */
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER
/* USB Power Delivery configuration */
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV1
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CUSTOM_PDO
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_PORT_MAX_COUNT 1
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_PD_TCPM_ANX7447
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USBC_SS_MUX

#undef CONFIG_USB_PD_INITIAL_DRP_STATE
#define CONFIG_USB_PD_INITIAL_DRP_STATE PD_DRP_TOGGLE_ON

#undef CONFIG_USB_PD_PULLUP
#define CONFIG_USB_PD_PULLUP TYPEC_RP_USB

/* fake board specific type-C power constants */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 650000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 60000
#define PD_MAX_CURRENT_MA 3000
#define PD_MAX_VOLTAGE_MV 20000

/* I2C master port connected to the TCPC */
#define I2C_PORT_TCPC 0
#define I2C_PORT_PD_MCU 0

/* Timer selection */

#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
/* delay to turn on/off vconn */

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_PID 0x500f
#define CONFIG_USB_CONSOLE

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_CONSOLE 0
#define USB_IFACE_COUNT 1

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL 0
#define USB_EP_CONSOLE 1
#define USB_EP_COUNT 2

/* Remove console commands / features for flash / RAM savings */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_CONSOLE_HISTORY
#undef CONFIG_HIBERNATE
#undef CONFIG_CMD_CRASH
#undef CONFIG_CMD_CRASH_NESTED

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2

#include "gpio_signal.h"

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_VERSION,
	USB_STR_CONSOLE_NAME,

	USB_STR_COUNT
};

void board_reset_pd_mcu(void);

#endif /* !__ASSEMBLER__ */
#endif /* __BOARD_H */

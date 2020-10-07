/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fusb307bgevb configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H


/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* Debug Congifuation */
#define DEBUG_GET_CC
#define DEBUG_ROLE_CTRL_UPDATES

/* Enable USART1,3,4 and USB streams */
#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USART1
#define CONFIG_STREAM_USART4
#define CONFIG_STREAM_USB
#define CONFIG_CMD_USART_INFO

/* the UART console is on USART2 (PA14/PA15) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_I2C_MASTER

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_PID 0x1234
#define CONFIG_USB_CONSOLE

/* USB Power Delivery configuration */
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV1
#define CONFIG_USB_PD_PORT_MAX_COUNT 1
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_PD_REV30
#define CONFIG_USB_PD_DECODE_SOP
#define CONFIG_USBC_VCONN
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_TCPM_FUSB307

/* delay to turn on/off vconn */
#define PD_VCONN_SWAP_DELAY 5000 /* us */
/* Define operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_VOLTAGE_MV 20000
#define PD_MAX_CURRENT_MA 3000
#define PD_MAX_POWER_MW       ((PD_MAX_VOLTAGE_MV * PD_MAX_CURRENT_MA) / 1000)

/* Degine board specific type-C power constants */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 160000  /* us */

/* I2C master port connected to the TCPC */
#define I2C_PORT_TCPC 1

/* LCD Configuration */
#define LCD_SLAVE_ADDR 0x27

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_STREAM  0
#define USB_IFACE_GPIO    1
#define USB_IFACE_SPI     2
#define USB_IFACE_CONSOLE 3
#define USB_IFACE_COUNT   4

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL 0
#define USB_EP_STREAM  1
#define USB_EP_GPIO    2
#define USB_EP_SPI     3
#define USB_EP_CONSOLE 4
#define USB_EP_COUNT   5

/* Enable control of GPIOs over USB */
#define CONFIG_USB_GPIO

#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH

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
	USB_STR_STREAM_NAME,
	USB_STR_CONSOLE_NAME,

	USB_STR_COUNT
};

void board_reset_pd_mcu(void);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */

/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hoho dongle configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#define CONFIG_UART_CONSOLE 1

/* Optional features */
#define CONFIG_ADC
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_CMD_SPI_FLASH
#define CONFIG_HW_CRC
#define CONFIG_RSA
#define CONFIG_RWSIG
#define CONFIG_RWSIG_TYPE_USBPD1
#define CONFIG_SHA256_SW
/* TODO(tbroch) Re-enable once STM spi controller can be inhibited at boot so it
   doesn't interfere with HDMI loading its f/w */
#undef CONFIG_SPI_FLASH
#define CONFIG_SPI_CS_GPIO GPIO_PD_MCDP_SPI_CS_L
#define CONFIG_USB
#define CONFIG_USB_BOS
#define CONFIG_USB_INHIBIT_CONNECT
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV1
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_CUSTOM_PDO
#define CONFIG_USB_PD_HW_DEV_ID_BOARD_MAJOR USB_PD_HW_DEV_ID_HOHO
#define CONFIG_USB_PD_HW_DEV_ID_BOARD_MINOR 2
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_FLASH
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_IDENTITY_HW_VERS 1
#define CONFIG_USB_PD_IDENTITY_SW_VERS 1
#define CONFIG_USB_PD_LOGGING
#undef CONFIG_EVENT_LOG_SIZE
#define CONFIG_EVENT_LOG_SIZE 256
#define CONFIG_USB_PD_PORT_MAX_COUNT 1
#define CONFIG_USB_PD_TCPC
#define CONFIG_USB_PD_TCPM_STUB
#define CONFIG_USB_PD_VBUS_DETECT_NONE
/* mcdp2850 serial interface */
#define CONFIG_MCDP28X0 usart3_hw
#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USART3
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_TASK_PROFILING

/* USB configuration */
#define CONFIG_USB_PID 0x5010
#define CONFIG_USB_BCD_DEV 0x0001 /* v 0.01 */

/* No Write-protect GPIO, force the write-protection */
#define CONFIG_WP_ALWAYS
#define CONFIG_FLASH_READOUT_PROTECTION

/* Inform VIF generator that this board is an Alt Mode Adapter */
#define CONFIG_USB_ALT_MODE_ADAPTER

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC 3

#include "gpio_signal.h"

/* ADC signal */
enum adc_channel {
	ADC_CH_CC1_PD = 0,
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

/* we are never a source : don't care about power supply */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 0 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 0 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 1000
#define PD_MAX_POWER_MW 1500
#define PD_MAX_CURRENT_MA 300
#define PD_MAX_VOLTAGE_MV 5000

#endif /* !__ASSEMBLER__ */

/* USB Device class */
#define USB_DEV_CLASS USB_CLASS_BILLBOARD

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_COUNT 0

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL 0
#define USB_EP_COUNT 1

#endif /* __CROS_EC_BOARD_H */

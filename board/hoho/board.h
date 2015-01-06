/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hoho dongle configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#define CONFIG_UART_CONSOLE 1

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_ADC
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_CMD_SPI_FLASH
#define CONFIG_HW_CRC
#define CONFIG_RSA
#define CONFIG_RWSIG
#define CONFIG_SHA256
/* TODO(tbroch) Re-enable once STM spi master can be inhibited at boot so it
   doesn't interfere with HDMI loading its f/w */
#undef CONFIG_SPI_FLASH
#define CONFIG_SPI_FLASH_SIZE 1048576
#define CONFIG_SPI_MASTER_PORT 2
#define CONFIG_SPI_CS_GPIO GPIO_PD_MCDP_SPI_CS_L
#define CONFIG_USB
#define CONFIG_USB_BOS
#define CONFIG_USB_INHIBIT_CONNECT
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_HW_DEV_ID_BOARD_MAJOR USB_PD_HW_DEV_ID_HOHO
#define CONFIG_USB_PD_HW_DEV_ID_BOARD_MINOR 2
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_FLASH
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_IDENTITY_HW_VERS 1
#define CONFIG_USB_PD_IDENTITY_SW_VERS 1
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_LOG_SIZE 256
#define CONFIG_USB_PD_NO_VBUS_DETECT
/* mcdp2850 serial interface */
#define CONFIG_MCDP28X0 usart3_hw
#define CONFIG_STREAM
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

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC     3

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

#endif /* !__ASSEMBLER__ */

/* USB Device class */
#define USB_DEV_CLASS USB_CLASS_BILLBOARD

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_COUNT     0

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL   0
#define USB_EP_COUNT     1

#endif /* __BOARD_H */

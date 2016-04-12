/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Features that we don't want */
#undef CONFIG_CMD_LID_ANGLE
#undef CONFIG_CMD_POWERINDEBUG
#undef CONFIG_DMA_DEFAULT_HANDLERS
#undef CONFIG_FMAP
#undef CONFIG_HIBERNATE
#undef CONFIG_LID_SWITCH

/* Flash configuration */
#undef CONFIG_FLASH_PSTATE
/* TODO(crosbug.com/p/44745): Bringup only! Do the right thing for real! */
#define CONFIG_WP_ALWAYS
/* TODO(crosbug.com/p/44745): For debugging only */
#define CONFIG_CMD_FLASH

/* Go to sleep when nothing else is happening */
#define CONFIG_LOW_POWER_IDLE

/* USB configuration */
#define CONFIG_USB
#define CONFIG_USB_HID
#define CONFIG_USB_CONSOLE
#define CONFIG_USB_SELECT_PHY

#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USB

#define CONFIG_USB_PID 0x5014

/* Enable SPI Slave (SPS) module */
#define CONFIG_SPS
#define CONFIG_TPM_SPS

/* We don't need to send events to the AP */
#undef  CONFIG_HOSTCMD_EVENTS

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* Not using software sync, so verify RW signature instead */
#define CONFIG_RSA
#define CONFIG_SHA256

#define CONFIG_SPS_TEST

/* Include crypto stuff, both software and hardware. */
#define CONFIG_DCRYPTO
#define CONFIG_SHA1
#define CONFIG_SHA256

#ifndef __ASSEMBLER__

#include "gpio_signal.h"

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_VERSION,
	USB_STR_CONSOLE_NAME,
	USB_STR_BLOB_NAME,
	USB_STR_HID_NAME,
	USB_STR_AP_NAME,
	USB_STR_EC_NAME,
	USB_STR_UPGRADE_NAME,

	USB_STR_COUNT
};

#endif /* !__ASSEMBLER__ */

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_CONSOLE 0
#define USB_IFACE_HID     1
#define USB_IFACE_AP      2
#define USB_IFACE_EC      3
#define USB_IFACE_UPGRADE 4
#define USB_IFACE_COUNT   5

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL   0
#define USB_EP_CONSOLE   1
#define USB_EP_HID       2
#define USB_EP_AP        3
#define USB_EP_EC        4
#define USB_EP_UPGRADE   5
#define USB_EP_COUNT     6

/* UART indexes (use define rather than enum to expand them) */
#define UART_CR50	0
#define UART_AP		1
#define UART_EC		2

#define UARTN UART_CR50

/*
 * This would be a low hanging fruit if there is a need to reduce memory
 * footprint. Having a large buffer helps not to drop debug outputs generated
 * before console is initialized, but this is not really necessary in a
 * production device.
 */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

#define CC_DEFAULT     (CC_ALL & ~CC_MASK(CC_TPM))

/*
 * Let's be on the lookout for stack overflow, while debugging.
 *
 * TODO(vbendeb): remove this before finalizing the code.
 */
#define CONFIG_DEBUG_STACK_OVERFLOW
#define CONFIG_RW_B

/* Firmware upgrade options. */
#define CONFIG_NON_HC_FW_UPDATE
#define CONFIG_USB_FW_UPDATE

#endif /* __CROS_EC_BOARD_H */

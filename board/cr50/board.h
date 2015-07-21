/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Features that we don't want just yet */
#undef CONFIG_CMD_LID_ANGLE
#undef CONFIG_CMD_POWERINDEBUG
#undef CONFIG_DMA_DEFAULT_HANDLERS
#undef CONFIG_FLASH
#undef CONFIG_FMAP
#undef CONFIG_HIBERNATE
#undef CONFIG_LID_SWITCH

#if !defined(CHIP_VARIANT_CR50_A1)

/* USB configuration */
#define CONFIG_USB
#define CONFIG_USB_HID
#define CONFIG_USB_BLOB

#define CONFIG_USB_PID 0x5014

/* Enable SPI Slave (SPS) module */
#define CONFIG_SPS
#define CONFIG_HOSTCMD_SPS
#define CONFIG_TPM_SPS

/* We don't need to send events to the AP */
#undef  CONFIG_HOSTCMD_EVENTS

#endif	/* not A1 */

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* Not using software sync, so verify RW signature instead */
#define CONFIG_RWSIG
#define CONFIG_RSA
#define CONFIG_SHA256

#define CONFIG_SPS_TEST

#ifndef __ASSEMBLER__

#include "gpio_signal.h"

/* user button interrupt handler */
void button_event(enum gpio_signal signal);

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_VERSION,
	USB_STR_CONSOLE_NAME,
	USB_STR_BLOB_NAME,
	USB_STR_HID_NAME,

	USB_STR_COUNT
};

#endif /* !__ASSEMBLER__ */

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_BLOB    0
#define USB_IFACE_HID     1
#define USB_IFACE_COUNT   2

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL   0
#define USB_EP_BLOB      1
#define USB_EP_HID       2
#define USB_EP_COUNT     3

/*
 * This would be a low hanging fruit if there is a need to reduce memory
 * footprint. Having a large buffer helps not to drop debug outputs generated
 * before console is initialized, but this is not really necessary in a
 * production device.
 */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

#endif /* __CROS_EC_BOARD_H */

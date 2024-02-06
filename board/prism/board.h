/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Prism configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Prism doesn't use WP GPIO, set WP enabled */
#ifdef SECTION_IS_RO
#define CONFIG_WP_ALWAYS
#endif

/* TODO: May define FLASH_PSTATE_LOCKED prior to building MP FW. */
#undef CONFIG_FLASH_PSTATE_LOCKED

/* USB ID for Prism */
#define CONFIG_USB_PID 0x5022

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* For RGB keyboard control */
#ifdef SECTION_IS_RW
#define CONFIG_KEYBOARD_BACKLIGHT
#define CONFIG_RGB_KEYBOARD
#define GPIO_RGBKBD_SDB_L GPIO_SDB_L
#define GPIO_RGBKBD_POWER GPIO_L_POWER
#define CONFIG_LED_DRIVER_IS31FL3743B
#define SPI_RGB0_DEVICE_ID 0
#define SPI_RGB1_DEVICE_ID 1
#define RGB_GRID0_COL 11
#define RGB_GRID0_ROW 6
#define RGB_GRID1_COL 11
#define RGB_GRID1_ROW 6

/* Enable control of SPI over USB */
#define CONFIG_SPI_CONTROLLER
#define CONFIG_STM32_SPI1_CONTROLLER
/* Enable SPI controller xfer command */
#define CONFIG_CMD_SPI_XFER

#define CONFIG_SPI_RGB_PORT 0

#define CONFIG_IS31FL3743B_LATE_INIT

#endif /* SECTION_IS_RW */

/*
 * Flash layout: we redefine the sections offsets and sizes as we want to
 * include a rollback region, and will use RO/RW regions of different sizes.
 */
#undef _IMAGE_SIZE
#undef CONFIG_ROLLBACK_OFF
#undef CONFIG_ROLLBACK_SIZE
#undef CONFIG_FLASH_PSTATE
#undef CONFIG_FW_PSTATE_SIZE
#undef CONFIG_FW_PSTATE_OFF
#undef CONFIG_SHAREDLIB_SIZE
#undef CONFIG_RO_MEM_OFF
#undef CONFIG_RO_STORAGE_OFF
#undef CONFIG_RO_SIZE
#undef CONFIG_RW_MEM_OFF
#undef CONFIG_RW_STORAGE_OFF
#undef CONFIG_RW_SIZE
#undef CONFIG_EC_PROTECTED_STORAGE_OFF
#undef CONFIG_EC_PROTECTED_STORAGE_SIZE
#undef CONFIG_EC_WRITABLE_STORAGE_OFF
#undef CONFIG_EC_WRITABLE_STORAGE_SIZE
#undef CONFIG_WP_STORAGE_OFF
#undef CONFIG_WP_STORAGE_SIZE

#define CONFIG_FLASH_PSTATE
/* Do not use a dedicated PSTATE bank */
#undef CONFIG_FLASH_PSTATE_BANK

#define CONFIG_SHAREDLIB_SIZE 0

#define CONFIG_RO_MEM_OFF 0
#define CONFIG_RO_STORAGE_OFF 0
#define CONFIG_RO_SIZE (44 * 1024)

/* EC rollback protection block */
#define CONFIG_ROLLBACK_OFF (CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE)
#define CONFIG_ROLLBACK_SIZE CONFIG_FLASH_BANK_SIZE

#define CONFIG_RW_MEM_OFF (CONFIG_ROLLBACK_OFF + CONFIG_ROLLBACK_SIZE)
#define CONFIG_RW_STORAGE_OFF 0
#define CONFIG_RW_SIZE (CONFIG_FLASH_SIZE_BYTES - CONFIG_RW_MEM_OFF)

#define CONFIG_EC_PROTECTED_STORAGE_OFF CONFIG_RO_MEM_OFF
#define CONFIG_EC_PROTECTED_STORAGE_SIZE CONFIG_RO_SIZE
#define CONFIG_EC_WRITABLE_STORAGE_OFF CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_SIZE CONFIG_RW_SIZE

#define CONFIG_WP_STORAGE_OFF CONFIG_EC_PROTECTED_STORAGE_OFF
#define CONFIG_WP_STORAGE_SIZE CONFIG_EC_PROTECTED_STORAGE_SIZE

/* The UART console is on USART1 (PA9/PA10) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1

#undef CONFIG_UART_TX_BUF_SIZE
/* Has to be power of two */
#define CONFIG_UART_TX_BUF_SIZE 4096

/* Optional features */
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LTO
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_MATH_UTIL

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_STREAM_USB
#define CONFIG_USB_UPDATE

#undef CONFIG_USB_MAXPOWER_MA
#define CONFIG_USB_MAXPOWER_MA 100

#define CONFIG_USB_REMOTE_WAKEUP
#define CONFIG_USB_SUSPEND

#define CONFIG_USB_SERIALNO
/* Replaced at runtime (board_read_serial) by chip unique-id-based number. */
#define DEFAULT_SERIALNO "Uninitialized"

/* USB interface indexes (use define rather than enum to expand them) */
#undef CONFIG_HOSTCMD_EVENTS
#define USB_IFACE_UPDATE 0
#define CONFIG_HOST_INTERFACE_USB
#define USB_IFACE_HOSTCMD 1
#define USB_IFACE_COUNT 2

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL 0
#define USB_EP_UPDATE 1
#define USB_EP_HOSTCMD 2
#define USB_EP_COUNT 3

/* Optional features */
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_WATCHDOG_HELP

/* No need to hibernate, remove console commands that are not very useful. */
#undef CONFIG_HIBERNATE
#undef CONFIG_CONSOLE_CHANNEL
#undef CONFIG_CONSOLE_HISTORY
#undef CONFIG_CMD_GETTIME
#undef CONFIG_CMD_MD
#undef CONFIG_CMD_RW
#undef CONFIG_CMD_SHMEM
#undef CONFIG_CMD_TIMERINFO
#undef CONFIG_CMD_WAITMS

#undef CONFIG_LID_SWITCH

#define CONFIG_USB_CONSOLE_READ

#ifdef SECTION_IS_RW

#define CONFIG_CURVE25519

#define CONFIG_USB_PAIRING

#else /* SECTION_IS_RO */
/* Sign and switch to RW partition on boot. */
#define CONFIG_RWSIG
#define CONFIG_RSA
#endif

#define CONFIG_RSA_KEY_SIZE 3072
#define CONFIG_RSA_EXPONENT_3

#define CONFIG_SHA256_SW
#ifdef SECTION_IS_RO
#define CONFIG_SHA256_UNROLLED
#endif

#define CONFIG_RWSIG_TYPE_RWSIG

/*
 * Add rollback protection, and independent RW region protection.
 */
#define CONFIG_LIBCRYPTOC
#define CONFIG_ROLLBACK
#define CONFIG_ROLLBACK_SECRET_SIZE 32
#define CONFIG_ROLLBACK_SECRET_LOCAL_ENTROPY_SIZE 32
#define CONFIG_FLASH_PROTECT_RW
#ifdef SECTION_IS_RW
#undef CONFIG_ROLLBACK_UPDATE
#endif

/* Maximum current to draw. */
#define MAX_CURRENT_MA 2000
/* Maximum current/voltage to provide over OTG. */
#define MAX_OTG_CURRENT_MA 2000
#define MAX_OTG_VOLTAGE_MV 20000

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_WATCHDOG 16

#include "gpio_signal.h"

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_SERIALNO,
	USB_STR_VERSION,
	USB_STR_UPDATE_NAME,
	USB_STR_HOSTCMD_NAME,
	USB_STR_COUNT
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */

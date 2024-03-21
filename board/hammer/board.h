/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hammer configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "variants.h"

/* TODO: Remove CONFIG_SYSTEM_UNLOCKED prior to building MP FW. */
#define CONFIG_SYSTEM_UNLOCKED
/* TODO(b:63378217): Define FLASH_PSTATE_LOCKED prior to building MP FW. */
#undef CONFIG_FLASH_PSTATE_LOCKED

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

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

/* Optional features */
/*
 * TODO(b:65697962): Reenable low-power-idle on wand without breaking EC-EC
 * communication
 */
#ifndef BOARD_WAND
#define CONFIG_LOW_POWER_IDLE
#endif
#define CONFIG_LTO
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_MATH_UTIL

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_STREAM_USB
#define CONFIG_USB_UPDATE

#undef CONFIG_UPDATE_PDU_SIZE
#if defined(BOARD_WAND) || defined(VARIANT_HAMMER_TP_LARGE_PAGE)
/* Wand/Zed does not have enough space to fit 4k PDU. */
#define CONFIG_UPDATE_PDU_SIZE 2048
#else
#define CONFIG_UPDATE_PDU_SIZE 4096
#endif

#undef CONFIG_USB_MAXPOWER_MA
#define CONFIG_USB_MAXPOWER_MA 100

#define CONFIG_USB_REMOTE_WAKEUP
#define CONFIG_USB_SUSPEND

#define CONFIG_USB_SERIALNO
/* Replaced at runtime (board_read_serial) by chip unique-id-based number. */
#define DEFAULT_SERIALNO ""

/* USB interface indexes (use define rather than enum to expand them) */
#ifdef SECTION_IS_RW
#define USB_IFACE_HID_KEYBOARD 0
#define USB_IFACE_UPDATE 1
#ifdef HAS_NO_TOUCHPAD
#define USB_IFACE_COUNT 2
#else /* !HAS_NO_TOUCHPAD */
#define USB_IFACE_HID_TOUCHPAD 2
/* Can be either I2C or SPI passthrough, depending on the board. */
#define USB_IFACE_I2C_SPI 3
#if defined(CONFIG_USB_ISOCHRONOUS)
#define USB_IFACE_ST_TOUCHPAD 4
#define USB_IFACE_COUNT 5
#else /* !CONFIG_USB_ISOCHRONOUS */
#define USB_IFACE_COUNT 4
#endif /* CONFIG_USB_ISOCHRONOUS */
#endif /* !HAS_NO_TOUCHPAD */
#else /* !SECTION_IS_RW */
#define USB_IFACE_UPDATE 0
#define USB_IFACE_COUNT 1
#endif /* SECTION_IS_RW */

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL 0
#define USB_EP_UPDATE 1
#ifdef SECTION_IS_RW
#define USB_EP_HID_KEYBOARD 2
#ifdef HAS_NO_TOUCHPAD
#define USB_EP_COUNT 3
#else /* !HAS_NO_TOUCHPAD */
#define USB_EP_HID_TOUCHPAD 3
/* Can be either I2C or SPI passthrough, depending on the board. */
#define USB_EP_I2C_SPI 4
#if defined(CONFIG_USB_ISOCHRONOUS)
#define USB_EP_ST_TOUCHPAD 5
#define USB_EP_ST_TOUCHPAD_INT 6
#define USB_EP_COUNT 7
#else /* !CONFIG_USB_ISOCHRONOUS */
#define USB_EP_COUNT 5
#endif /* CONFIG_USB_ISOCHRONOUS */
#endif /* !HAS_NO_TOUCHPAD */
#else /* !SECTION_IS_RW */
#define USB_EP_COUNT 2
#endif /* SECTION_IS_RW */

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

/*
 * Enlarge the allowed write / read count for trackpad debug
 * In the extended I2C reading over I2C ( >= 128 bytes ), the header size
 * have to be 6 bytes instead of 4 bytes for receiving packets. Moreover,
 * buffer size have to be power of two.
 */
#undef CONFIG_USB_I2C_MAX_WRITE_COUNT
#ifdef VARIANT_HAMMER_TP_LARGE_PAGE
/* Zed requires 516 byte per packet for touchpad update */
#define CONFIG_USB_I2C_MAX_WRITE_COUNT         \
	(1024 - 4) /* 4 is maximum header size \
		    */
#else
#define CONFIG_USB_I2C_MAX_WRITE_COUNT        \
	(128 - 4) /* 4 is maximum header size \
		   */
#endif

#undef CONFIG_USB_I2C_MAX_READ_COUNT
#define CONFIG_USB_I2C_MAX_READ_COUNT          \
	(1024 - 6) /* 6 is maximum header size \
		    */

#define CONFIG_I2C_XFER_LARGE_TRANSFER

/* No lid switch */
#undef CONFIG_LID_SWITCH

#ifdef SECTION_IS_RW
#define CONFIG_USB_HID
#define CONFIG_USB_HID_KEYBOARD
#ifdef HAS_BACKLIGHT
#define CONFIG_USB_HID_KEYBOARD_BACKLIGHT
#endif

#ifndef HAS_NO_TOUCHPAD
#define CONFIG_USB_HID_TOUCHPAD

/* Virtual address for touchpad FW in USB updater. */
#define CONFIG_TOUCHPAD_VIRTUAL_OFF 0x80000000

/* Include touchpad FW hashes in image */
#define CONFIG_TOUCHPAD_HASH_FW
#endif /* !HAS_NO_TOUCHPAD */

#define CONFIG_KEYBOARD_DEBUG
#undef CONFIG_KEYBOARD_BOOT_KEYS
#undef CONFIG_KEYBOARD_RUNTIME_KEYS
/* Keyboard output port list */
#define KB_OUT_PORT_LIST GPIO_A, GPIO_B, GPIO_C, GPIO_F

#if defined(HAS_I2C_TOUCHPAD) || defined(CONFIG_LED_DRIVER_LM3630A)
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER
#define I2C_PORT_MASTER 0
#define I2C_PORT_KBLIGHT 0
#define I2C_PORT_CHARGER 1
#endif

/* Enable PWM */
#ifdef HAS_BACKLIGHT
#define CONFIG_PWM
#endif

#ifdef CONFIG_GMR_TABLET_MODE
#define CONFIG_TABLET_MODE
#define CONFIG_KEYBOARD_TABLET_MODE_SWITCH
#endif

#ifdef HAS_SPI_TOUCHPAD
/* Enable control of SPI over USB */
#define CONFIG_USB_SPI
#define USB_IFACE_SPI USB_IFACE_I2C_SPI
#define USB_EP_SPI USB_EP_I2C_SPI
#define CONFIG_SPI_CONTROLLER
#define CONFIG_SPI_HALFDUPLEX
#define CONFIG_STM32_SPI1_CONTROLLER
#define CONFIG_SPI_TOUCHPAD_PORT 0
#define SPI_ST_TP_DEVICE_ID 0
/* Enable SPI controller xfer command */
#define CONFIG_CMD_SPI_XFER
#define CONFIG_TOUCHPAD
#define CONFIG_TOUCHPAD_ST
#elif defined(HAS_I2C_TOUCHPAD) /* HAS_SPI_TOUCHPAD */
/* Enable control of I2C over USB */
#define CONFIG_USB_I2C
#define USB_IFACE_I2C USB_IFACE_I2C_SPI
#define USB_EP_I2C USB_EP_I2C_SPI
/* Enable Elan touchpad driver */
#define CONFIG_TOUCHPAD
#define CONFIG_TOUCHPAD_ELAN
#define CONFIG_TOUCHPAD_I2C_PORT I2C_PORT_MASTER
#define CONFIG_TOUCHPAD_I2C_ADDR_FLAGS 0x15
#endif /* HAS_I2C_TOUCHPAD */

#define CONFIG_CURVE25519

#define CONFIG_USB_PAIRING

#define CONFIG_USB_CONSOLE_READ

#ifdef BOARD_WAND
/* Battery and charger options. */
#define CONFIG_CHARGER
#define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 128
#define CONFIG_CHARGER_ISL9238
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_OTG

#define CONFIG_CHARGE_RAMP_HW

#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_SMART

#define I2C_PORT_BATTERY I2C_PORT_CHARGER

#define EC_EC_UART usart2_hw
#define CONFIG_STREAM_USART2
#define CONFIG_STREAM_USART

#define CONFIG_EC_EC_COMM_SERVER
#define CONFIG_EC_EC_COMM_BATTERY
#define CONFIG_CRC8
#endif /* BOARD_WAND */

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
#define TIM_KBLIGHT 17

#include "gpio_signal.h"

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_SERIALNO,
	USB_STR_VERSION,
	USB_STR_SPI_NAME,
	USB_STR_I2C_NAME,
	USB_STR_UPDATE_NAME,
#ifdef CONFIG_USB_ISOCHRONOUS
	USB_STR_HEATMAP_NAME,
#endif
	USB_STR_COUNT
};

#ifdef SECTION_IS_RW
#ifdef HAS_BACKLIGHT
enum pwm_channel {
	PWM_CH_KBLIGHT = 0,
	/* Number of PWM channels */
	PWM_CH_COUNT
};
#endif /* HAS_BACKLIGHT */

enum adc_channel {
	/* Number of ADC channels */
	ADC_CH_COUNT
};
#endif /* SECTION_IS_RW */

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */

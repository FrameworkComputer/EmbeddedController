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

/* We're using all of TOP_B for NVMEM. TOP_A is unused as yet. */
#define CONFIG_FLASH_NVMEM
/* Offset to start of NvMem area from base of flash */
#define CONFIG_FLASH_NVMEM_OFFSET (CFG_TOP_B_OFF)
/* Address of start of Nvmem area */
#define CONFIG_FLASH_NVMEM_BASE (CONFIG_PROGRAM_MEMORY_BASE + \
				 CONFIG_FLASH_NVMEM_OFFSET)
/* Size in bytes of NvMem area */
#define CONFIG_FLASH_NVMEM_SIZE CFG_TOP_SIZE
/* Size partition in NvMem */
#define NVMEM_PARTITION_SIZE (CONFIG_FLASH_NVMEM_SIZE / NVMEM_NUM_PARTITIONS)

/* Go to sleep when nothing else is happening */
#define CONFIG_LOW_POWER_IDLE

/* Detect the states of other devices */
#define CONFIG_DEVICE_STATE

/* Enable debug cable detection */
#define CONFIG_RDD

/* USB configuration */
#define CONFIG_USB
#define CONFIG_USB_HID
#define CONFIG_USB_CONSOLE
#define CONFIG_USB_INHIBIT_INIT
#define CONFIG_USB_SELECT_PHY
#define CONFIG_USB_SPI

#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USB

/* Enable Case Closed Debugging */
#define CONFIG_CASE_CLOSED_DEBUG

#define CONFIG_USB_PID 0x5014

/* Enable SPI Master (SPI) module */
#define CONFIG_SPI_MASTER
#define CONFIG_SPI_MASTER_NO_CS_GPIOS
#define CONFIG_SPI_MASTER_CONFIGURE_GPIOS
#define CONFIG_SPI_FLASH_PORT 0

/* Enable SPI Slave (SPS) module */
#define CONFIG_SPS
#define CONFIG_TPM_SPS

#define CONFIG_RBOX

/* We don't need to send events to the AP */
#undef  CONFIG_HOSTCMD_EVENTS

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

/*
 * Enable the spstest console command & corresponding handler.
 */
#define CONFIG_SPS_TEST

/* Include crypto stuff, both software and hardware. */
#define CONFIG_DCRYPTO

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
	USB_STR_SPI_NAME,

	USB_STR_COUNT
};

/* Device indexes */
enum device_type {
	DEVICE_AP = 0,
	DEVICE_EC,
	DEVICE_SERVO,
	DEVICE_SERVO_AP,
	DEVICE_SERVO_EC,

	DEVICE_COUNT
};

/* USB SPI device indexes */
enum usb_spi {
	USB_SPI_DISABLE = 0,
	USB_SPI_AP,
	USB_SPI_EC,
};

/* Interrupt handler */
void sys_rst_asserted(enum gpio_signal signal);
void device_state_on(enum gpio_signal signal);
void device_state_off(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_CONSOLE 0
#define USB_IFACE_HID     1
#define USB_IFACE_AP      2
#define USB_IFACE_EC      3
#define USB_IFACE_UPGRADE 4
#define USB_IFACE_SPI     5
#define USB_IFACE_COUNT   6

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL   0
#define USB_EP_CONSOLE   1
#define USB_EP_HID       2
#define USB_EP_AP        3
#define USB_EP_EC        4
#define USB_EP_UPGRADE   5
#define USB_EP_SPI       6
#define USB_EP_COUNT     7

/* UART indexes (use define rather than enum to expand them) */
#define UART_CR50	0
#define UART_AP		1
#define UART_EC		2

#define UARTN UART_CR50
#define GC_UART0_RX_DISABLE

/*
 * This would be a low hanging fruit if there is a need to reduce memory
 * footprint. Having a large buffer helps not to drop debug outputs generated
 * before console is initialized, but this is not really necessary in a
 * production device.
 */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

#define CC_DEFAULT     (CC_ALL & ~CC_MASK(CC_TPM))

/* Nv Memory users */
#ifndef __ASSEMBLER__
enum nvmem_users {
	NVMEM_TPM = 0,
	NVMEM_CR50,
	NVMEM_NUM_USERS
};
#endif

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

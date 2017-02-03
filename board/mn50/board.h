/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * Make sure we don't trigger the watchdog accidentally if the timing
 * is just a little off.
 */
#undef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 5000

#define CR50_DEV

/* Features that we don't want */
#undef CONFIG_CMD_LID_ANGLE
#undef CONFIG_CMD_POWERINDEBUG
#undef CONFIG_DMA_DEFAULT_HANDLERS
#undef CONFIG_FMAP
#undef CONFIG_HIBERNATE
#undef CONFIG_LID_SWITCH
#undef CONFIG_CMD_SYSINFO
#undef CONFIG_CMD_SYSJUMP
#undef CONFIG_CMD_SYSLOCK

#ifndef CR50_DEV
/* Disable stuff that should only be in debug builds */
#undef CONFIG_CMD_MD
#undef CONFIG_CMD_RW
#undef CONFIG_CMD_SLEEPMASK
#undef CONFIG_CMD_WAITMS
#undef CONFIG_FLASH
#endif

/* Flash configuration */
#undef CONFIG_FLASH_PSTATE
/* TODO(crosbug.com/p/44745): Bringup only! Do the right thing for real! */
#define CONFIG_WP_ALWAYS
/* TODO(crosbug.com/p/44745): For debugging only */
#define CONFIG_CMD_FLASH

/* We're using TOP_A for partition 0, TOP_B for partition 1 */
#define CONFIG_FLASH_NVMEM
/* Offset to start of NvMem area from base of flash */
#define CONFIG_FLASH_NVMEM_OFFSET_A (CFG_TOP_A_OFF)
#define CONFIG_FLASH_NVMEM_OFFSET_B (CFG_TOP_B_OFF)
/* Address of start of Nvmem area */
#define CONFIG_FLASH_NVMEM_BASE_A (CONFIG_PROGRAM_MEMORY_BASE + \
				 CONFIG_FLASH_NVMEM_OFFSET_A)
#define CONFIG_FLASH_NVMEM_BASE_B (CONFIG_PROGRAM_MEMORY_BASE + \
				 CONFIG_FLASH_NVMEM_OFFSET_B)
/* Size partition in NvMem */
#define NVMEM_PARTITION_SIZE CFG_TOP_SIZE
/* Size in bytes of NvMem area */
#define CONFIG_FLASH_NVMEM_SIZE (CFG_TOP_SIZE * NVMEM_NUM_PARTITIONS)
/* Enable <key, value> variable support. */
#define CONFIG_FLASH_NVMEM_VARS
#define NVMEM_CR50_SIZE 272
#define CONFIG_FLASH_NVMEM_VARS_USER_SIZE NVMEM_CR50_SIZE

/* Allow multiple concurrent memory allocations. */
#define CONFIG_MALLOC

/* USB configuration */
#define CONFIG_USB
#define CONFIG_USB_CONSOLE
#define CONFIG_USB_I2C
#define CONFIG_USB_INHIBIT_INIT
#define CONFIG_USB_SELECT_PHY
#define CONFIG_USB_SPI
#define CONFIG_USB_SERIALNO
#define DEFAULT_SERIALNO "0"
#define CONFIG_CMD_GPIO_EXTENDED

#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USB
#define CONFIG_STREAM_USART1

/* Enable Case Closed Debugging */
#define CONFIG_CASE_CLOSED_DEBUG

#define CONFIG_USB_PID 0x502a
#define CONFIG_USB_SELF_POWERED

#undef CONFIG_USB_MAXPOWER_MA
#define CONFIG_USB_MAXPOWER_MA 0

/* Enable SPI Master (SPI) module */
#define CONFIG_SPI_MASTER
#define CONFIG_SPI_MASTER_NO_CS_GPIOS
#define CONFIG_SPI_MASTER_CONFIGURE_GPIOS
#define CONFIG_SPI_FLASH_PORT 0

/* We don't need to send events to the AP */
#undef  CONFIG_HOSTCMD_EVENTS

#define CONFIG_CONSOLE_COMMAND_FLAGS

/* Include crypto stuff, both software and hardware. */
#define CONFIG_DCRYPTO
#define CONFIG_UPTO_SHA512

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
	USB_STR_HID_KEYBOARD_NAME,
	USB_STR_AP_NAME,
	USB_STR_UPGRADE_NAME,
	USB_STR_SPI_NAME,
	USB_STR_SERIALNO,
	USB_STR_I2C_NAME,

	USB_STR_COUNT
};

void post_reboot_request(void);
void ccd_force_enable(void);

#endif /* !__ASSEMBLER__ */

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_CONSOLE 0
#define USB_IFACE_AP      1
#define USB_IFACE_UPGRADE 2
#define USB_IFACE_SPI     3
#define USB_IFACE_I2C     4
#define USB_IFACE_COUNT   5

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL   0
#define USB_EP_CONSOLE   1
#define USB_EP_AP        2
#define USB_EP_UPGRADE   3
#define USB_EP_SPI       4
#define USB_EP_I2C       5
#define USB_EP_COUNT     6

/* UART indexes (use define rather than enum to expand them) */
#define UART_CR50       0
#define UART_AP         1

#define UARTN UART_CR50

/* TODO(crosbug.com/p/56540): Remove this when UART0_RX works everywhere */
#define GC_UART0_RX_DISABLE

#define CC_DEFAULT     (CC_ALL & ~CC_MASK(CC_TPM))

/* Nv Memory users */
#ifndef __ASSEMBLER__
enum nvmem_users {
	NVMEM_CR50 = 0,
	NVMEM_NUM_USERS
};
#endif

#define CONFIG_FLASH_NVMEM_VARS_USER_NUM NVMEM_CR50

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

#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define I2C_PORT_MASTER 0
#define CONFIG_INA231

#endif /* __CROS_EC_BOARD_H */

/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * The default watchdog timeout is 1.6 seconds, but there are some legitimate
 * flash-intensive TPM operations that actually take close to that long to
 * complete. Make sure we don't trigger the watchdog accidentally if the timing
 * is just a little off.
 */
#undef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 5000

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


/* Go to sleep when nothing else is happening */
#define CONFIG_LOW_POWER_IDLE

/* Detect the states of other devices */
#define CONFIG_DEVICE_STATE

/* Enable debug cable detection */
#define CONFIG_RDD

/* USB configuration */
#define CONFIG_USB
#define CONFIG_USB_CONSOLE
#define CONFIG_USB_I2C
#define CONFIG_USB_INHIBIT_INIT
#define CONFIG_USB_SELECT_PHY
#define CONFIG_USB_SPI
#define CONFIG_USB_SERIALNO
#define DEFAULT_SERIALNO "0"

#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USB

/* Enable Case Closed Debugging */
#define CONFIG_CASE_CLOSED_DEBUG

#define CONFIG_USB_PID 0x5014
#define CONFIG_USB_SELF_POWERED

#undef CONFIG_USB_MAXPOWER_MA
#define CONFIG_USB_MAXPOWER_MA 0

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

/* Make most commands restricted */
#define CONFIG_CONSOLE_COMMAND_FLAGS
#define CONFIG_RESTRICTED_CONSOLE_COMMANDS
#define CONFIG_CONSOLE_COMMAND_FLAGS_DEFAULT CMD_FLAG_RESTRICTED

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
	USB_STR_EC_NAME,
	USB_STR_UPGRADE_NAME,
	USB_STR_SPI_NAME,
	USB_STR_SERIALNO,
	USB_STR_I2C_NAME,

	USB_STR_COUNT
};

/* Device indexes */
enum device_type {
	DEVICE_AP = 0,
	DEVICE_EC,
	DEVICE_SERVO,

	DEVICE_COUNT
};

/* USB SPI device indexes */
enum usb_spi {
	USB_SPI_DISABLE = 0,
	USB_SPI_AP,
	USB_SPI_EC,
};

void board_configure_deep_sleep_wakepins(void);
/* Interrupt handler */
void sys_rst_asserted(enum gpio_signal signal);
void device_state_on(enum gpio_signal signal);
void post_reboot_request(void);

/* Special controls over EC and AP */
void assert_sys_rst(void);
void deassert_sys_rst(void);
int is_sys_rst_asserted(void);
void assert_ec_rst(void);
void deassert_ec_rst(void);
int is_ec_rst_asserted(void);

int board_has_ap_usb(void);
int board_has_plt_rst(void);
int board_rst_pullup_needed(void);
int board_tpm_uses_i2c(void);
int board_tpm_uses_spi(void);

#endif /* !__ASSEMBLER__ */

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_CONSOLE 0
#define USB_IFACE_AP      1
#define USB_IFACE_EC      2
#define USB_IFACE_UPGRADE 3
#define USB_IFACE_SPI     4
#define USB_IFACE_I2C     5
#define USB_IFACE_COUNT   6

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL   0
#define USB_EP_CONSOLE   1
#define USB_EP_AP        2
#define USB_EP_EC        3
#define USB_EP_UPGRADE   4
#define USB_EP_SPI       5
#define USB_EP_I2C       6
#define USB_EP_COUNT     7

/* UART indexes (use define rather than enum to expand them) */
#define UART_CR50	0
#define UART_AP		1
#define UART_EC		2

#define UARTN UART_CR50

/* TODO(crosbug.com/p/56540): Remove this when UART0_RX works everywhere */
#define GC_UART0_RX_DISABLE

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

#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_I2C_SLAVE
#define CONFIG_TPM_I2CS

#define I2C_PORT_MASTER 0

#endif /* __CROS_EC_BOARD_H */

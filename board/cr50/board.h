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
#undef CONFIG_CMD_CRASH
#undef CONFIG_CMD_MD
#undef CONFIG_CMD_RW
#undef CONFIG_CMD_SLEEPMASK
#undef CONFIG_CMD_WAITMS
#undef CONFIG_FLASH
#endif

/* Flash configuration */
#undef CONFIG_FLASH_PSTATE
#define CONFIG_WP_ALWAYS
#define CONFIG_CMD_FLASH

#define CONFIG_CRC8

/* Non-volatile counter storage for U2F */
#define CONFIG_FLASH_NVCOUNTER
#define CONFIG_FLASH_NVCTR_SIZE CONFIG_FLASH_BANK_SIZE
#define CONFIG_FLASH_NVCTR_BASE_A (CONFIG_PROGRAM_MEMORY_BASE + \
				   CFG_TOP_A_OFF)
#define CONFIG_FLASH_NVCTR_BASE_B (CONFIG_PROGRAM_MEMORY_BASE + \
				   CFG_TOP_B_OFF)
/* We're using TOP_A for partition 0, TOP_B for partition 1 */
#define CONFIG_FLASH_NVMEM
/* Offset to start of NvMem area from base of flash */
#define CONFIG_FLASH_NVMEM_OFFSET_A (CFG_TOP_A_OFF + CONFIG_FLASH_NVCTR_SIZE)
#define CONFIG_FLASH_NVMEM_OFFSET_B (CFG_TOP_B_OFF + CONFIG_FLASH_NVCTR_SIZE)
/* Address of start of Nvmem area */
#define CONFIG_FLASH_NVMEM_BASE_A (CONFIG_PROGRAM_MEMORY_BASE + \
				 CONFIG_FLASH_NVMEM_OFFSET_A)
#define CONFIG_FLASH_NVMEM_BASE_B (CONFIG_PROGRAM_MEMORY_BASE + \
				 CONFIG_FLASH_NVMEM_OFFSET_B)
/* Size partition in NvMem */
#define NVMEM_PARTITION_SIZE (CFG_TOP_SIZE - CONFIG_FLASH_NVCTR_SIZE)
/* Size in bytes of NvMem area */
#define CONFIG_FLASH_NVMEM_SIZE (NVMEM_PARTITION_SIZE * NVMEM_NUM_PARTITIONS)
/* Enable <key, value> variable support. */
#define CONFIG_FLASH_NVMEM_VARS
#define NVMEM_CR50_SIZE 272
#define CONFIG_FLASH_NVMEM_VARS_USER_SIZE NVMEM_CR50_SIZE


/* Go to sleep when nothing else is happening */
#define CONFIG_LOW_POWER_IDLE

/* Allow multiple concurrent memory allocations. */
#define CONFIG_MALLOC

/* Detect the states of other devices */
#define CONFIG_DEVICE_STATE

/* Enable debug cable detection */
#define CONFIG_RDD

/* Also use the cr50 as a second factor authentication */
#define CONFIG_U2F

/* USB configuration */
#define CONFIG_USB
#define CONFIG_USB_CONSOLE
#define CONFIG_USB_I2C
#define CONFIG_USB_INHIBIT_INIT
#define CONFIG_USB_SPI
#define CONFIG_USB_SERIALNO
#define DEFAULT_SERIALNO "0"

#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USB
#define CONFIG_STREAM_USART1
#define CONFIG_STREAM_USART2

/* Enable Case Closed Debugging */
#define CONFIG_CASE_CLOSED_DEBUG
#define CONFIG_CASE_CLOSED_DEBUG_V1
#define CONFIG_PHYSICAL_PRESENCE

#ifdef CR50_DEV
/* Enable unsafe dev features for CCD in dev builds */
#define CONFIG_CASE_CLOSED_DEBUG_V1_UNSAFE
#define CONFIG_PHYSICAL_PRESENCE_DEBUG_UNSAFE
#endif

#define CONFIG_USB_PID 0x5014
#define CONFIG_USB_SELF_POWERED

#undef CONFIG_USB_MAXPOWER_MA
#define CONFIG_USB_MAXPOWER_MA 0

/* Need to be able to bitbang the EC UART for updates through CCD. */
#define CONFIG_UART_BITBANG

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

/* Implement custom udelay, due to usec hwtimer imprecision. */
#define CONFIG_HW_SPECIFIC_UDELAY

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
	DEVICE_BATTERY_PRESENT,
	DEVICE_CCD_MODE,

	DEVICE_COUNT
};

/* NVMem variables. */
enum nvmem_vars {
	NVMEM_VAR_CONSOLE_LOCKED = 0,
	NVMEM_VAR_TEST_VAR,
	NVMEM_VAR_U2F_SALT,
	NVMEM_VAR_CCD_CONFIG,

	NVMEM_VARS_COUNT
};

void board_configure_deep_sleep_wakepins(void);
/* Interrupt handler */
void tpm_rst_deasserted(enum gpio_signal signal);
void device_state_on(enum gpio_signal signal);
void post_reboot_request(void);
void ec_tx_cr50_rx(enum gpio_signal signal);

/* Special controls over EC and AP */
void assert_sys_rst(void);
void deassert_sys_rst(void);
int is_sys_rst_asserted(void);
void assert_ec_rst(void);
void deassert_ec_rst(void);
int is_ec_rst_asserted(void);

int board_has_ap_usb(void);
int board_use_plt_rst(void);
int board_rst_pullup_needed(void);
int board_tpm_uses_i2c(void);
int board_tpm_uses_spi(void);
int board_id_is_mismatched(void);

void power_button_record(void);

/* Functions needed by CCD config */
int board_battery_is_present(void);
int board_fwmp_allows_unlock(void);
void board_reboot_ap(void);
int board_wipe_tpm(void);
int board_is_first_factory_boot(void);

/* Returns True if chip is brought up in a factory test harness. */
int chip_factory_mode(void);

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

#define CC_DEFAULT     (CC_ALL & ~CC_MASK(CC_TPM))

/* Nv Memory users */
#ifndef __ASSEMBLER__
enum nvmem_users {
	NVMEM_TPM = 0,
	NVMEM_CR50,
	NVMEM_NUM_USERS
};
#endif

#define CONFIG_FLASH_NVMEM_VARS_USER_NUM NVMEM_CR50
#define CONFIG_RW_B

/* Firmware upgrade options. */
#define CONFIG_NON_HC_FW_UPDATE
#define CONFIG_USB_FW_UPDATE

#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_I2C_SLAVE
#define CONFIG_TPM_I2CS

#define CONFIG_BOARD_ID_SUPPORT
#define CONFIG_EXTENDED_VERSION_INFO

#define I2C_PORT_MASTER 0

#endif /* __CROS_EC_BOARD_H */

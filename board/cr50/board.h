/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define CONFIG_LTO

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
#undef CONFIG_CMD_SLEEPMASK_SET
#undef CONFIG_CMD_WAITMS
#undef CONFIG_FLASH
#endif

/* Enable getting gpio flags to tell if open drain pins are asserted */
#define CONFIG_GPIO_GET_EXTENDED

/* Flash configuration */
#undef CONFIG_FLASH_PSTATE
#define CONFIG_WP_ALWAYS
#define CONFIG_FLASH_READOUT_PROTECTION
#define CONFIG_CMD_FLASH

#define CONFIG_CRC8

/* Non-volatile counter storage for U2F (deprecated) */
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
#define CONFIG_FLASH_NVMEM_BASE_A                                              \
	(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_FLASH_NVMEM_OFFSET_A)
#define CONFIG_FLASH_NVMEM_BASE_B                                              \
	(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_FLASH_NVMEM_OFFSET_B)
#define CONFIG_FLASH_NEW_NVMEM_BASE_A                                          \
	(CONFIG_FLASH_NVMEM_BASE_A + CONFIG_FLASH_BANK_SIZE)
#define CONFIG_FLASH_NEW_NVMEM_BASE_B                                          \
	(CONFIG_FLASH_NVMEM_BASE_B + CONFIG_FLASH_BANK_SIZE)

/* Size partition in NvMem */
#define NVMEM_PARTITION_SIZE (CFG_TOP_SIZE - CONFIG_FLASH_NVCTR_SIZE)
#define NEW_NVMEM_PARTITION_SIZE (NVMEM_PARTITION_SIZE - CONFIG_FLASH_BANK_SIZE)
#define NEW_NVMEM_TOTAL_PAGES                                                  \
	(2 * NEW_NVMEM_PARTITION_SIZE / CONFIG_FLASH_BANK_SIZE)
/* Size in bytes of NvMem area */
#define CONFIG_FLASH_LOG
#define CONFIG_FLASH_NVMEM_SIZE (NVMEM_PARTITION_SIZE * NVMEM_NUM_PARTITIONS)
/* Enable <key, value> variable support. */
#define CONFIG_FLASH_NVMEM_VARS
#define NVMEM_CR50_SIZE 272
#define CONFIG_FLASH_NVMEM_VARS_USER_SIZE NVMEM_CR50_SIZE


/* Go to sleep when nothing else is happening */
#define CONFIG_LOW_POWER_IDLE

/* Allow multiple concurrent memory allocations. */
#define CONFIG_MALLOC

/* Enable debug cable detection */
#define CONFIG_RDD

/* Also use the cr50 as a second factor authentication */
#define CONFIG_U2F

/* USB configuration */
#define CONFIG_USB
#define CONFIG_USB_CONSOLE_STREAM
#undef CONFIG_USB_CONSOLE_TX_BUF_SIZE
#define CONFIG_USB_CONSOLE_TX_BUF_SIZE		4096
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
#define CONFIG_CASE_CLOSED_DEBUG_V1
#define CONFIG_PHYSICAL_PRESENCE
/* Loosen CCD open requirements. Only allowed in prePVT images */
#define CONFIG_CCD_OPEN_PREPVT

#ifdef CR50_DEV
/* Enable unsafe dev features for CCD in dev builds */
#define CONFIG_CASE_CLOSED_DEBUG_V1_UNSAFE
#define CONFIG_CMD_FLASH_LOG
#define CONFIG_PHYSICAL_PRESENCE_DEBUG_UNSAFE
#endif
#if defined(CR50_DEV) || defined(CR50_SQA)
#define CR50_RELAXED
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
#define CONFIG_RBOX_WAKEUP

/* We don't need to send events to the AP */
#undef  CONFIG_HOSTCMD_EVENTS

/* Make most commands restricted */
#define CONFIG_CONSOLE_COMMAND_FLAGS
#define CONFIG_RESTRICTED_CONSOLE_COMMANDS
#define CONFIG_CONSOLE_COMMAND_FLAGS_DEFAULT CMD_FLAG_RESTRICTED

/* Include crypto stuff, both software and hardware. Enable optimizations. */
#define CONFIG_DCRYPTO
#define CONFIG_UPTO_SHA512
#define CONFIG_DCRYPTO_RSA_SPEEDUP

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

/*
 * Device states
 *
 * Note that not all states are used by all devices.
 */
enum device_state {
	/* Initial state at boot */
	DEVICE_STATE_INIT = 0,

	/*
	 * Detect was not asserted at boot, but we're not willing to give up on
	 * the device right away so we're debouncing to see if it shows up.
	 */
	DEVICE_STATE_INIT_DEBOUNCING,

	/*
	 * Device was detected at boot, but we can't enable transmit yet
	 * because that would interfere with detection of another device.
	 */
	DEVICE_STATE_INIT_RX_ONLY,

	/* Disconnected or off, because detect is deasserted */
	DEVICE_STATE_DISCONNECTED,
	DEVICE_STATE_OFF,

	/* Device state is not knowable because we're driving detect */
	DEVICE_STATE_UNDETECTABLE,

	/* Connected or on, because detect is asserted */
	DEVICE_STATE_CONNECTED,
	DEVICE_STATE_ON,

	/*
	 * Device was connected, but we saw detect deasserted and are
	 * debouncing to see if it stays deasserted - at which point we'll
	 * decide that it's disconnected.
	 */
	DEVICE_STATE_DEBOUNCING,

	/* Device state is unknown.  Used only by legacy device_state code. */
	DEVICE_STATE_UNKNOWN,

	/* The state is being ignored. */
	DEVICE_STATE_IGNORED,

	/* Number of device states */
	DEVICE_STATE_COUNT
};

/**
 * Return the name of the device state as as string.
 *
 * @param state		State to look up
 * @return Name of the state, or "?" if no match.
 */
const char *device_state_name(enum device_state state);

/* NVMem variables. */
enum nvmem_vars {
	NVMEM_VAR_CONSOLE_LOCKED = 0,
	NVMEM_VAR_TEST_VAR,
	NVMEM_VAR_U2F_SALT,
	NVMEM_VAR_CCD_CONFIG,
	NVMEM_VAR_G2F_SALT,

	NVMEM_VARS_COUNT
};

void board_configure_deep_sleep_wakepins(void);
void ap_detect_asserted(enum gpio_signal signal);
void ec_detect_asserted(enum gpio_signal signal);
void servo_detect_asserted(enum gpio_signal signal);
void tpm_rst_deasserted(enum gpio_signal signal);
void tpm_rst_asserted(enum gpio_signal signal);

void post_reboot_request(void);

/* Special controls over EC and AP */
void assert_sys_rst(void);
void deassert_sys_rst(void);
void assert_ec_rst(void);
void deassert_ec_rst(void);
int is_ec_rst_asserted(void);
/* Ignore the servo state. */
void servo_ignore(int enable);

/**
 * Set up a deferred call to update CCD state.
 *
 * This will enable/disable UARTs, SPI, I2C, etc. as needed.
 */
void ccd_update_state(void);

/**
 * Return the state of the BOARD_USE_PLT_RST board strap option.
 *
 * @return 0 if option is not set, !=0 if option set.
 */
int board_use_plt_rst(void);
/**
 * Return the state of the BOARD_NEEDS_SYS_RST_PULL_UP board strap option.
 *
 * @return 0 if option is not set, !=0 if option set.
 */
int board_rst_pullup_needed(void);
/**
 * Return the state of the BOARD_SLAVE_CONFIG_I2C board strap option.
 *
 * @return 0 if option is not set, !=0 if option set.
 */
int board_tpm_uses_i2c(void);
/**
 * Return the state of the BOARD_SLAVE_CONFIG_SPI board strap option.
 *
 * @return 0 if option is not set, !=0 if option set.
 */
int board_tpm_uses_spi(void);
/**
 * Return the state of the BOARD_CLOSED_SOURCE_SET1 board strap option.
 *
 * @return 0 if option is not set, !=0 if option set.
 */
int board_uses_closed_source_set1(void);
/**
 * The board needs to wait until TPM_RST_L is asserted before deasserting
 * system reset signals.
 *
 * @return 0 if option is not set, !=0 if option set.
 */
int board_uses_closed_loop_reset(void);
/**
 * The board has all necessary I2C pins connected for INA support.
 *
 * @return 0 if option is not set, !=0 if option set.
 */
int board_has_ina_support(void);
/* The board allows vendor commands to enable/disable tpm. */
int board_tpm_mode_change_allowed(void);
int board_id_is_mismatched(void);
/* Allow for deep sleep to be enabled on AP shutdown */
int board_deep_sleep_allowed(void);

void power_button_record(void);

/**
 * Enable/disable power button release interrupt.
 *
 * @param enable	Enable (!=0) or disable (==0)
 */
void power_button_release_enable_interrupt(int enable);

/* Functions needed by CCD config */
int board_battery_is_present(void);
int board_fwmp_allows_unlock(void);
int board_vboot_dev_mode_enabled(void);
void board_reboot_ap(void);
void board_reboot_ec(void);
void board_closed_loop_reset(void);
int board_wipe_tpm(int reset_required);
int board_is_first_factory_boot(void);

int usb_i2c_board_enable(void);
void usb_i2c_board_disable(void);

void print_ap_state(void);
void print_ap_uart_state(void);
void print_ec_state(void);
void print_servo_state(void);

int ap_is_on(void);
int ap_uart_is_on(void);
int ec_is_on(void);
int ec_is_rx_allowed(void);
int servo_is_connected(void);

void set_ap_on(void);

/* Returns True if chip is brought up in a factory test harness. */
int chip_factory_mode(void);

/*
 * Trigger generation of the ITE SYNC sequence on the way up after next
 * reboot.
 */
void board_start_ite_sync(void);

/*
 * Board specific function (needs information about pinmux settings) which
 * allows to take the i2cs controller out of the 'wedged' state where the
 * master stopped i2c access mid transaction and the slave is holding SDA low,
 */
void board_unwedge_i2cs(void);

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
#define CONFIG_SN_BITS_SUPPORT
#define CONFIG_EXTENDED_VERSION_INFO

#define I2C_PORT_MASTER 0

#define CONFIG_BASE32
#define CONFIG_RMA_AUTH
#define CONFIG_FACTORY_MODE
#define CONFIG_RNG

#define CONFIG_ENABLE_H1_ALERTS

/* Enable hardware backed brute force resistance feature */
#define CONFIG_PINWEAVER

/*
 * Disabling p256 will result in RMA Auth falling back to the x25519 curve
 * which in turn would require extra 5328 bytes of flash space.
 */
#define CONFIG_RMA_AUTH_USE_P256
#ifndef CONFIG_RMA_AUTH_USE_P256
#define CONFIG_CURVE25519
#endif

#define CONFIG_CCD_ITE_PROGRAMMING

/*
 * Increase sizes of USB over I2C read and write queues. Sizes are are such
 * that when appropriate overheads are included, total buffer sizes are powers
 * of 2 (2^9 in both cases below).
 */
#undef CONFIG_USB_I2C_MAX_WRITE_COUNT
#undef CONFIG_USB_I2C_MAX_READ_COUNT
#define CONFIG_USB_I2C_MAX_WRITE_COUNT 508
#define CONFIG_USB_I2C_MAX_READ_COUNT 506

/* The below time constants are way longer than should be required in practice:
 *
 * Time it takes to finish processing TPM command
 */
#define TPM_PROCESSING_TIME (1 * SECOND)

/*
 * Time it takse TPM reset function to wipe out the NVMEM and reboot the
 * device.
 */
#define TPM_RESET_TIME (10 * SECOND)

/* Total time deep sleep should not be allowed while wiping the TPM. */
#define DISABLE_SLEEP_TIME_TPM_WIPE (TPM_PROCESSING_TIME + TPM_RESET_TIME)

#endif /* __CROS_EC_BOARD_H */

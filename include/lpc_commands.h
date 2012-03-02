/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC command constants for Chrome EC */

#ifndef __CROS_EC_LPC_COMMANDS_H
#define __CROS_EC_LPC_COMMANDS_H

#include <stdint.h>


/* During the development stage, the LPC bus has high error bit rate.
 * Using checksum can detect the error and trigger re-transmit.
 * FIXME: remove this after mass production.
 */
#define SUPPORT_CHECKSUM


/* I/O addresses for LPC commands */
#define EC_LPC_ADDR_KERNEL_DATA   0x62
#define EC_LPC_ADDR_KERNEL_CMD    0x66
#define EC_LPC_ADDR_KERNEL_PARAM 0x800
#define EC_LPC_ADDR_USER_DATA    0x200
#define EC_LPC_ADDR_USER_CMD     0x204
#define EC_LPC_ADDR_USER_PARAM   0x880
#define EC_LPC_PARAM_SIZE          128  /* Size of each param area in bytes */

#define EC_LPC_ADDR_MEMMAP       0x900
#define EC_LPC_MEMMAP_SIZE         256

/* The offset address of each type of data in mapped memory. */
#define EC_LPC_MEMMAP_TEMP_SENSOR 0x00
#define EC_LPC_MEMMAP_FAN         0x10
#define EC_LPC_MEMMAP_BATT_VOLT   0x20
#define EC_LPC_MEMMAP_BATT_RATE   0x24
#define EC_LPC_MEMMAP_BATT_CAP    0x28
#define EC_LPC_MEMMAP_BATT_FLAG   0x2c
#define EC_LPC_MEMMAP_SWITCHES    0x30
#define EC_LPC_MEMMAP_HOST_EVENTS 0x34

/* The battery bit flags. */
#define EC_BATT_FLAG_AC_PRESENT   0x01
#define EC_BATT_FLAG_BATT_PRESENT 0x02
#define EC_BATT_FLAG_DISCHARGING  0x04
#define EC_BATT_FLAG_CHARGING     0x08
#define EC_BATT_FLAG_LEVEL_CRITICAL 0x10

/* The offset of temperature value stored in mapped memory.
 * This allows reporting a temperature range of
 * 200K to 454K = -73C to 181C.
 */
#define EC_LPC_TEMP_SENSOR_OFFSET 200

/* LPC command status byte masks */
/* EC has written a byte in the data register and host hasn't read it yet */
#define EC_LPC_STATUS_TO_HOST     0x01
/* Host has written a command/data byte and the EC hasn't read it yet */
#define EC_LPC_STATUS_FROM_HOST   0x02
/* EC is processing a command */
#define EC_LPC_STATUS_PROCESSING  0x04
/* Last write to EC was a command, not data */
#define EC_LPC_STATUS_LAST_CMD    0x08
/* EC is in burst mode.  Chrome EC doesn't support this, so this bit is never
 * set. */
#define EC_LPC_STATUS_BURST_MODE  0x10
/* SCI event is pending (requesting SCI query) */
#define EC_LPC_STATUS_SCI_PENDING 0x20
/* SMI event is pending (requesting SMI query) */
#define EC_LPC_STATUS_SMI_PENDING 0x40
/* (reserved) */
#define EC_LPC_STATUS_RESERVED    0x80

/* EC is busy.  This covers both the EC processing a command, and the host has
 * written a new command but the EC hasn't picked it up yet. */
#define EC_LPC_STATUS_BUSY_MASK \
	(EC_LPC_STATUS_FROM_HOST | EC_LPC_STATUS_PROCESSING)

/* LPC command response codes */
/* TODO: move these so they don't overlap SCI/SMI data? */
enum lpc_status {
	EC_LPC_RESULT_SUCCESS = 0,
	EC_LPC_RESULT_INVALID_COMMAND = 1,
	EC_LPC_RESULT_ERROR = 2,
	EC_LPC_RESULT_INVALID_PARAM = 3,
};


/* Notes on commands:
 *
 * Each command is an 8-byte command value.  Commands which take
 * params or return response data specify structs for that data.  If
 * no struct is specified, the command does not input or output data,
 * respectively. */

/*****************************************************************************/
/* General / test commands */

/* Hello.  This is a simple command to test the EC is responsive to
 * commands. */
#define EC_LPC_COMMAND_HELLO 0x01
struct lpc_params_hello {
	uint32_t in_data;  /* Pass anything here */
} __attribute__ ((packed));
struct lpc_response_hello {
	uint32_t out_data;  /* Output will be in_data + 0x01020304 */
} __attribute__ ((packed));


/* Get version number */
#define EC_LPC_COMMAND_GET_VERSION 0x02
enum lpc_current_image {
	EC_LPC_IMAGE_UNKNOWN = 0,
	EC_LPC_IMAGE_RO,
	EC_LPC_IMAGE_RW_A,
	EC_LPC_IMAGE_RW_B
};
struct lpc_response_get_version {
	/* Null-terminated version strings for RO, RW-A, RW-B */
	char version_string_ro[32];
	char version_string_rw_a[32];
	char version_string_rw_b[32];
	uint32_t current_image;  /* One of lpc_current_image */
} __attribute__ ((packed));


/* Read test */
#define EC_LPC_COMMAND_READ_TEST 0x03
struct lpc_params_read_test {
	uint32_t offset;   /* Starting value for read buffer */
	uint32_t size;     /* Size to read in bytes */
} __attribute__ ((packed));
struct lpc_response_read_test {
	uint32_t data[32];
} __attribute__ ((packed));


/*****************************************************************************/
/* Flash commands */

/* Maximum bytes that can be read/written in a single command */
#define EC_LPC_FLASH_SIZE_MAX 64

/* Get flash info */
#define EC_LPC_COMMAND_FLASH_INFO 0x10
struct lpc_response_flash_info {
	/* Usable flash size, in bytes */
	uint32_t flash_size;
	/* Write block size.  Write offset and size must be a multiple
	 * of this. */
	uint32_t write_block_size;
	/* Erase block size.  Erase offset and size must be a multiple
	 * of this. */
	uint32_t erase_block_size;
	/* Protection block size.  Protection offset and size must be a
	 * multiple of this. */
	uint32_t protect_block_size;
} __attribute__ ((packed));

/* Read flash */
#define EC_LPC_COMMAND_FLASH_READ 0x11
struct lpc_params_flash_read {
	uint32_t offset;   /* Byte offset to read */
	uint32_t size;     /* Size to read in bytes */
} __attribute__ ((packed));
struct lpc_response_flash_read {
	uint8_t data[EC_LPC_FLASH_SIZE_MAX];
} __attribute__ ((packed));

/* Write flash */
#define EC_LPC_COMMAND_FLASH_WRITE 0x12
struct lpc_params_flash_write {
	uint32_t offset;   /* Byte offset to write */
	uint32_t size;     /* Size to write in bytes */
	uint8_t data[EC_LPC_FLASH_SIZE_MAX];
} __attribute__ ((packed));

/* Erase flash */
#define EC_LPC_COMMAND_FLASH_ERASE 0x13
struct lpc_params_flash_erase {
	uint32_t offset;   /* Byte offset to erase */
	uint32_t size;     /* Size to erase in bytes */
} __attribute__ ((packed));

/* Flashmap offset */
#define EC_LPC_COMMAND_FLASH_GET_FLASHMAP 0x14
struct lpc_response_flash_flashmap {
	uint32_t offset;   /* Flashmap offset */
} __attribute__ ((packed));

/* Enable/disable flash write protect */
#define EC_LPC_COMMAND_FLASH_WP_ENABLE 0x15
struct lpc_params_flash_wp_enable {
	uint32_t enable_wp;
} __attribute__ ((packed));

/* Get flash write protection commit state */
#define EC_LPC_COMMAND_FLASH_WP_GET_STATE 0x16
struct lpc_response_flash_wp_enable {
	uint32_t enable_wp;
} __attribute__ ((packed));

/* Set/get flash write protection range */
#define EC_LPC_COMMAND_FLASH_WP_SET_RANGE 0x17
struct lpc_params_flash_wp_range {
	/* Byte offset aligned to info.protect_block_size */
	uint32_t offset;
	/* Size should be multiply of info.protect_block_size */
	uint32_t size;
} __attribute__ ((packed));

#define EC_LPC_COMMAND_FLASH_WP_GET_RANGE 0x18
struct lpc_response_flash_wp_range {
	uint32_t offset;
	uint32_t size;
} __attribute__ ((packed));

/* Read flash write protection GPIO pin */
#define EC_LPC_COMMAND_FLASH_WP_GET_GPIO 0x19
struct lpc_params_flash_wp_gpio {
	uint32_t pin_no;
} __attribute__ ((packed));
struct lpc_response_flash_wp_gpio {
	uint32_t value;
} __attribute__ ((packed));

#ifdef SUPPORT_CHECKSUM
/* Checksum a range of flash datq */
#define EC_LPC_COMMAND_FLASH_CHECKSUM 0x1f
struct lpc_params_flash_checksum {
	uint32_t offset;   /* Byte offset to read */
	uint32_t size;     /* Size to read in bytes */
} __attribute__ ((packed));
struct lpc_response_flash_checksum {
	uint8_t checksum;
} __attribute__ ((packed));
#define BYTE_IN(sum, byte) do {  \
		sum = (sum << 1) | (sum >> 7);  \
		sum ^= (byte ^ 0x53);  \
	} while (0)
#endif  /* SUPPORT_CHECKSUM */

/*****************************************************************************/
/* PWM commands */

/* Get fan RPM */
#define EC_LPC_COMMAND_PWM_GET_FAN_RPM 0x20
struct lpc_response_pwm_get_fan_rpm {
	uint32_t rpm;
} __attribute__ ((packed));

/* Set target fan RPM */
#define EC_LPC_COMMAND_PWM_SET_FAN_TARGET_RPM 0x21
struct lpc_params_pwm_set_fan_target_rpm {
	uint32_t rpm;
} __attribute__ ((packed));

/* Get keyboard backlight */
#define EC_LPC_COMMAND_PWM_GET_KEYBOARD_BACKLIGHT 0x22
struct lpc_response_pwm_get_keyboard_backlight {
	uint8_t percent;
} __attribute__ ((packed));

/* Set keyboard backlight */
#define EC_LPC_COMMAND_PWM_SET_KEYBOARD_BACKLIGHT 0x23
struct lpc_params_pwm_set_keyboard_backlight {
	uint8_t percent;
} __attribute__ ((packed));

/*****************************************************************************/
/* USB charging control commands */

/* Set USB port charging mode */
#define EC_LPC_COMMAND_USB_CHARGE_SET_MODE 0x30
struct lpc_params_usb_charge_set_mode {
	uint8_t usb_port_id;
	uint8_t mode;
} __attribute__ ((packed));

/*****************************************************************************/
/* Persistent storage for host */

/* Maximum bytes that can be read/written in a single command */
#define EC_LPC_PSTORE_SIZE_MAX 64

/* Get persistent storage info */
#define EC_LPC_COMMAND_PSTORE_INFO 0x40
struct lpc_response_pstore_info {
	/* Persistent storage size, in bytes */
	uint32_t pstore_size;
	/* Access size.  Read/write offset and size must be a multiple
	 * of this. */
	uint32_t access_size;
} __attribute__ ((packed));

/* Read persistent storage */
#define EC_LPC_COMMAND_PSTORE_READ 0x41
struct lpc_params_pstore_read {
	uint32_t offset;   /* Byte offset to read */
	uint32_t size;     /* Size to read in bytes */
} __attribute__ ((packed));
struct lpc_response_pstore_read {
	uint8_t data[EC_LPC_PSTORE_SIZE_MAX];
} __attribute__ ((packed));

/* Write persistent storage */
#define EC_LPC_COMMAND_PSTORE_WRITE 0x42
struct lpc_params_pstore_write {
	uint32_t offset;   /* Byte offset to write */
	uint32_t size;     /* Size to write in bytes */
	uint8_t data[EC_LPC_PSTORE_SIZE_MAX];
} __attribute__ ((packed));

/*****************************************************************************/
/* Thermal engine commands */

/* Set thershold value */
#define EC_LPC_COMMAND_THERMAL_SET_THRESHOLD 0x50
struct lpc_params_thermal_set_threshold {
	uint8_t sensor_id;
	uint8_t threshold_id;
	uint16_t value;
} __attribute__ ((packed));

/* Get threshold value */
#define EC_LPC_COMMAND_THERMAL_GET_THRESHOLD 0x51
struct lpc_params_thermal_get_threshold {
	uint8_t sensor_id;
	uint8_t threshold_id;
} __attribute__ ((packed));
struct lpc_response_thermal_get_threshold {
	uint16_t value;
} __attribute__ ((packed));

/* Toggling automatic fan control */
#define EC_LPC_COMMAND_THERMAL_AUTO_FAN_CTRL 0x52

/*****************************************************************************/
/* Host event commands */

#define EC_LPC_COMMAND_HOST_EVENT_GET_SMI_MASK 0x88
struct lpc_response_host_event_get_smi_mask {
	uint32_t mask;
} __attribute__ ((packed));

#define EC_LPC_COMMAND_HOST_EVENT_GET_SCI_MASK 0x89
struct lpc_response_host_event_get_sci_mask {
	uint32_t mask;
} __attribute__ ((packed));

#define EC_LPC_COMMAND_HOST_EVENT_SET_SMI_MASK 0x8a
struct lpc_params_host_event_set_smi_mask {
	uint32_t mask;
} __attribute__ ((packed));

#define EC_LPC_COMMAND_HOST_EVENT_SET_SCI_MASK 0x8b
struct lpc_params_host_event_set_sci_mask {
	uint32_t mask;
} __attribute__ ((packed));

#define EC_LPC_COMMAND_HOST_EVENT_CLEAR 0x8c
struct lpc_params_host_event_clear {
	uint32_t mask;
} __attribute__ ((packed));

/*****************************************************************************/
/* Special commands
 *
 * These do not follow the normal rules for commands.  See each command for
 * details. */

/* ACPI Query Embedded Controller
 *
 * This clears the lowest-order bit in the currently pending host events, and
 * sets the result code to the 1-based index of the bit (event 0x00000001 = 1,
 * event 0x80000000 = 32), or 0 if no event was pending. */
#define EC_LPC_COMMAND_ACPI_QUERY_EVENT 0x84

/* Reboot
 *
 * This command will work even when the EC LPC interface is busy, because the
 * reboot command is processed at interrupt level.  Note that when the EC
 * reboots, the host will reboot too, so there is no response to this
 * command. */
#define EC_LPC_COMMAND_REBOOT 0xd1  /* Think "die" */



#endif  /* __CROS_EC_LPC_COMMANDS_H */

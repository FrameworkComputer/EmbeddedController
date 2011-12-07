/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC command constants for Chrome EC */

#ifndef __CROS_EC_LPC_COMMANDS_H
#define __CROS_EC_LPC_COMMANDS_H

#include <stdint.h>

/* LPC command status byte masks */
/* EC is busy processing a command.  This covers both bit 0x04, which
 * is the busy-bit, and 0x02, which is the bit which indicates the
 * host has written a byte but the EC hasn't picked it up yet. */
#define EC_LPC_BUSY_MASK   0x06
#define EC_LPC_STATUS_MASK 0xF0  /* Mask for status codes in status byte */
#define EC_LPC_GET_STATUS(x) (((x) & EC_LPC_STATUS_MASK) >> 4)

/* LPC command response codes */
enum lpc_status {
	EC_LPC_STATUS_SUCCESS = 0,
	EC_LPC_STATUS_INVALID_COMMAND = 1,
	EC_LPC_STATUS_ERROR = 2,
	EC_LPC_STATUS_INVALID_PARAM = 3,
};


/* Notes on commands:
 *
 * Each command is an 8-byte command value.  Commands which take
 * params or return response data specify structs for that data.  If
 * no struct is specified, the command does not input or output data,
 * respectively. */

/* Reboot.  This command will work even when the EC LPC interface is
 * busy, because the reboot command is processed at interrupt
 * level.  Note that when the EC reboots, the host will reboot too, so
 * there is no response to this command. */
#define EC_LPC_COMMAND_REBOOT 0xD1  /* Think "die" */


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
#define EC_LPC_FLASH_SIZE_MAX 128

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
	uint32_t offset;   /* Byte offset to erase */
	uint32_t size;     /* Size to erase in bytes */
	uint8_t data[EC_LPC_FLASH_SIZE_MAX];
} __attribute__ ((packed));


/* Erase flash */
#define EC_LPC_COMMAND_FLASH_ERASE 0x13
struct lpc_params_flash_erase {
	uint32_t offset;   /* Byte offset to erase */
	uint32_t size;     /* Size to erase in bytes */
} __attribute__ ((packed));


#endif  /* __CROS_EC_LPC_COMMANDS_H */

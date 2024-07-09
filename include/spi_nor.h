/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SPI Serial NOR Flash driver module for Chrome EC */

#ifndef __CROS_EC_SPI_NOR_H
#define __CROS_EC_SPI_NOR_H

#include "common.h"
#include "console.h"
#include "shared_mem.h"
#include "task.h"
#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Driver compatibility requirements based on JEDEC SFDP support:
 *
 * Parameter     | SFDP v1.(5+)     | SFDP v1.0 | All others
 * ============================================================================
 * Capacity      | N/A              | N/A       | Uses instantiated default
 *               --------------------------------------------------------------
 *               | The capacity must be less than 4GiB for compatibility.
 * ----------------------------------------------------------------------------
 * Page Size     | N/A              | 1B or 64B | Uses instantiated default
 * ----------------------------------------------------------------------------
 * Erase Opcodes | 4KiB Erase with an opcode of 0x20 is always required.
 * ----------------------------------------------------------------------------
 * 4B Addressing | 4B addressing mode must be supported if the part is larger
 *               | than 16MiB. 4B mode entry will be attempted through opcode
 *               | 0xB7 and exit through 0xE9 where writes are enabled for both
 *               | in case it is required
 * ----------------------------------------------------------------------------
 */

/* Boards which use SPI NOR devices must provide enum spi_device indexing all
 * spi_device_t's in the board.h file. */
enum spi_device;

struct spi_nor_device_t {
	/* Name of the Serial NOR Flash device. */
	const char *name;

	/* Index of the SPI controller which this device is connected
	 * through.
	 */
	const enum spi_device spi_controller;

	/* Maximum timeout per command in microseconds. */
	const uint32_t timeout_usec;

	/* when instantiating this device, the initialization values for the
	 * following fields will be the default values. Note that the values
	 * below may change on the fly based on device state and SFDP
	 * discovery. */
	uint32_t capacity;
	size_t page_size;
	int in_4b_addressing_mode;
};

extern struct spi_nor_device_t spi_nor_devices[];
extern const unsigned int spi_nor_devices_used;

/* Industry standard Serial NOR Flash opcodes. All other opcodes are part
 * specific and require SFDP discovery. */
#define SPI_NOR_OPCODE_WRITE_STATUS 0x01 /* Write Status Register (1 Byte) */
#define SPI_NOR_OPCODE_PAGE_PROGRAM 0x02 /* Page program */
#define SPI_NOR_OPCODE_SLOW_READ 0x03 /* Read data (low frequency) */
#define SPI_NOR_OPCODE_WRITE_DISABLE 0x04
#define SPI_NOR_OPCODE_READ_STATUS 0x05 /* Read Status Register */
#define SPI_NOR_OPCODE_WRITE_ENABLE 0x06
#define SPI_NOR_OPCODE_FAST_READ 0x0b /* Read data (high frequency) */
#define SPI_NOR_OPCODE_SFDP 0x5a /* Read JEDEC SFDP */
#define SPI_NOR_OPCODE_JEDEC_ID 0x9f /* Read JEDEC ID */
#define SPI_NOR_OPCODE_WREAR 0xc5 /* Write extended address register */
#define SPI_NOR_OPCODE_CHIP_ERASE 0xc7 /* Erase whole flash chip */
#define SPI_NOR_OPCODE_RDEAR 0xc8 /* Read extended address register */

/* Flags for SPI_NOR_OPCODE_READ_STATUS */
#define SPI_NOR_STATUS_REGISTER_WIP BIT(0) /* Write in progres */
#define SPI_NOR_STATUS_REGISTER_WEL BIT(1) /* Write enabled latch */

/* If needed in the future this driver can be extended to discover SFDP
 * advertised erase sizes and opcodes for SFDP v1.0+. */
#define SPI_NOR_DRIVER_SPECIFIED_OPCODE_4KIB_ERASE 0x20
#define SPI_NOR_DRIVER_SPECIFIED_OPCODE_64KIB_ERASE 0xd8

/* If needed in the future this driver can be extended to discover 4B entry and
 * exit methods for SFDP v1.5+. */
#define SPI_NOR_DRIVER_SPECIFIED_OPCODE_ENTER_4B 0xb7
#define SPI_NOR_DRIVER_SPECIFIED_OPCODE_EXIT_4B 0xe9

/* JEDEC JEP106AR specifies 9 Manufacturer ID banks, read 12 to be sure. */
#define SPI_NOR_JEDEC_ID_BANKS 12

/**
 * Initialize the module, assumes the Serial NOR Flash devices are currently
 * all available for initialization. As part of the initialization the driver
 * will check if the part has a compatible SFDP Basic Flash Parameter table
 * and update the part's page_size, capacity, and forces the addressing mode.
 * Parts with more than 16MiB of capacity are initialized into 4B addressing
 * and parts with less are initialized into 3B addressing mode.
 *
 * WARNING: This must successfully return before invoking any other Serial NOR
 * Flash APIs.
 */
int spi_nor_init(void);

/**
 * Forces the Serial NOR Flash device to enter (or exit) 4 Byte addressing mode.
 *
 * WARNING:
 * 1) In 3 Byte addressing mode only 16MiB of Serial NOR Flash is accessible.
 * 2) If there's a second SPI controller communicating with this Serial
 *    NOR Flash part on the board, the user is responsible for ensuring
 *    addressing mode compatibility and cooperation.
 * 3) The user must ensure that multiple users do not trample on each other
 *    by having multiple parties changing the device's addressing mode.
 *
 * @param spi_nor_device The Serial NOR Flash device to use.
 * @param enter_4b_addressing_mode Whether to enter (1) or exit (0) 4B mode.
 * @return ec_error_list (non-zero on error and timeout).
 */
int spi_nor_set_4b_mode(struct spi_nor_device_t *spi_nor_device,
			int enter_4b_addressing_mode);

/**
 * Read JEDEC Identifier.
 *
 * @param spi_nor_device The Serial NOR Flash device to use.
 * @param size Number of Bytes to read.
 * @param data Destination buffer for data.
 * @return ec_error_list (non-zero on error and timeout).
 */
int spi_nor_read_jedec_id(const struct spi_nor_device_t *spi_nor_device,
			  size_t size, uint8_t *data);

/**
 * Read from the Serial NOR Flash device.
 *
 * @param spi_nor_device The Serial NOR Flash device to use.
 * @param offset Flash offset to read.
 * @param size Number of Bytes to read.
 * @param data Destination buffer for data.
 * @return ec_error_list (non-zero on error and timeout).
 */
int spi_nor_read(const struct spi_nor_device_t *spi_nor_device, uint32_t offset,
		 size_t size, uint8_t *data);

/**
 * Erase flash on the Serial Flash Device.
 *
 * @param spi_nor_device The Serial NOR Flash device to use.
 * @param offset Flash offset to erase, must be aligned to the minimum physical
 *               erase size.
 * @param size Number of Bytes to erase, must be a multiple of the the minimum
 *             physical erase size.
 * @return ec_error_list (non-zero on error and timeout).
 */
int spi_nor_erase(const struct spi_nor_device_t *spi_nor_device,
		  uint32_t offset, size_t size);

/**
 * Write to the Serial NOR Flash device. Assumes already erased.
 *
 * @param spi_nor_device The Serial NOR Flash device to use.
 * @param offset Flash offset to write.
 * @param size Number of Bytes to write.
 * @param data Data to write to flash.
 * @return ec_error_list (non-zero on error and timeout).
 */
int spi_nor_write(const struct spi_nor_device_t *spi_nor_device,
		  uint32_t offset, size_t size, const uint8_t *data);

/**
 * Write to the extended address register.
 * @param  spi_nor_device The Serial NOR Flash device to use.
 * @param  value          The value to write.
 * @return                ec_error_list (non-zero on error and timeout).
 */
int spi_nor_write_ear(const struct spi_nor_device_t *spi_nor_device,
		      const uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_SPI_NOR_H */

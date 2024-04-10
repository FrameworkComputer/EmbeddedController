/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SFDP-based Serial NOR flash device module for Chrome EC */

#include "common.h"
#include "console.h"
#include "sfdp.h"
#include "shared_mem.h"
#include "spi.h"
#include "spi_nor.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#ifdef CONFIG_SPI_NOR_DEBUG
#define CPRINTS(dev, string, args...) \
	cprints(CC_SPI, "SPI NOR %s: " string, (dev)->name, ##args)
#else
#define CPRINTS(dev, string, args...)
#endif

/* Time to sleep while serial NOR flash write is in progress. */
#define SPI_NOR_WIP_SLEEP_USEC 10

/* This driver only supports v1.* SFDP. */
#define SPI_NOR_SUPPORTED_SFDP_MAJOR_VERSION 1

/* Ensure a Serial NOR Flash read command in 4B addressing mode fits. */
BUILD_ASSERT(CONFIG_SPI_NOR_MAX_READ_SIZE + 5 <=
	     CONFIG_SPI_NOR_MAX_MESSAGE_SIZE);
/* The maximum write size must be a power of two so it can be used as an
 * emulated maximum page size. */
BUILD_ASSERT(POWER_OF_TWO(CONFIG_SPI_NOR_MAX_WRITE_SIZE));
/* Ensure a Serial NOR Flash page program command in 4B addressing mode fits. */
BUILD_ASSERT(CONFIG_SPI_NOR_MAX_WRITE_SIZE + 5 <=
	     CONFIG_SPI_NOR_MAX_MESSAGE_SIZE);

/* A single mutex is used to protect the single buffer, SPI port, and all of the
 * device mutable board defined device states, if the contention is too high it
 * may be worthwhile to change the global mutex granularity to a finer-grained
 * mutex granularity. */
static struct mutex driver_mutex;

/* Single internal buffer used to stage serial NOR flash commands for the
 * public APIs (read, write, erase). */
static uint8_t buf[CONFIG_SPI_NOR_MAX_MESSAGE_SIZE];

/******************************************************************************/
/* Internal driver functions. */

/**
 * Blocking read of the Serial Flash's first status register.
 */
static int spi_nor_read_status(const struct spi_nor_device_t *spi_nor_device,
			       uint8_t *status_register_value)
{
	uint8_t cmd = SPI_NOR_OPCODE_READ_STATUS;

	return spi_transaction(&spi_devices[spi_nor_device->spi_controller],
			       &cmd, 1, status_register_value, 1);
}

/**
 * Set the write enable latch. Device and shared buffer mutexes must be held!
 */
static int spi_nor_write_enable(const struct spi_nor_device_t *spi_nor_device)
{
	uint8_t cmd = SPI_NOR_OPCODE_WRITE_ENABLE;
	uint8_t status_register_value;
	int rv = EC_SUCCESS;

	/* Set the write enable latch. */
	rv = spi_transaction(&spi_devices[spi_nor_device->spi_controller], &cmd,
			     1, NULL, 0);
	if (rv)
		return rv;

	/* Verify the write enabled latch got set. */
	rv = spi_nor_read_status(spi_nor_device, &status_register_value);
	if (rv)
		return rv;
	if ((status_register_value & SPI_NOR_STATUS_REGISTER_WEL) == 0)
		return EC_ERROR_UNKNOWN; /* WEL not set but should be. */

	return rv;
}

/**
 * Read from the extended address register.
 * @param  spi_nor_device The Serial NOR Flash device to use.
 * @param  value          The value to read to.
 * @return                ec_error_list (non-zero on error and timeout).
 */
static int spi_nor_read_ear(const struct spi_nor_device_t *spi_nor_device,
			    uint8_t *value)
{
	uint8_t command = SPI_NOR_OPCODE_RDEAR;

	return spi_transaction(&spi_devices[spi_nor_device->spi_controller],
			       &command, sizeof(command), value, 1);
}

int spi_nor_write_ear(const struct spi_nor_device_t *spi_nor_device,
		      const uint8_t value)
{
	uint8_t buf[2];
	int rv;
	uint8_t ear;

	mutex_lock(&driver_mutex);

	rv = spi_nor_write_enable(spi_nor_device);
	if (rv) {
		CPRINTS(spi_nor_device, "Failed to write enable");
		goto err_free;
	}

	buf[0] = SPI_NOR_OPCODE_WREAR;
	buf[1] = value;

	rv = spi_transaction(&spi_devices[spi_nor_device->spi_controller], buf,
			     sizeof(buf), NULL, 0);
	if (rv) {
		CPRINTS(spi_nor_device, "Failed to write EAR, rv=%d", rv);
		goto err_free;
	}

	rv = spi_nor_read_ear(spi_nor_device, &ear);
	if (rv)
		goto err_free;

	if (ear != value) {
		CPRINTS(spi_nor_device, "Write EAR error: write=%d, read=%d",
			value, ear);
		rv = EC_ERROR_UNKNOWN; /* WEL not set but should be. */
		goto err_free;
	}

err_free:
	mutex_unlock(&driver_mutex);
	return rv;
}

/**
 * Block until the Serial NOR Flash clears the BUSY/WIP bit in its status reg.
 */
static int spi_nor_wait(const struct spi_nor_device_t *spi_nor_device)
{
	int rv = EC_SUCCESS;
	timestamp_t timeout;
	uint8_t status_register_value;

	rv = spi_nor_read_status(spi_nor_device, &status_register_value);
	if (rv)
		return rv;
	timeout.val = get_time().val + spi_nor_device->timeout_usec;
	while (status_register_value & SPI_NOR_STATUS_REGISTER_WIP) {
		/* Reload the watchdog before sleeping. */
		watchdog_reload();
		crec_usleep(SPI_NOR_WIP_SLEEP_USEC);

		/* Give up if the deadline has been exceeded. */
		if (get_time().val > timeout.val)
			return EC_ERROR_TIMEOUT;

		/* Re-read the status register. */
		rv = spi_nor_read_status(spi_nor_device,
					 &status_register_value);
		if (rv)
			return rv;
	}

	return rv;
}

/**
 * Read the Manufacturer bank and ID out of the JEDEC ID.
 */
static int
spi_nor_read_jedec_mfn_id(const struct spi_nor_device_t *spi_nor_device,
			  uint8_t *out_mfn_bank, uint8_t *out_mfn_id)
{
	int rv = EC_SUCCESS;
	uint8_t jedec_id[SPI_NOR_JEDEC_ID_BANKS];
	size_t i;
	uint8_t cmd = SPI_NOR_OPCODE_JEDEC_ID;

	/* Read the standardized part of the JEDEC ID. */
	rv = spi_transaction(&spi_devices[spi_nor_device->spi_controller], &cmd,
			     1, jedec_id, SPI_NOR_JEDEC_ID_BANKS);
	if (rv)
		return rv;

	*out_mfn_bank = 0;
	/* Go through the JEDEC ID a byte a time to looking for a manufacturer
	 * ID instead of the next bank indicator (0x7F). */
	for (i = 0; i < SPI_NOR_JEDEC_ID_BANKS; i++) {
		*out_mfn_id = jedec_id[i];
		if (*out_mfn_id != 0x7F)
			return EC_SUCCESS;
		*out_mfn_bank += 1;
	}
	/* JEDEC Manufacturer ID should be available, perhaps there is a bus
	 * problem or the JEP106 specification has grown the number of banks? */
	return EC_ERROR_UNKNOWN;
}

/**
 * Read a doubleword out of a SFDP table (DWs are 1-based like the SFDP spec).
 */
static int
spi_nor_read_sfdp_dword(const struct spi_nor_device_t *spi_nor_device,
			uint32_t table_offset, uint8_t table_double_word,
			uint32_t *out_dw)
{
	uint8_t sfdp_cmd[5];
	/* Calculate the byte offset based on the double word. */
	uint32_t sfdp_offset = table_offset + ((table_double_word - 1) * 4);

	/* Read the DW out of the SFDP region. */
	sfdp_cmd[0] = SPI_NOR_OPCODE_SFDP;
	sfdp_cmd[1] = (sfdp_offset & 0xFF0000) >> 16;
	sfdp_cmd[2] = (sfdp_offset & 0xFF00) >> 8;
	sfdp_cmd[3] = (sfdp_offset & 0xFF);
	sfdp_cmd[4] = 0; /* Required extra cycle. */
	return spi_transaction(&spi_devices[spi_nor_device->spi_controller],
			       sfdp_cmd, 5, (uint8_t *)out_dw, 4);
}

/**
 * Returns a bool (1 or 0) based on whether the parameter header double words
 * are for a SFDP v1.* Basic SPI Flash NOR Parameter Table.
 */
static int is_basic_flash_parameter_table(uint8_t sfdp_major_rev,
					  uint8_t sfdp_minor_rev,
					  uint32_t parameter_header_dw1,
					  uint32_t parameter_header_dw2)
{
	if (sfdp_major_rev == 1 && sfdp_minor_rev < 5) {
		return (SFDP_GET_BITFIELD(SFDP_1_0_PARAMETER_HEADER_DW1_ID,
					  parameter_header_dw1) ==
			BASIC_FLASH_PARAMETER_TABLE_1_0_ID);
	} else if (sfdp_major_rev == 1 && sfdp_minor_rev >= 5) {
		return ((SFDP_GET_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW1_ID_LSB,
					   parameter_header_dw1) ==
			 BASIC_FLASH_PARAMETER_TABLE_1_5_ID_LSB) &&
			(SFDP_GET_BITFIELD(SFDP_1_5_PARAMETER_HEADER_DW2_ID_MSB,
					   parameter_header_dw2) ==
			 BASIC_FLASH_PARAMETER_TABLE_1_5_ID_MSB));
	}

	return 0;
}

/**
 * Helper function to locate the SFDP Basic SPI Flash NOR Parameter Table.
 */
static int locate_sfdp_basic_parameter_table(
	const struct spi_nor_device_t *spi_nor_device,
	uint8_t *out_sfdp_major_rev, uint8_t *out_sfdp_minor_rev,
	uint8_t *out_table_major_rev, uint8_t *out_table_minor_rev,
	uint32_t *out_table_offset, size_t *out_table_size)
{
	int rv = EC_SUCCESS;
	uint8_t number_parameter_headers;
	uint32_t table_offset = 0;
	int table_found = 0;
	uint32_t dw1;
	uint32_t dw2;

	/* Read the SFDP header. */
	rv = spi_nor_read_sfdp_dword(spi_nor_device, 0, 1, &dw1);
	rv |= spi_nor_read_sfdp_dword(spi_nor_device, 0, 2, &dw2);
	if (rv)
		return rv;

	/* Ensure the SFDP table is valid. Note the versions are not checked
	 * through the SFDP table header, as there may be a backwards
	 * compatible, older basic parameter tables which are compatible with
	 * this driver in the parameter headers. */
	if (!SFDP_HEADER_DW1_SFDP_SIGNATURE_VALID(dw1)) {
		CPRINTS(spi_nor_device, "SFDP signature invalid");
		return EC_ERROR_UNKNOWN;
	}

	*out_sfdp_major_rev =
		SFDP_GET_BITFIELD(SFDP_HEADER_DW2_SFDP_MAJOR, dw2);
	*out_sfdp_minor_rev =
		SFDP_GET_BITFIELD(SFDP_HEADER_DW2_SFDP_MINOR, dw2);
	CPRINTS(spi_nor_device, "SFDP v%d.%d discovered", *out_sfdp_major_rev,
		*out_sfdp_minor_rev);

	/* NPH is 0-based, so add 1. */
	number_parameter_headers =
		SFDP_GET_BITFIELD(SFDP_HEADER_DW2_NPH, dw2) + 1;
	CPRINTS(spi_nor_device, "There are %d SFDP parameter headers",
		number_parameter_headers);

	/* Search for the newest, compatible basic flash parameter table. */
	*out_table_major_rev = 0;
	*out_table_minor_rev = 0;
	while (number_parameter_headers) {
		uint8_t major_rev, minor_rev;

		table_offset += 8;
		number_parameter_headers--;

		/* Read this parameter header's two dwords. */
		rv = spi_nor_read_sfdp_dword(spi_nor_device, table_offset, 1,
					     &dw1);
		rv |= spi_nor_read_sfdp_dword(spi_nor_device, table_offset, 2,
					      &dw2);
		if (rv)
			return rv;

		/* Ensure it's the basic flash parameter table. */
		if (!is_basic_flash_parameter_table(
			    *out_sfdp_major_rev, *out_sfdp_minor_rev, dw1, dw2))
			continue;

		/* The parameter header major and minor versioning is still the
		 * same as SFDP 1.0. */
		major_rev = SFDP_GET_BITFIELD(
			SFDP_1_0_PARAMETER_HEADER_DW1_TABLE_MAJOR, dw1);
		minor_rev = SFDP_GET_BITFIELD(
			SFDP_1_0_PARAMETER_HEADER_DW1_TABLE_MINOR, dw1);

		/* Skip incompatible parameter tables. */
		if (major_rev != SPI_NOR_SUPPORTED_SFDP_MAJOR_VERSION)
			continue;

		/* If this parameter table has a lower revision compared to a
		 * previously found compatible table, skip it. */
		if (minor_rev < *out_table_minor_rev)
			continue;

		table_found = 1;
		*out_table_major_rev = major_rev;
		*out_table_minor_rev = minor_rev;
		/* The parameter header ptp and ptl are still the same as
		 * SFDP 1.0. */
		*out_table_offset = SFDP_GET_BITFIELD(
			SFDP_1_0_PARAMETER_HEADER_DW2_PTP, dw2);
		/* Convert the size from DW to Bytes. */
		*out_table_size =
			SFDP_GET_BITFIELD(SFDP_1_0_PARAMETER_HEADER_DW1_PTL,
					  dw1) *
			4;
	}

	if (!table_found) {
		CPRINTS(spi_nor_device,
			"No compatible Basic Flash Parameter Table found");
		return EC_ERROR_UNKNOWN;
	}

	CPRINTS(spi_nor_device, "Using Basic Flash Parameter Table v%d.%d",
		*out_sfdp_major_rev, *out_sfdp_minor_rev);

	return EC_SUCCESS;
}

/**
 * Helper function to lookup the part's page size in the SFDP Basic SPI Flash
 * NOR Parameter Table.
 */
static int spi_nor_device_discover_sfdp_page_size(
	struct spi_nor_device_t *spi_nor_device,
	uint8_t basic_parameter_table_major_version,
	uint8_t basic_parameter_table_minor_version,
	uint32_t basic_parameter_table_offset, size_t *page_size)
{
	int rv = EC_SUCCESS;
	uint32_t dw;

	if (basic_parameter_table_major_version == 1 &&
	    basic_parameter_table_minor_version < 5) {
		/* Use the Basic Flash Parameter v1.0 page size reporting. */
		rv = spi_nor_read_sfdp_dword(
			spi_nor_device, basic_parameter_table_offset, 1, &dw);
		if (rv)
			return rv;
		if (SFDP_GET_BITFIELD(BFPT_1_0_DW1_WRITE_GRANULARITY, dw))
			*page_size = 64;
		else
			*page_size = 1;

	} else if (basic_parameter_table_major_version == 1 &&
		   basic_parameter_table_minor_version >= 5) {
		/* Use the Basic Flash Parameter v1.5 page size reporting. */
		rv = spi_nor_read_sfdp_dword(
			spi_nor_device, basic_parameter_table_offset, 11, &dw);
		if (rv)
			return rv;
		*page_size = 1
			     << SFDP_GET_BITFIELD(BFPT_1_5_DW11_PAGE_SIZE, dw);
	}

	return EC_SUCCESS;
}

/**
 * Helper function to lookup the part's capacity in the SFDP Basic SPI Flash
 * NOR Parameter Table.
 */
static int spi_nor_device_discover_sfdp_capacity(
	struct spi_nor_device_t *spi_nor_device,
	uint8_t basic_parameter_table_major_version,
	uint8_t basic_parameter_table_minor_version,
	uint32_t basic_parameter_table_offset, uint32_t *capacity)
{
	int rv = EC_SUCCESS;
	uint32_t dw;

	/* First attempt to discover the device's capacity. */
	if (basic_parameter_table_major_version == 1) {
		/* Use the Basic Flash Parameter v1.0 capacity reporting. */
		rv = spi_nor_read_sfdp_dword(
			spi_nor_device, basic_parameter_table_offset, 2, &dw);
		if (rv)
			return rv;

		if (SFDP_GET_BITFIELD(BFPT_1_0_DW2_GT_2_GIBIBITS, dw)) {
			/* Ensure the capacity is less than 4GiB. */
			uint64_t tmp_capacity =
				1
				<< (SFDP_GET_BITFIELD(BFPT_1_0_DW2_N, dw) - 3);
			if (tmp_capacity > UINT32_MAX)
				return EC_ERROR_OVERFLOW;
			*capacity = tmp_capacity;
		} else {
			*capacity =
				1 +
				(SFDP_GET_BITFIELD(BFPT_1_0_DW2_N, dw) >> 3);
		}
	}

	return EC_SUCCESS;
}

static int spi_nor_read_internal(const struct spi_nor_device_t *spi_nor_device,
				 uint32_t offset, size_t size, uint8_t *data)
{
	int rv;

	/* Split up the read operation into multiple transactions if the size
	 * is larger than the maximum read size.
	 */
	while (size > 0) {
		size_t read_size = MIN(size, CONFIG_SPI_NOR_MAX_READ_SIZE);
		size_t read_command_size;

		/* Set up the read command in the TX buffer. */
		buf[0] = SPI_NOR_OPCODE_SLOW_READ;
		if (spi_nor_device->in_4b_addressing_mode) {
			buf[1] = (offset & 0xFF000000) >> 24;
			buf[2] = (offset & 0xFF0000) >> 16;
			buf[3] = (offset & 0xFF00) >> 8;
			buf[4] = (offset & 0xFF);
			read_command_size = 5;
		} else { /* in 3 byte addressing mode */
			buf[1] = (offset & 0xFF0000) >> 16;
			buf[2] = (offset & 0xFF00) >> 8;
			buf[3] = (offset & 0xFF);
			read_command_size = 4;
		}

		rv = spi_transaction(
			&spi_devices[spi_nor_device->spi_controller], buf,
			read_command_size, data, read_size);
		if (rv)
			return rv;

		data += read_size;
		offset += read_size;
		size -= read_size;
	}
	return EC_SUCCESS;
}

/******************************************************************************/
/* External Serial NOR Flash API available to other modules. */

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
int spi_nor_init(void)
{
	int rv = EC_SUCCESS;
	size_t i;

	/* Initialize the state for each serial NOR flash device. */
	for (i = 0; i < SPI_NOR_DEVICE_COUNT; i++) {
		uint8_t sfdp_major_rev, sfdp_minor_rev;
		uint8_t table_major_rev, table_minor_rev;
		uint32_t table_offset;
		size_t table_size;
		struct spi_nor_device_t *spi_nor_device = &spi_nor_devices[i];

		rv |= locate_sfdp_basic_parameter_table(
			spi_nor_device, &sfdp_major_rev, &sfdp_minor_rev,
			&table_major_rev, &table_minor_rev, &table_offset,
			&table_size);

		/* If we failed to find a compatible SFDP Basic Flash Parameter
		 * table, use the default capacity, page size, and addressing
		 * mode values. */
		if (rv == EC_SUCCESS) {
			size_t page_size = 0;
			uint32_t capacity = 0;

			rv |= spi_nor_device_discover_sfdp_page_size(
				spi_nor_device, table_major_rev,
				table_minor_rev, table_offset, &page_size);
			rv |= spi_nor_device_discover_sfdp_capacity(
				spi_nor_device, table_major_rev,
				table_minor_rev, table_offset, &capacity);
			if (rv == EC_SUCCESS) {
				mutex_lock(&driver_mutex);
				spi_nor_device->capacity = capacity;
				spi_nor_device->page_size = page_size;
				CPRINTS(spi_nor_device,
					"Updated to SFDP params: %dKiB w/ %dB pages",
					spi_nor_device->capacity >> 10,
					spi_nor_device->page_size);
				mutex_unlock(&driver_mutex);
			}
		}

		/* Ensure the device is in a determined addressing state by
		 * forcing a 4B addressing mode entry or exit depending on the
		 * device capacity. If the device is larger than 16MiB, enter
		 * 4B addressing mode. */
		rv |= spi_nor_set_4b_mode(spi_nor_device,
					  spi_nor_device->capacity > 0x1000000);
	}

	return rv;
}

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
			int enter_4b_addressing_mode)
{
	uint8_t cmd;
	int rv;

	rv = spi_nor_write_enable(spi_nor_device);
	if (rv)
		return rv;

	if (enter_4b_addressing_mode)
		cmd = SPI_NOR_DRIVER_SPECIFIED_OPCODE_ENTER_4B;
	else
		cmd = SPI_NOR_DRIVER_SPECIFIED_OPCODE_EXIT_4B;

	/* Claim the driver mutex to modify the device state. */
	mutex_lock(&driver_mutex);

	rv = spi_transaction(&spi_devices[spi_nor_device->spi_controller], &cmd,
			     1, NULL, 0);
	if (rv == EC_SUCCESS) {
		spi_nor_device->in_4b_addressing_mode =
			enter_4b_addressing_mode;
	}

	CPRINTS(spi_nor_device, "Entered %s Addressing Mode",
		enter_4b_addressing_mode ? "4-Byte" : "3-Byte");

	/* Release the driver mutex. */
	mutex_unlock(&driver_mutex);
	return rv;
}

/**
 * Read JEDEC Identifier.
 *
 * @param spi_nor_device The Serial NOR Flash device to use.
 * @param size Number of Bytes to read.
 * @param data Destination buffer for data.
 * @return ec_error_list (non-zero on error and timeout).
 */
int spi_nor_read_jedec_id(const struct spi_nor_device_t *spi_nor_device,
			  size_t size, uint8_t *data)
{
	int rv;
	uint8_t cmd = SPI_NOR_OPCODE_JEDEC_ID;

	if (size > CONFIG_SPI_NOR_MAX_READ_SIZE)
		return EC_ERROR_INVAL;
	/* Claim the driver mutex. */
	mutex_lock(&driver_mutex);
	/* Read the JEDEC ID. */
	rv = spi_transaction(&spi_devices[spi_nor_device->spi_controller], &cmd,
			     1, data, size);
	/* Release the driver mutex. */
	mutex_unlock(&driver_mutex);

	return rv;
}

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
		 size_t size, uint8_t *data)
{
	int rv;

	/* Claim the driver mutex. */
	mutex_lock(&driver_mutex);
	rv = spi_nor_read_internal(spi_nor_device, offset, size, data);
	/* Release the driver mutex. */
	mutex_unlock(&driver_mutex);

	return rv;
}

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
		  uint32_t offset, size_t size)
{
	int rv = EC_SUCCESS;
	size_t erase_command_size, erase_size;
	uint8_t erase_opcode;
#ifdef CONFIG_SPI_NOR_SMART_ERASE
	BUILD_ASSERT((CONFIG_SPI_NOR_MAX_READ_SIZE % 4) == 0);
	uint8_t buffer[CONFIG_SPI_NOR_MAX_READ_SIZE] __aligned(4);
	size_t verify_offset, read_offset, read_size, read_left;
#endif

	/* Invalid input */
	if ((offset % 4096 != 0) || (size % 4096 != 0) || (size < 4096))
		return EC_ERROR_INVAL;

	/* Claim the driver mutex. */
	mutex_lock(&driver_mutex);

	while (size > 0) {
		erase_opcode = SPI_NOR_DRIVER_SPECIFIED_OPCODE_4KIB_ERASE;
		erase_size = 4096;

		/* Wait for the previous operation to finish. */
		rv = spi_nor_wait(spi_nor_device);
		if (rv)
			goto err_free;

#ifdef CONFIG_SPI_NOR_BLOCK_ERASE
		if (!(offset % 65536) && size >= 65536) {
			erase_opcode =
				SPI_NOR_DRIVER_SPECIFIED_OPCODE_64KIB_ERASE;
			erase_size = 65536;
		}
#endif
#ifdef CONFIG_SPI_NOR_SMART_ERASE
		read_offset = offset;
		read_left = erase_size;
		while (read_left) {
			read_size =
				MIN(read_left, CONFIG_SPI_NOR_MAX_READ_SIZE);
			/* Since CONFIG_SPI_NOR_MAX_READ_SIZE & erase_size are
			 * both guaranteed to be multiples of 4.
			 */
			assert(read_size >= 4 && (read_size % 4) == 0);
			rv = spi_nor_read_internal(spi_nor_device, read_offset,
						   read_size, buffer);

			/* Note: the return value here is lost below
			 * at the write enable, this is not a problem,
			 * as this code is only an optimisation, if it
			 * fails, the full erase functionality still
			 * gets done, and the error from that returned
			 */
			if (rv != EC_SUCCESS)
				break;
			/* Aligned word verify reduced the overall (read +
			 * verify) time by ~20% (vs bytewise verify) on
			 * an m3@24MHz & SPI@24MHz.
			 */
			verify_offset = 0;
			while (verify_offset <= read_size - 4) {
				if (*(uint32_t *)(buffer + verify_offset) !=
				    0xffffffff) {
					break;
				}
				verify_offset += 4;
			}
			if (verify_offset != read_size)
				break;
			read_offset += read_size;
			read_left -= read_size;
			watchdog_reload();
		}
		if (!read_left) {
			/* Sector/block already erased. */
			CPRINTS(spi_nor_device,
				"Skipping erase [%x:%x] "
				"(already erased)",
				offset, erase_size);
			offset += erase_size;
			size -= erase_size;
			continue;
		}
#endif
		/* Enable writing to serial NOR flash. */
		rv = spi_nor_write_enable(spi_nor_device);
		if (rv)
			goto err_free;

		/* Set up the erase instruction. */
		buf[0] = erase_opcode;
		if (spi_nor_device->in_4b_addressing_mode) {
			buf[1] = (offset & 0xFF000000) >> 24;
			buf[2] = (offset & 0xFF0000) >> 16;
			buf[3] = (offset & 0xFF00) >> 8;
			buf[4] = (offset & 0xFF);
			erase_command_size = 5;
		} else { /* in 3 byte addressing mode */
			buf[1] = (offset & 0xFF0000) >> 16;
			buf[2] = (offset & 0xFF00) >> 8;
			buf[3] = (offset & 0xFF);
			erase_command_size = 4;
		}

		rv = spi_transaction(
			&spi_devices[spi_nor_device->spi_controller], buf,
			erase_command_size, NULL, 0);
		if (rv)
			goto err_free;

		offset += erase_size;
		size -= erase_size;
	}

	/* Wait for the previous operation to finish. */
	rv = spi_nor_wait(spi_nor_device);

err_free:
	/* Release the driver mutex. */
	mutex_unlock(&driver_mutex);

	return rv;
}

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
		  uint32_t offset, size_t size, const uint8_t *data)
{
	int rv = EC_SUCCESS;
	size_t effective_page_size;

	/* Claim the driver mutex. */
	mutex_lock(&driver_mutex);

	/* Ensure the device's page size fits in the driver's buffer, if not
	 * emulate a smaller page size based on the buffer size. */
	effective_page_size =
		MIN(spi_nor_device->page_size, CONFIG_SPI_NOR_MAX_WRITE_SIZE);

	/* Split the write into multiple writes if the size is too large. */
	while (size > 0) {
		size_t prefix_size;
		/* Figure out the size of the next write within 1 page. */
		uint32_t page_offset = offset & (effective_page_size - 1);
		size_t write_size =
			MIN(size, effective_page_size - page_offset);

		/* Wait for the previous operation to finish. */
		rv = spi_nor_wait(spi_nor_device);
		if (rv)
			goto err_free;

		/* Enable writing to serial NOR flash. */
		rv = spi_nor_write_enable(spi_nor_device);
		if (rv)
			goto err_free;

		/* Set up the page program command. */
		buf[0] = SPI_NOR_OPCODE_PAGE_PROGRAM;
		if (spi_nor_device->in_4b_addressing_mode) {
			buf[1] = (offset & 0xFF000000) >> 24;
			buf[2] = (offset & 0xFF0000) >> 16;
			buf[3] = (offset & 0xFF00) >> 8;
			buf[4] = (offset & 0xFF);
			prefix_size = 5;
		} else { /* in 3 byte addressing mode */
			buf[1] = (offset & 0xFF0000) >> 16;
			buf[2] = (offset & 0xFF00) >> 8;
			buf[3] = (offset & 0xFF);
			prefix_size = 4;
		}
		/* Copy data to write into the buffer after the prefix. */
		memmove(buf + prefix_size, data, write_size);

		rv = spi_transaction(
			&spi_devices[spi_nor_device->spi_controller], buf,
			prefix_size + write_size, NULL, 0);
		if (rv)
			goto err_free;

		data += write_size;
		offset += write_size;
		size -= write_size;
	}

	/* Wait for the previous operation to finish. */
	rv = spi_nor_wait(spi_nor_device);

err_free:
	/* Release the driver mutex. */
	mutex_unlock(&driver_mutex);

	return rv;
}

/******************************************************************************/
/* Serial NOR Flash console commands. */

#ifdef CONFIG_CMD_SPI_NOR
static int command_spi_nor_info(int argc, const char **argv)
{
	int rv = EC_SUCCESS;

	uint8_t sfdp_major_rev, sfdp_minor_rev;
	uint8_t table_major_rev, table_minor_rev;
	uint32_t table_offset;
	uint8_t mfn_bank = 0, mfn_id = 0;
	size_t table_size;
	const struct spi_nor_device_t *spi_nor_device = 0;
	int spi_nor_device_index = 0;
	int spi_nor_device_index_limit = spi_nor_devices_used - 1;

	/* Set the device index limits if a device was specified. */
	if (argc == 2) {
		spi_nor_device_index = strtoi(argv[1], NULL, 0);
		if (spi_nor_device_index >= spi_nor_devices_used)
			return EC_ERROR_PARAM1;
		spi_nor_device_index_limit = spi_nor_device_index;
	} else if (argc != 1) {
		return EC_ERROR_PARAM_COUNT;
	}

	for (; spi_nor_device_index <= spi_nor_device_index_limit;
	     spi_nor_device_index++) {
		spi_nor_device = &spi_nor_devices[spi_nor_device_index];

		ccprintf("Serial NOR Flash Device %d:\n", spi_nor_device_index);
		ccprintf("\tName: %s\n", spi_nor_device->name);
		ccprintf("\tSPI controller index: %d\n",
			 spi_nor_device->spi_controller);
		ccprintf("\tTimeout: %d uSec\n", spi_nor_device->timeout_usec);
		ccprintf("\tCapacity: %d KiB\n",
			 spi_nor_device->capacity >> 10),
			ccprintf("\tAddressing: %s addressing mode\n",
				 spi_nor_device->in_4b_addressing_mode ? "4B" :
									 "3B");
		ccprintf("\tPage Size: %d Bytes\n", spi_nor_device->page_size);

		/* Get JEDEC ID info. */
		rv = spi_nor_read_jedec_mfn_id(spi_nor_device, &mfn_bank,
					       &mfn_id);
		if (rv != EC_SUCCESS)
			return rv;
		ccprintf("\tJEDEC ID bank %d manufacturing code 0x%x\n",
			 mfn_bank, mfn_id);

		/* Get SFDP info. */
		if (locate_sfdp_basic_parameter_table(
			    spi_nor_device, &sfdp_major_rev, &sfdp_minor_rev,
			    &table_major_rev, &table_minor_rev, &table_offset,
			    &table_size) != EC_SUCCESS) {
			ccputs("\tNo JEDEC SFDP support detected\n");
			continue; /* Go on to the next device. */
		}
		ccprintf("\tSFDP v%d.%d\n", sfdp_major_rev, sfdp_minor_rev);
		ccprintf("\tFlash Parameter Table v%d.%d (%dB @ 0x%x)\n",
			 table_major_rev, table_minor_rev, table_size,
			 table_offset);
	}

	return rv;
}
DECLARE_CONSOLE_COMMAND(spinorinfo, command_spi_nor_info, "[device]",
			"Report Serial NOR Flash device information");
#endif /* CONFIG_CMD_SPI_NOR */

#ifdef CONFIG_CMD_SPI_NOR
static int command_spi_nor_erase(int argc, const char **argv)
{
	const struct spi_nor_device_t *spi_nor_device;
	int spi_nor_device_index;
	int offset = 0;
	int size = 4096;
	int rv;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	spi_nor_device_index = strtoi(argv[1], NULL, 0);
	if (spi_nor_device_index >= spi_nor_devices_used)
		return EC_ERROR_PARAM1;
	spi_nor_device = &spi_nor_devices[spi_nor_device_index];

	rv = parse_offset_size(argc, argv, 2, &offset, &size);
	if (rv)
		return rv;

	ccprintf("Erasing %d bytes at 0x%x on %s...\n", size, offset,
		 spi_nor_device->name);
	return spi_nor_erase(spi_nor_device, offset, size);
}
DECLARE_CONSOLE_COMMAND(spinorerase, command_spi_nor_erase,
			"device [offset] [size]", "Erase flash");
#endif /* CONFIG_CMD_SPI_NOR */

#ifdef CONFIG_CMD_SPI_NOR
static int command_spi_nor_write(int argc, const char **argv)
{
	const struct spi_nor_device_t *spi_nor_device;
	int spi_nor_device_index;
	int offset = 0;
	int size = CONFIG_SPI_NOR_MAX_WRITE_SIZE;
	int rv;
	char *data;
	int i;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	spi_nor_device_index = strtoi(argv[1], NULL, 0);
	if (spi_nor_device_index >= spi_nor_devices_used)
		return EC_ERROR_PARAM1;
	spi_nor_device = &spi_nor_devices[spi_nor_device_index];

	rv = parse_offset_size(argc, argv, 2, &offset, &size);
	if (rv)
		return rv;

	if (size > shared_mem_size())
		size = shared_mem_size();

	/* Acquire the shared memory buffer */
	rv = shared_mem_acquire(size, &data);
	if (rv) {
		ccputs("Can't get shared mem\n");
		return rv;
	}

	/* Fill the data buffer with a pattern */
	for (i = 0; i < size; i++)
		data[i] = i;

	ccprintf("Writing %d bytes to 0x%x on %s...\n", size, offset,
		 spi_nor_device->name);
	rv = spi_nor_write(spi_nor_device, offset, size, data);

	/* Free the buffer */
	shared_mem_release(data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(spinorwrite, command_spi_nor_write,
			"device [offset] [size]", "Write pattern to flash");
#endif /* CONFIG_CMD_SPI_NOR */

#ifdef CONFIG_CMD_SPI_NOR
static int command_spi_nor_read(int argc, const char **argv)
{
	const struct spi_nor_device_t *spi_nor_device;
	int spi_nor_device_index;
	int offset = 0;
	int size = CONFIG_SPI_NOR_MAX_READ_SIZE;
	int rv;
	char *data;
	int i;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	spi_nor_device_index = strtoi(argv[1], NULL, 0);
	if (spi_nor_device_index >= spi_nor_devices_used)
		return EC_ERROR_PARAM1;
	spi_nor_device = &spi_nor_devices[spi_nor_device_index];

	rv = parse_offset_size(argc, argv, 2, &offset, &size);
	if (rv)
		return rv;

	if (size > shared_mem_size())
		size = shared_mem_size();

	/* Acquire the shared memory buffer */
	rv = shared_mem_acquire(size, &data);
	if (rv) {
		ccputs("Can't get shared mem\n");
		return rv;
	}

	/* Read the data */
	ccprintf("Reading %d bytes from %s...", size, spi_nor_device->name);
	if (spi_nor_read(spi_nor_device, offset, size, data)) {
		rv = EC_ERROR_INVAL;
		goto err_free;
	}

	/* Dump it */
	for (i = 0; i < size; i++) {
		if ((offset + i) % 16) {
			ccprintf(" %02x", data[i]);
		} else {
			ccprintf("\n%08x: %02x", offset + i, data[i]);
			cflush();
		}
	}
	ccprintf("\n");

err_free:
	/* Free the buffer */
	shared_mem_release(data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(spinorread, command_spi_nor_read,
			"device [offset] [size]", "Read flash");
#endif /* CONFIG_CMD_SPI_NOR */

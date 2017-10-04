/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cros Board Info
 */

#include "common.h"
#include "console.h"
#include "crc8.h"
#include "cros_board_info.h"
#include "i2c.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, "CBI " format, ## args)

static struct board_info bi;
/* TODO: Init it to -1. On error (I2C or bad contents), retry a read and set it
 * to enum ec_error_list if it still fails. The successive calls can be
 * immediately returned with the cached error code. This will avoid attempting
 * reads doomed to fail. */
static int initialized;

static uint8_t cbi_crc8(const struct board_info *bi)
{
	return crc8((uint8_t *)&bi->head.crc + 1, bi->head.total_size - 4);
}

/*
 * Get board information from EEPROM
 */
static int read_board_info(void)
{
	uint8_t buf[256];
	uint8_t offset;

	if (initialized)
		return EC_SUCCESS;

	CPRINTS("Reading board info");

	/* Read header */
	offset = 0;
	if (i2c_xfer(I2C_PORT_EEPROM, I2C_ADDR_EEPROM,
		     &offset, 1, buf, sizeof(bi.head), I2C_XFER_SINGLE)) {
		CPRINTS("Failed to read header");
		return EC_ERROR_INVAL;
	}
	memcpy(&bi.head, buf, sizeof(bi.head));

	/* Check magic */
	if (memcmp(bi.head.magic, cbi_magic, sizeof(bi.head.magic))) {
		CPRINTS("Bad magic");
		return EC_ERROR_INVAL;
	}

	/* check version */
	if (bi.head.major_version > CBI_VERSION_MAJOR) {
		CPRINTS("Version mismatch");
		return EC_ERROR_INVAL;
	}

	/* Check the data size. It's expected to support up to 64k but our
	 * buffer has practical limitation. */
	if (bi.head.total_size < sizeof(bi) ||
			bi.head.total_size > sizeof(buf)) {
		CPRINTS("Bad size: %d", bi.head.total_size);
		return EC_ERROR_OVERFLOW;
	}

	/* Read the rest */
	offset = sizeof(bi.head);
	if (i2c_xfer(I2C_PORT_EEPROM, I2C_ADDR_EEPROM, &offset, 1,
		     buf + sizeof(bi.head),
		     bi.head.total_size - sizeof(bi.head),
		     I2C_XFER_SINGLE)) {
		CPRINTS("Failed to read body");
		return EC_ERROR_INVAL;
	}

	/* Check CRC. This supports new fields unknown to this parser. */
	if (cbi_crc8((struct board_info *)&buf) != bi.head.crc) {
		CPRINTS("Bad CRC");
		return EC_ERROR_INVAL;
	}

	/* Save only the data we understand. */
	memcpy(&bi.head + 1, &buf[sizeof(bi.head)],
	       sizeof(bi) - sizeof(bi.head));
	/* If we're handling previous version, clear all new fields */

	initialized = 1;

	return EC_SUCCESS;
}

int cbi_get_board_version(uint32_t *version)
{
	if (read_board_info())
		return EC_ERROR_UNKNOWN;
	*version = bi.version;
	return EC_SUCCESS;
}

int cbi_get_sku_id(uint32_t *sku_id)
{
	if (read_board_info())
		return EC_ERROR_UNKNOWN;
	*sku_id = bi.sku_id;
	return EC_SUCCESS;
}

int cbi_get_oem_id(uint32_t *oem_id)
{
	if (read_board_info())
		return EC_ERROR_UNKNOWN;
	*oem_id = bi.oem_id;
	return EC_SUCCESS;
}

int board_get_version(void)
{
	uint32_t version;
	if (cbi_get_board_version(&version))
		return -1;
	return version;
}

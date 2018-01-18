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
#include "gpio.h"
#include "host_command.h"
#include "i2c.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, "CBI " format, ## args)

#define EEPROM_PAGE_WRITE_SIZE	16
#define EC_ERROR_CBI_CACHE_INVALID	EC_ERROR_INTERNAL_FIRST
static struct board_info bi;
static int cached_read_result = EC_ERROR_CBI_CACHE_INVALID;

static uint8_t cbi_crc8(const struct board_info *bi)
{
	return crc8((uint8_t *)&bi->head.crc + 1, bi->head.total_size - 4);
}

/*
 * Get board information from EEPROM
 */
static int do_read_board_info(void)
{
	uint8_t buf[256];
	uint8_t offset;

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

	return EC_SUCCESS;
}

static int read_board_info(void)
{
	if (cached_read_result != EC_ERROR_CBI_CACHE_INVALID)
		/* We already tried and know the result. Return the cached
		 * error code immediately to avoid wasteful reads. */
		return cached_read_result;

	cached_read_result = do_read_board_info();
	if (cached_read_result)
		/* On error (I2C or bad contents), retry a read */
		cached_read_result = do_read_board_info();

	return cached_read_result;
}

static int eeprom_is_write_protected(void)
{
	return !gpio_get_level(GPIO_WP_L);
}

static int write_board_info(void)
{
	uint8_t buf[sizeof(bi) + 1];
	/* The code is only tested for ST M24C02, whose page size for a single
	 * write is 16 byte. To support different EEPROMs, you may need to
	 * craft the i2c packets accordingly. */
	_Static_assert(sizeof(bi) <= EEPROM_PAGE_WRITE_SIZE,
		       "struct board_info exceeds page write size");
	int rv;

	if (eeprom_is_write_protected()) {
		CPRINTS("Failed to write for WP");
		return EC_ERROR_ACCESS_DENIED;
	}

	buf[0] = 0;	/* Offset 0 */
	memcpy(&buf[1], &bi, sizeof(bi));
	rv = i2c_xfer(I2C_PORT_EEPROM, I2C_ADDR_EEPROM, buf,
		     sizeof(bi) + 1, NULL, 0, I2C_XFER_SINGLE);
	if (rv) {
		CPRINTS("Failed to write for %d", rv);
		return rv;
	}

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

static int hc_cbi_get(struct host_cmd_handler_args *args)
{
	const struct __ec_align4 ec_params_get_cbi *p = args->params;

	if (p->flag & CBI_GET_RELOAD)
		cached_read_result = EC_ERROR_CBI_CACHE_INVALID;

	if (read_board_info())
		return EC_RES_ERROR;

	switch (p->type) {
	case CBI_DATA_BOARD_VERSION:
		*(uint32_t *)args->response = bi.version;
		break;
	case CBI_DATA_OEM_ID:
		*(uint32_t *)args->response = bi.oem_id;
		break;
	case CBI_DATA_SKU_ID:
		*(uint32_t *)args->response = bi.sku_id;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}
	args->response_size = sizeof(uint32_t);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_CROS_BOARD_INFO,
		     hc_cbi_get,
		     EC_VER_MASK(0));

static int hc_cbi_set(struct host_cmd_handler_args *args)
{
	const struct __ec_align4 ec_params_set_cbi *p = args->params;

	if (p->flag & CBI_SET_INIT) {
		memset(&bi, 0, sizeof(bi));
		memcpy(&bi.head.magic, cbi_magic, sizeof(cbi_magic));
		cached_read_result = EC_SUCCESS;
	} else {
		if (read_board_info())
			return EC_RES_ERROR;
	}

	switch (p->type) {
	case CBI_DATA_BOARD_VERSION:
		if (p->data > UINT16_MAX)
			return EC_RES_INVALID_PARAM;
		bi.version = p->data;
		break;
	case CBI_DATA_OEM_ID:
		if (p->data > UINT8_MAX)
			return EC_RES_INVALID_PARAM;
		bi.oem_id = p->data;
		break;
	case CBI_DATA_SKU_ID:
		if (p->data > UINT8_MAX)
			return EC_RES_INVALID_PARAM;
		bi.sku_id = p->data;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	/* Whether we're modifying existing data or creating new one,
	 * we take over the format. */
	bi.head.major_version = CBI_VERSION_MAJOR;
	bi.head.minor_version = CBI_VERSION_MINOR;
	bi.head.total_size = sizeof(bi);
	bi.head.crc = cbi_crc8(&bi);

	/* Skip write if client asks so. */
	if (p->flag & CBI_SET_NO_SYNC)
		return EC_RES_SUCCESS;

	if (write_board_info())
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SET_CROS_BOARD_INFO,
		     hc_cbi_set,
		     EC_VER_MASK(0));

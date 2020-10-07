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
#include "timer.h"

#ifdef HOST_TOOLS_BUILD
#include <string.h>
#else
#include "util.h"
#endif

/*
 * Functions and variables defined here shared with host tools (e.g. cbi-util).
 * TODO: Move these to common/cbi/cbi.c and common/cbi/utils.c if they grow.
 */
uint8_t cbi_crc8(const struct cbi_header *h)
{
	return crc8((uint8_t *)&h->crc + 1,
		    h->total_size - sizeof(h->magic) - sizeof(h->crc));
}

uint8_t *cbi_set_data(uint8_t *p, enum cbi_data_tag tag,
		      const void *buf, int size)
{
	struct cbi_data *d = (struct cbi_data *)p;

	/*
	 * If size of the data to be added is zero, then no need to add the tag
	 * as well.
	 */
	if (size == 0)
		return p;

	d->tag = tag;
	d->size = size;
	memcpy(d->value, buf, size);
	p += sizeof(*d) + size;
	return p;
}

uint8_t *cbi_set_string(uint8_t *p, enum cbi_data_tag tag, const char *str)
{
	if (str == NULL)
		return p;

	return cbi_set_data(p, tag, str, strlen(str) + 1);
}

struct cbi_data *cbi_find_tag(const void *buf, enum cbi_data_tag tag)
{
	struct cbi_data *d;
	const struct cbi_header *h = buf;
	const uint8_t *p;
	for (p = h->data; p + sizeof(*d) < (uint8_t *)buf + h->total_size;) {
		d = (struct cbi_data *)p;
		if (d->tag == tag)
			return d;
		p += sizeof(*d) + d->size;
	}
	return NULL;
}

/*
 * Functions and variables specific to EC firmware
 */
#ifndef HOST_TOOLS_BUILD

#define CPRINTS(format, args...) cprints(CC_SYSTEM, "CBI " format, ## args)

/*
 * We allow EEPROMs with page size of 8 or 16. Use 8 to be the most compatible.
 * This causes a little more overhead for writes, but we are not writing to the
 * EEPROM outside of the factory process.
 */
#define EEPROM_PAGE_WRITE_SIZE	8

#define EEPROM_PAGE_WRITE_MS	5
#define EC_ERROR_CBI_CACHE_INVALID	EC_ERROR_INTERNAL_FIRST

static int cached_read_result = EC_ERROR_CBI_CACHE_INVALID;
static uint8_t cbi[CBI_EEPROM_SIZE];
static struct cbi_header * const head = (struct cbi_header *)cbi;

int cbi_create(void)
{
	struct cbi_header * const h = (struct cbi_header *)cbi;

	memset(cbi, 0, sizeof(cbi));
	memcpy(h->magic, cbi_magic, sizeof(cbi_magic));
	h->total_size = sizeof(*h);
	h->major_version = CBI_VERSION_MAJOR;
	h->minor_version = CBI_VERSION_MINOR;
	h->crc = cbi_crc8(h);
	cached_read_result = EC_SUCCESS;

	return EC_SUCCESS;
}

void cbi_invalidate_cache(void)
{
	cached_read_result = EC_ERROR_CBI_CACHE_INVALID;
}

static int read_eeprom(uint8_t offset, uint8_t *in, int in_size)
{
	return i2c_read_block(I2C_PORT_EEPROM, I2C_ADDR_EEPROM_FLAGS,
			      offset, in, in_size);
}

/*
 * Get board information from EEPROM
 */
static int do_read_board_info(void)
{
	CPRINTS("Reading board info");

	/* Read header */
	if (read_eeprom(0, cbi, sizeof(*head))) {
		CPRINTS("Failed to read header");
		return EC_ERROR_INVAL;
	}

	/* Check magic */
	if (memcmp(head->magic, cbi_magic, sizeof(head->magic))) {
		CPRINTS("Bad magic");
		return EC_ERROR_INVAL;
	}

	/* check version */
	if (head->major_version > CBI_VERSION_MAJOR) {
		CPRINTS("Version mismatch");
		return EC_ERROR_INVAL;
	}

	/* Check the data size. It's expected to support up to 64k but our
	 * buffer has practical limitation. */
	if (head->total_size < sizeof(*head) ||
			sizeof(cbi) < head->total_size) {
		CPRINTS("Bad size: %d", head->total_size);
		return EC_ERROR_OVERFLOW;
	}

	/* Read the data */
	if (read_eeprom(sizeof(*head), head->data,
			head->total_size - sizeof(*head))) {
		CPRINTS("Failed to read body");
		return EC_ERROR_INVAL;
	}

	/* Check CRC. This supports new fields unknown to this parser. */
	if (cbi_crc8(head) != head->crc) {
		CPRINTS("Bad CRC");
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

static int read_board_info(void)
{
	if (cached_read_result == EC_ERROR_CBI_CACHE_INVALID) {
		cached_read_result = do_read_board_info();
		if (cached_read_result)
			/* On error (I2C or bad contents), retry a read */
			cached_read_result = do_read_board_info();
	}
	/* Else, we already tried and know the result. Return the cached
	 * error code immediately to avoid wasteful reads. */
	return cached_read_result;
}

__attribute__((weak))
int cbi_board_override(enum cbi_data_tag tag, uint8_t *buf, uint8_t *size)
{
	return EC_SUCCESS;
}

int cbi_get_board_info(enum cbi_data_tag tag, uint8_t *buf, uint8_t *size)
{
	const struct cbi_data *d;

	if (read_board_info())
		return EC_ERROR_UNKNOWN;

	d = cbi_find_tag(cbi, tag);
	if (!d)
		/* Not found */
		return EC_ERROR_UNKNOWN;
	if (*size < d->size)
		/* Insufficient buffer size */
		return EC_ERROR_INVAL;

	/* Clear the buffer in case len < *size */
	memset(buf, 0, *size);
	/* Copy the value */
	memcpy(buf, d->value, d->size);
	*size = d->size;

	return cbi_board_override(tag, buf, size);
}

static void cbi_remove_tag(void *const cbi, struct cbi_data *const d)
{
	struct cbi_header *const h = cbi;
	const size_t size = sizeof(*d) + d->size;
	const uint8_t *next = (uint8_t *)d + size;
	const size_t bytes_after = ((uint8_t *)cbi + h->total_size) - next;

	memmove(d, next, bytes_after);
	h->total_size -= size;
}

int cbi_set_board_info(enum cbi_data_tag tag, const uint8_t *buf, uint8_t size)
{
	struct cbi_data *d;

	d = cbi_find_tag(cbi, tag);

	/* If we found the entry, but the size doesn't match, delete it */
	if (d && d->size != size) {
		cbi_remove_tag(cbi, d);
		d = NULL;
	}

	if (!d) {
		uint8_t *p;
		/* Not found. Check if new item would fit */
		if (sizeof(cbi) < head->total_size + sizeof(*d) + size)
			return EC_ERROR_OVERFLOW;
		/* Append new item */
		p = cbi_set_data(&cbi[head->total_size], tag, buf, size);
		head->total_size = p - cbi;
	} else {
		/* Overwrite existing item */
		memcpy(d->value, buf, d->size);
	}

	return EC_SUCCESS;
}

static int eeprom_is_write_protected(void)
{
#ifdef CONFIG_WP_ACTIVE_HIGH
	return gpio_get_level(GPIO_WP);
#else
	return !gpio_get_level(GPIO_WP_L);
#endif /* CONFIG_WP_ACTIVE_HIGH */
}

static int write_board_info(void)
{
	const uint8_t *p = cbi;
	int rest = head->total_size;

	if (eeprom_is_write_protected()) {
		CPRINTS("Failed to write for WP");
		return EC_ERROR_ACCESS_DENIED;
	}

	while (rest > 0) {
		int size = MIN(EEPROM_PAGE_WRITE_SIZE, rest);
		int rv;
		rv = i2c_write_block(I2C_PORT_EEPROM, I2C_ADDR_EEPROM_FLAGS,
				     p - cbi, p, size);
		if (rv) {
			CPRINTS("Failed to write for %d", rv);
			return rv;
		}
		/* Wait for internal write cycle completion */
		msleep(EEPROM_PAGE_WRITE_MS);
		p += size;
		rest -= size;
	}

	return EC_SUCCESS;
}

int cbi_write(void)
{
	return write_board_info();
}

int cbi_get_board_version(uint32_t *ver)
{
	uint8_t size = sizeof(*ver);

	return cbi_get_board_info(CBI_TAG_BOARD_VERSION, (uint8_t *)ver, &size);
}

int cbi_get_sku_id(uint32_t *id)
{
	uint8_t size = sizeof(*id);

	return cbi_get_board_info(CBI_TAG_SKU_ID, (uint8_t *)id, &size);
}

int cbi_get_oem_id(uint32_t *id)
{
	uint8_t size = sizeof(*id);

	return cbi_get_board_info(CBI_TAG_OEM_ID, (uint8_t *)id, &size);
}

int cbi_get_model_id(uint32_t *id)
{
	uint8_t size = sizeof(*id);

	return cbi_get_board_info(CBI_TAG_MODEL_ID, (uint8_t *)id, &size);
}

int cbi_get_fw_config(uint32_t *fw_config)
{
	uint8_t size = sizeof(*fw_config);

	return cbi_get_board_info(CBI_TAG_FW_CONFIG, (uint8_t *)fw_config,
				  &size);
}

int cbi_get_ssfc(uint32_t *ssfc)
{
	uint8_t size = sizeof(*ssfc);

	return cbi_get_board_info(CBI_TAG_SSFC, (uint8_t *)ssfc,
				  &size);
}

int cbi_get_pcb_supplier(uint32_t *pcb_supplier)
{
	uint8_t size = sizeof(*pcb_supplier);

	return cbi_get_board_info(CBI_TAG_PCB_SUPPLIER, (uint8_t *)pcb_supplier,
			&size);
}

static enum ec_status hc_cbi_get(struct host_cmd_handler_args *args)
{
	const struct __ec_align4 ec_params_get_cbi *p = args->params;
	uint8_t size = MIN(args->response_max, UINT8_MAX);

	if (p->flag & CBI_GET_RELOAD)
		cached_read_result = EC_ERROR_CBI_CACHE_INVALID;

	if (cbi_get_board_info(p->tag, args->response, &size))
		return EC_RES_INVALID_PARAM;

	args->response_size = size;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_CROS_BOARD_INFO,
		     hc_cbi_get,
		     EC_VER_MASK(0));

static enum ec_status hc_cbi_set(struct host_cmd_handler_args *args)
{
	const struct __ec_align4 ec_params_set_cbi *p = args->params;

	/*
	 * If we ultimately cannot write to the flash, then fail early unless
	 * we are explicitly trying to write to the in-memory CBI only
	 */
	if (eeprom_is_write_protected() && !(p->flag & CBI_SET_NO_SYNC)) {
		CPRINTS("Failed to write for WP");
		return EC_RES_ACCESS_DENIED;
	}

#ifndef CONFIG_SYSTEM_UNLOCKED
	/* These fields are not allowed to be reprogrammed regardless the
	 * hardware WP state. They're considered as a part of the hardware. */
	if (p->tag == CBI_TAG_BOARD_VERSION || p->tag == CBI_TAG_OEM_ID)
		return EC_RES_ACCESS_DENIED;
#endif

	if (p->flag & CBI_SET_INIT) {
		memset(cbi, 0, sizeof(cbi));
		memcpy(head->magic, cbi_magic, sizeof(cbi_magic));
		head->total_size = sizeof(*head);
		cached_read_result = EC_SUCCESS;
	} else {
		if (read_board_info())
			return EC_RES_ERROR;
	}

	/* Given data size exceeds the packet size. */
	if (args->params_size < sizeof(*p) + p->size)
		return EC_RES_INVALID_PARAM;

	if (cbi_set_board_info(p->tag, p->data, p->size))
		return EC_RES_INVALID_PARAM;

	/* Whether we're modifying existing data or creating new one,
	 * we take over the format. */
	head->major_version = CBI_VERSION_MAJOR;
	head->minor_version = CBI_VERSION_MINOR;
	head->crc = cbi_crc8(head);

	/* Skip write if client asks so. */
	if (p->flag & CBI_SET_NO_SYNC)
		return EC_RES_SUCCESS;

	/* We already checked write protect failure case. */
	if (write_board_info())
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SET_CROS_BOARD_INFO,
		     hc_cbi_set,
		     EC_VER_MASK(0));

#ifdef CONFIG_CMD_CBI
static void dump_flash(void)
{
	uint8_t buf[16];
	int i;
	for (i = 0; i < CBI_EEPROM_SIZE; i += sizeof(buf)) {
		if (read_eeprom(i, buf, sizeof(buf))) {
			ccprintf("\nFailed to read EEPROM\n");
			return;
		}
		hexdump(buf, sizeof(buf));
	}
}

static void print_tag(const char * const tag, int rv, const uint32_t *val)
{
	ccprintf(tag);
	if (rv == EC_SUCCESS && val)
		ccprintf(": %u (0x%x)\n", *val, *val);
	else
		ccprintf(": (Error %d)\n", rv);
}

static void dump_cbi(void)
{
	uint32_t val;

	/* Ensure we read the latest data from flash. */
	cached_read_result = EC_ERROR_CBI_CACHE_INVALID;
	read_board_info();

	if (cached_read_result != EC_SUCCESS) {
		ccprintf("Cannot Read CBI (Error %d)\n", cached_read_result);
		return;
	}

	ccprintf("CBI_VERSION: 0x%04x\n", head->version);
	ccprintf("TOTAL_SIZE: %u\n", head->total_size);

	print_tag("BOARD_VERSION", cbi_get_board_version(&val), &val);
	print_tag("OEM_ID", cbi_get_oem_id(&val), &val);
	print_tag("MODEL_ID", cbi_get_model_id(&val), &val);
	print_tag("SKU_ID", cbi_get_sku_id(&val), &val);
	print_tag("FW_CONFIG", cbi_get_fw_config(&val), &val);
	print_tag("PCB_SUPPLIER", cbi_get_pcb_supplier(&val), &val);
	print_tag("SSFC", cbi_get_ssfc(&val), &val);
}

static int cc_cbi(int argc, char **argv)
{
	dump_cbi();
	dump_flash();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cbi, cc_cbi, NULL, "Print Cros Board Info from flash");
#endif /* CONFIG_CMD_CBI */

#endif /* !HOST_TOOLS_BUILD */

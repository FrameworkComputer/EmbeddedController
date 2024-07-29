/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cros Board Info
 */

#include "chipset.h"
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
	return cros_crc8((uint8_t *)&h->crc + 1,
			 h->total_size - sizeof(h->magic) - sizeof(h->crc));
}

uint8_t *cbi_set_data(uint8_t *p, enum cbi_data_tag tag, const void *buf,
		      int size)
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
	const struct cbi_header *h = (struct cbi_header *)buf;
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

#define CPRINTS(format, args...) cprints(CC_SYSTEM, "CBI " format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, "CBI " format, ##args)

static int cache_status = CBI_CACHE_STATUS_INVALID;
static uint8_t cbi[CBI_IMAGE_SIZE];
static struct cbi_header *const head = (struct cbi_header *)cbi;

int cbi_create(void)
{
	memset(cbi, 0, sizeof(cbi));
	memcpy(head->magic, cbi_magic, sizeof(cbi_magic));
	head->total_size = sizeof(*head);
	head->major_version = CBI_VERSION_MAJOR;
	head->minor_version = CBI_VERSION_MINOR;
	head->crc = cbi_crc8(head);
	cache_status = CBI_CACHE_STATUS_SYNCED;

	return EC_SUCCESS;
}

void cbi_invalidate_cache(void)
{
	cache_status = CBI_CACHE_STATUS_INVALID;
}

int cbi_get_cache_status(void)
{
	return cache_status;
}

static int do_cbi_read(void)
{
	CPRINTS("Reading board info");

	/* Read header */
	if (cbi_config->drv->load(0, cbi, sizeof(*head))) {
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

	/*
	 * Check the data size. It's expected to support up to 64k but our
	 * buffer has practical limitation.
	 */
	if (head->total_size < sizeof(*head) ||
	    head->total_size > CBI_IMAGE_SIZE) {
		CPRINTS("Bad size: %d", head->total_size);
		return EC_ERROR_OVERFLOW;
	}

	/* Read the data */
	if (cbi_config->drv->load(sizeof(*head), head->data,
				  head->total_size - sizeof(*head))) {
		CPRINTS("Failed to read body");
		return EC_ERROR_INVAL;
	}

	/* Check CRC. This supports new fields unknown to this parser. */
	if (cbi_config->storage_type != CBI_STORAGE_TYPE_GPIO &&
	    cbi_crc8(head) != head->crc) {
		CPRINTS("Bad CRC");
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

static int cbi_read(void)
{
	int i;
	int rv;

	if (cbi_get_cache_status() == CBI_CACHE_STATUS_SYNCED)
		return EC_SUCCESS;

	for (i = 0; i < 2; i++) {
		rv = do_cbi_read();
		if (rv == EC_SUCCESS) {
			cache_status = CBI_CACHE_STATUS_SYNCED;
			return EC_SUCCESS;
		}
		/* On error (I2C or bad contents), retry a read */
	}

	return rv;
}

__attribute__((weak)) int cbi_board_override(enum cbi_data_tag tag,
					     uint8_t *buf, uint8_t *size)
{
	return EC_SUCCESS;
}

int cbi_get_board_info(enum cbi_data_tag tag, uint8_t *buf, uint8_t *size)
{
	const struct cbi_data *d;

	if (cbi_read())
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

test_mockable int cbi_set_board_info(enum cbi_data_tag tag, const uint8_t *buf,
				     uint8_t size)
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

int cbi_write(void)
{
	if (cbi_config->drv->is_protected()) {
		CPRINTS("Failed to write due to WP");
		return EC_ERROR_ACCESS_DENIED;
	}

	return cbi_config->drv->store(cbi);
}

test_mockable int cbi_get_board_version(uint32_t *ver)
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

test_mockable int cbi_get_fw_config(uint32_t *fw_config)
{
	uint8_t size = sizeof(*fw_config);

	return cbi_get_board_info(CBI_TAG_FW_CONFIG, (uint8_t *)fw_config,
				  &size);
}

test_mockable int cbi_get_ssfc(uint32_t *ssfc)
{
	uint8_t size = sizeof(*ssfc);

	return cbi_get_board_info(CBI_TAG_SSFC, (uint8_t *)ssfc, &size);
}

int cbi_get_pcb_supplier(uint32_t *pcb_supplier)
{
	uint8_t size = sizeof(*pcb_supplier);

	return cbi_get_board_info(CBI_TAG_PCB_SUPPLIER, (uint8_t *)pcb_supplier,
				  &size);
}

int cbi_get_rework_id(uint64_t *id)
{
	uint8_t size = sizeof(*id);
	return cbi_get_board_info(CBI_TAG_REWORK_ID, (uint8_t *)id, &size);
}

int cbi_get_factory_calibration_data(uint32_t *calibration_data)
{
	uint8_t size = sizeof(*calibration_data);

	return cbi_get_board_info(CBI_TAG_FACTORY_CALIBRATION_DATA,
				  (uint8_t *)calibration_data, &size);
}

test_mockable int cbi_get_common_control(union ec_common_control *ctrl)
{
	uint8_t size = sizeof(*ctrl);

	return cbi_get_board_info(CBI_TAG_COMMON_CONTROL, (uint8_t *)ctrl,
				  &size);
}

static enum ec_status hc_cbi_get(struct host_cmd_handler_args *args)
{
	const struct __ec_align4 ec_params_get_cbi *p = args->params;
	uint8_t size = MIN(args->response_max, UINT8_MAX);

	if (p->flag & CBI_GET_RELOAD)
		cbi_invalidate_cache();

	if (cbi_get_board_info(p->tag, args->response, &size))
		return EC_RES_INVALID_PARAM;

	args->response_size = size;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_CROS_BOARD_INFO, hc_cbi_get, EC_VER_MASK(0));

static enum ec_status
common_cbi_set(const struct __ec_align4 ec_params_set_cbi *p)
{
	/*
	 * If we ultimately cannot write to the flash, then fail early unless
	 * we are explicitly trying to write to the in-memory CBI only
	 */
	if (cbi_config->drv->is_protected() && !(p->flag & CBI_SET_NO_SYNC)) {
		CPRINTS("Failed to write due to WP");
		return EC_RES_ACCESS_DENIED;
	}

#ifndef CONFIG_SYSTEM_UNLOCKED
	/*
	 * These fields are not allowed to be reprogrammed regardless the
	 * hardware WP state. They're considered as a part of the hardware.
	 */
	if (p->tag == CBI_TAG_BOARD_VERSION || p->tag == CBI_TAG_OEM_ID) {
		CPRINTS("Failed to write tag: %d. System locked", p->tag);
		return EC_RES_ACCESS_DENIED;
	}
#endif

	if (p->flag & CBI_SET_INIT) {
		memset(cbi, 0, sizeof(cbi));
		memcpy(head->magic, cbi_magic, sizeof(cbi_magic));
		head->total_size = sizeof(*head);
	} else {
		if (cbi_read())
			return EC_RES_ERROR;
	}

	if (cbi_set_board_info(p->tag, p->data, p->size))
		return EC_RES_INVALID_PARAM;

	/*
	 * Whether we're modifying existing data or creating new one,
	 * we take over the format.
	 */
	head->major_version = CBI_VERSION_MAJOR;
	head->minor_version = CBI_VERSION_MINOR;
	head->crc = cbi_crc8(head);
	cache_status = CBI_CACHE_STATUS_SYNCED;

	/* Skip write if client asks so. */
	if (p->flag & CBI_SET_NO_SYNC)
		return EC_RES_SUCCESS;

	/* We already checked write protect failure case. */
	if (cbi_write())
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}

static enum ec_status hc_cbi_set(struct host_cmd_handler_args *args)
{
	const struct __ec_align4 ec_params_set_cbi *p = args->params;

	/* Given data size exceeds the packet size. */
	if (args->params_size < sizeof(*p) + p->size)
		return EC_RES_INVALID_PARAM;

	return common_cbi_set(p);
}
DECLARE_HOST_COMMAND(EC_CMD_SET_CROS_BOARD_INFO, hc_cbi_set, EC_VER_MASK(0));

static enum ec_status hc_cbi_bin_read(struct host_cmd_handler_args *args)
{
	const struct __ec_align4 ec_params_get_cbi_bin *p = args->params;
	uint8_t size = MIN(args->response_max, UINT8_MAX);

	if (size < p->size) {
		/* Insufficient buffer size */
		return EC_RES_INVALID_PARAM;
	}
	if (p->offset >= CBI_FLASH_SIZE) {
		/* Incorrect offset */
		return EC_RES_INVALID_PARAM;
	}
	if ((p->offset + p->size) > CBI_FLASH_SIZE) {
		/* Incorrect area */
		return EC_RES_INVALID_PARAM;
	}
	if (p->offset < CBI_IMAGE_SIZE) {
		uint32_t read_size = p->size;

		if ((p->offset + p->size) > CBI_IMAGE_SIZE) {
			read_size = CBI_IMAGE_SIZE - p->offset;
			memset((uint8_t *)args->response + read_size, 0xFF,
			       p->size - read_size);
		}

		if (cbi_config->drv->load(p->offset, args->response,
					  read_size)) {
			CPRINTS("Failed to read CBI");
			return EC_RES_ERROR;
		}
	} else {
		memset((uint8_t *)args->response, 0xFF, p->size);
	}
	args->response_size = p->size;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CBI_BIN_READ, hc_cbi_bin_read, EC_VER_MASK(0));

static bool is_valid_cbi(const uint8_t *cbi)
{
	const struct cbi_header *head = (const struct cbi_header *)cbi;

	/* Check magic */
	if (memcmp(head->magic, cbi_magic, sizeof(head->magic))) {
		CPRINTS("Bad CBI magic");
		return false;
	}

	/* check version */
	if (head->major_version > CBI_VERSION_MAJOR) {
		CPRINTS("Bad CBI version");
		return false;
	}

	/*
	 * Check the data size. It's expected to support up to 64k but our
	 * buffer has practical limitation.
	 */
	if (head->total_size < sizeof(*head) ||
	    head->total_size > CBI_IMAGE_SIZE) {
		CPRINTS("Bad CBI size");
		return false;
	}

	/* Check CRC */
	if (cbi_crc8(head) != head->crc) {
		CPRINTS("Bad CRC");
		return false;
	}

	return true;
}

static enum ec_status hc_cbi_bin_write(struct host_cmd_handler_args *args)
{
	/*
	 * If we ultimately cannot write to the flash, then fail early
	 */
	if (cbi_config->drv->is_protected()) {
		CPRINTS("Failed to write due to WP");
		return EC_RES_ACCESS_DENIED;
	}
	const struct __ec_align4 ec_params_set_cbi_bin *p = args->params;

	/* Given data size exceeds the packet size. */
	if (args->params_size < sizeof(*p) + p->size)
		return EC_RES_INVALID_PARAM;

	if (p->offset >= CBI_FLASH_SIZE)
		/* Incorrect offset */
		return EC_RES_INVALID_PARAM;

	/* Incorrect area */
	if ((p->offset + p->size) > CBI_FLASH_SIZE)
		return EC_RES_INVALID_PARAM;

	if (p->flags & EC_CBI_BIN_BUFFER_CLEAR)
		memset(cbi, 0xFF, CBI_IMAGE_SIZE);

	if (p->offset < CBI_IMAGE_SIZE) {
		uint32_t write_size = p->size;

		if ((p->offset + p->size) > CBI_IMAGE_SIZE)
			write_size = CBI_IMAGE_SIZE - p->offset;

		memcpy(cbi + p->offset, p->data, write_size);
	} else {
		CPRINTS("CBI buffer overflow");
		return EC_RES_ERROR;
	}
	if (p->flags & EC_CBI_BIN_BUFFER_WRITE) {
		if (is_valid_cbi(cbi)) {
			if (cbi_config->drv->store(cbi)) {
				CPRINTS("Failed to write CBI");
				return EC_RES_ERROR;
			}
			cbi_invalidate_cache();
			cbi_read();
			if (cbi_get_cache_status() != CBI_CACHE_STATUS_SYNCED) {
				CPRINTF("Cannot Read CBI (Error %d)\n",
					cbi_get_cache_status());
				return EC_RES_ERROR;
			}
		} else {
			CPRINTS("Invalid CBI in buffer");
			return EC_RES_ERROR;
		}
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CBI_BIN_WRITE, hc_cbi_bin_write, EC_VER_MASK(0));

#ifdef CONFIG_CMD_CBI
static void print_tag(const char *const tag, int rv, const uint32_t *val)
{
	ccprintf("%s", tag);
	if (rv == EC_SUCCESS && val)
		ccprintf(": %u (0x%x)\n", *val, *val);
	else
		ccprintf(": (Error %d)\n", rv);
}

static void print_uint64_tag(const char *const tag, int rv,
			     const uint64_t *lval)
{
	ccprintf("%s", tag);
	if (rv == EC_SUCCESS && lval)
		ccprintf(": %llu (0x%llx)\n", *(unsigned long long *)lval,
			 *(unsigned long long *)lval);
	else
		ccprintf(": (Error %d)\n", rv);
}

static void dump_cbi(void)
{
	uint32_t val;
	uint64_t lval;
	union ec_common_control ctrl;

	/* Ensure we read the latest data from flash. */
	cbi_invalidate_cache();
	cbi_read();

	if (cbi_get_cache_status() != CBI_CACHE_STATUS_SYNCED) {
		ccprintf("Cannot Read CBI (Error %d)\n",
			 cbi_get_cache_status());
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
	print_uint64_tag("REWORK_ID", cbi_get_rework_id(&lval), &lval);
	print_tag("COMMON_CONTROL", cbi_get_common_control(&ctrl),
		  &ctrl.raw_value);
}

/*
 * Space for the set command (does not include data space) plus maximum
 * possible console input
 */
static uint8_t
	buf[sizeof(struct ec_params_set_cbi) + CONFIG_CONSOLE_INPUT_LINE_SIZE];

static int cc_cbi(int argc, const char **argv)
{
	struct __ec_align4 ec_params_set_cbi *setter =
		(struct __ec_align4 ec_params_set_cbi *)buf;
	int last_arg;
	char *e;

	if (argc == 1) {
		dump_cbi();
		if (cbi_get_cache_status() == CBI_CACHE_STATUS_SYNCED)
			hexdump(cbi, CBI_IMAGE_SIZE);
		return EC_SUCCESS;
	}

	if (strcasecmp(argv[1], "set") == 0) {
		if (argc < 5) {
			ccprintf("Set requires: <tag> <value> <size>\n");
			return EC_ERROR_PARAM_COUNT;
		}

		setter->tag = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (setter->tag == CBI_TAG_DRAM_PART_NUM ||
		    setter->tag == CBI_TAG_OEM_NAME) {
			setter->size = strlen(argv[3]) + 1;
			memcpy(setter->data, argv[3], setter->size);
		} else {
			uint64_t val = strtoull(argv[3], &e, 0);

			if (*e)
				return EC_ERROR_PARAM3;

			setter->size = strtoi(argv[4], &e, 0);
			if (*e)
				return EC_ERROR_PARAM4;

			if (setter->size < 1) {
				ccprintf("Set size too small\n");
				return EC_ERROR_PARAM4;
			} else if ((setter->size > 8) ||
				   (setter->size > 4 &&
				    setter->tag != CBI_TAG_REWORK_ID)) {
				ccprintf("Set size too large\n");
				return EC_ERROR_PARAM4;
			}

			memcpy(setter->data, &val, setter->size);
		}

		last_arg = 5;
	} else if (strcasecmp(argv[1], "remove") == 0) {
		if (argc < 3) {
			ccprintf("Remove requires: <tag>\n");
			return EC_ERROR_PARAM_COUNT;
		}

		setter->tag = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		setter->size = 0;
		last_arg = 3;
	} else {
		return EC_ERROR_PARAM1;
	}

	setter->flag = 0;

	if (argc > last_arg) {
		int i;

		for (i = last_arg; i < argc; i++) {
			if (strcasecmp(argv[i], "init") == 0) {
				setter->flag |= CBI_SET_INIT;
			} else if (strcasecmp(argv[i], "skip_write") == 0) {
				setter->flag |= CBI_SET_NO_SYNC;
			} else {
				ccprintf("Invalid additional option\n");
				return EC_ERROR_PARAM1 + i - 1;
			}
		}
	}

	switch (common_cbi_set(setter)) {
	case EC_RES_SUCCESS:
		return EC_SUCCESS;
	case EC_RES_INVALID_PARAM:
		return EC_ERROR_INVAL;
	case EC_RES_ACCESS_DENIED:
		return EC_ERROR_ACCESS_DENIED;
	default:
		return EC_ERROR_UNKNOWN;
	}
}
DECLARE_CONSOLE_COMMAND(cbi, cc_cbi,
			"[set <tag> <value> <size> | "
			"remove <tag>] [init | skip_write]",
			"Print or change Cros Board Info from flash");
#endif /* CONFIG_CMD_CBI */

#ifndef CONFIG_AP_POWER_CONTROL
int cbi_set_fw_config(uint32_t fw_config)
{
	/* Check write protect status */
	if (cbi_config->drv->is_protected())
		return EC_ERROR_ACCESS_DENIED;

	/* Ensure that CBI has been configured */
	if (cbi_read())
		cbi_create();

	/* Update the FW_CONFIG field */
	cbi_set_board_info(CBI_TAG_FW_CONFIG, (uint8_t *)&fw_config,
			   sizeof(int));

	/* Update CRC calculation and write to the storage */
	head->crc = cbi_crc8(head);
	if (cbi_write())
		return EC_ERROR_UNKNOWN;

	dump_cbi();

	return EC_SUCCESS;
}

int cbi_set_ssfc(uint32_t ssfc)
{
	/* Check write protect status */
	if (cbi_config->drv->is_protected())
		return EC_ERROR_ACCESS_DENIED;

	/* Ensure that CBI has been configured */
	if (cbi_read())
		cbi_create();

	/* Update the SSFC field */
	cbi_set_board_info(CBI_TAG_SSFC, (uint8_t *)&ssfc, sizeof(int));

	/* Update CRC calculation and write to the storage */
	head->crc = cbi_crc8(head);
	if (cbi_write())
		return EC_ERROR_UNKNOWN;

	dump_cbi();

	return EC_SUCCESS;
}
#endif

#endif /* !HOST_TOOLS_BUILD */

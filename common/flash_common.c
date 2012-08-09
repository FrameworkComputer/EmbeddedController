/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC - common functions */

#include "config.h"
#include "console.h"
#include "flash.h"
#include "host_command.h"
#include "registers.h"
#include "shared_mem.h"
#include "system.h"
#include "util.h"

int flash_dataptr(int offset, int size_req, int align, char **ptrp)
{
	if (offset < 0 || size_req < 0 ||
			offset + size_req > CONFIG_FLASH_SIZE ||
			(offset | size_req) & (align - 1))
		return -1;  /* Invalid range */
	if (ptrp)
		*ptrp = flash_physical_dataptr(offset);

	return CONFIG_FLASH_SIZE - offset;
}

int flash_write(int offset, int size, const char *data)
{
	if (flash_dataptr(offset, size, CONFIG_FLASH_WRITE_SIZE, NULL) < 0)
		return EC_ERROR_INVAL;  /* Invalid range */

	return flash_physical_write(offset, size, data);
}

int flash_erase(int offset, int size)
{
	if (flash_dataptr(offset, size, CONFIG_FLASH_ERASE_SIZE, NULL) < 0)
		return EC_ERROR_INVAL;  /* Invalid range */

	return flash_physical_erase(offset, size);
}

/*****************************************************************************/
/* Console commands */

/*
 * Parse offset and size from command line argv[shift] and argv[shift+1]
 *
 * Default values: If argc<=shift, leaves offset unchanged, returning error if
 * *offset<0.  If argc<shift+1, leaves size unchanged, returning error if
 * *size<0.
 */
static int parse_offset_size(int argc, char **argv, int shift,
			     int *offset, int *size)
{
	char *e;
	int i;

	if (argc > shift) {
		i = (uint32_t)strtoi(argv[shift], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		*offset = i;
	} else if (*offset < 0)
		return EC_ERROR_PARAM_COUNT;

	if (argc > shift + 1) {
		i = (uint32_t)strtoi(argv[shift + 1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		*size = i;
	} else if (*size < 0)
		return EC_ERROR_PARAM_COUNT;

	return EC_SUCCESS;
}

static int command_flash_info(int argc, char **argv)
{
	int i;

	ccprintf("Physical:%4d KB\n", CONFIG_FLASH_PHYSICAL_SIZE / 1024);
	ccprintf("Usable:  %4d KB\n", CONFIG_FLASH_SIZE / 1024);
	ccprintf("Write:   %4d B\n", CONFIG_FLASH_WRITE_SIZE);
	ccprintf("Erase:   %4d B\n", CONFIG_FLASH_ERASE_SIZE);
	ccprintf("Protect: %4d B\n", CONFIG_FLASH_BANK_SIZE);

	i = flash_get_protect();
	ccprintf("Flags:  ");
	if (i & EC_FLASH_PROTECT_GPIO_ASSERTED)
		ccputs(" wp_gpio_asserted");
	if (i & EC_FLASH_PROTECT_RO_AT_BOOT)
		ccputs(" ro_at_boot");
	if (i & EC_FLASH_PROTECT_RW_AT_BOOT)
		ccputs(" rw_at_boot");
	if (i & EC_FLASH_PROTECT_RO_NOW)
		ccputs(" ro_now");
	if (i & EC_FLASH_PROTECT_RW_NOW)
		ccputs(" rw_now");
	if (i & EC_FLASH_PROTECT_ERROR_STUCK)
		ccputs(" STUCK");
	if (i & EC_FLASH_PROTECT_ERROR_INCONSISTENT)
		ccputs(" INCONSISTENT");
	ccputs("\n");

	ccputs("Protected now:");
	for (i = 0; i < CONFIG_FLASH_PHYSICAL_SIZE / CONFIG_FLASH_BANK_SIZE;
	     i++) {
		if (!(i & 31))
			ccputs("\n    ");
		else if (!(i & 7))
			ccputs(" ");
		ccputs(flash_physical_get_protect(i) ? "Y" : ".");
	}
	ccputs("\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flashinfo, command_flash_info,
			NULL,
			"Print flash info",
			NULL);

static int command_flash_erase(int argc, char **argv)
{
	int offset = -1;
	int size = CONFIG_FLASH_ERASE_SIZE;
	int rv;

	rv = parse_offset_size(argc, argv, 1, &offset, &size);
	if (rv)
		return rv;

	ccprintf("Erasing %d bytes at 0x%x...\n", size, offset, offset);
	return flash_erase(offset, size);
}
DECLARE_CONSOLE_COMMAND(flasherase, command_flash_erase,
			"offset [size]",
			"Erase flash",
			NULL);

static int command_flash_write(int argc, char **argv)
{
	int offset = -1;
	int size = CONFIG_FLASH_ERASE_SIZE;
	int rv;
	char *data;
	int i;


	rv = parse_offset_size(argc, argv, 1, &offset, &size);
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

	ccprintf("Writing %d bytes to 0x%x...\n",
		 size, offset, offset);
	rv = flash_write(offset, size, data);

	/* Free the buffer */
	shared_mem_release(data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(flashwrite, command_flash_write,
			"offset [size]",
			"Write pattern to flash",
			NULL);

static int command_flash_wp(int argc, char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "enable"))
		return flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT, -1);
	else if (!strcasecmp(argv[1], "disable"))
		return flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT, 0);
	else if (!strcasecmp(argv[1], "now"))
		return flash_set_protect(EC_FLASH_PROTECT_RW_NOW |
					 EC_FLASH_PROTECT_RO_NOW, -1);
	else if (!strcasecmp(argv[1], "rw"))
		return flash_set_protect(EC_FLASH_PROTECT_RW_AT_BOOT, -1);
	else if (!strcasecmp(argv[1], "norw"))
		return flash_set_protect(EC_FLASH_PROTECT_RW_AT_BOOT, 0);
	else
		return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND(flashwp, command_flash_wp,
			"<enable | disable | now | rw | norw>",
			"Modify flash write protect",
			NULL);

/*****************************************************************************/
/* Host commands */

static int flash_command_get_info(struct host_cmd_handler_args *args)
{
	struct ec_response_flash_info *r = args->response;

	r->flash_size = CONFIG_FLASH_SIZE;
	r->write_block_size = CONFIG_FLASH_WRITE_SIZE;
	r->erase_block_size = CONFIG_FLASH_ERASE_SIZE;
	r->protect_block_size = CONFIG_FLASH_BANK_SIZE;
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_INFO,
		     flash_command_get_info,
		     EC_VER_MASK(0));

static int flash_command_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_read *p = args->params;

	if (flash_dataptr(p->offset, p->size, 1, (char **)&args->response) < 0)
		return EC_RES_ERROR;

	args->response_size = p->size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_READ,
		     flash_command_read,
		     EC_VER_MASK(0));

static int flash_command_write(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_write *p = args->params;

	if (p->size > sizeof(p->data))
		return EC_RES_INVALID_PARAM;

	if (system_unsafe_to_overwrite(p->offset, p->size))
		return EC_RES_ACCESS_DENIED;

	if (flash_write(p->offset, p->size, p->data))
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_WRITE,
		     flash_command_write,
		     EC_VER_MASK(0));

static int flash_command_erase(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_erase *p = args->params;

	if (system_unsafe_to_overwrite(p->offset, p->size))
		return EC_RES_ACCESS_DENIED;

	if (flash_erase(p->offset, p->size))
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_ERASE,
		     flash_command_erase,
		     EC_VER_MASK(0));

static int flash_command_protect(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_protect *p = args->params;
	struct ec_response_flash_protect *r = args->response;

	/*
	 * Handle requesting new flags.  Note that we ignore the return code
	 * from flash_set_protect(), since errors will be visible to the caller
	 * via the flags in the response.  (If we returned error, the caller
	 * wouldn't get the response.)
	 */
	if (p->mask)
		flash_set_protect(p->mask, p->flags);

	/*
	 * Retrieve the current flags.  The caller can use this to determine
	 * which of the requested flags could be set.  This is cleaner than
	 * simply returning error, because it provides information to the
	 * caller about the actual result.
	 */
	r->flags = flash_get_protect();

	/* Indicate which flags are valid on this platform */
	r->valid_flags =
		EC_FLASH_PROTECT_GPIO_ASSERTED |
		EC_FLASH_PROTECT_ERROR_STUCK |
		EC_FLASH_PROTECT_RO_AT_BOOT |
		EC_FLASH_PROTECT_RO_NOW |
		EC_FLASH_PROTECT_RW_NOW |
		EC_FLASH_PROTECT_ERROR_INCONSISTENT;
	r->writable_flags = 0;

	/* If RO protection isn't enabled, its at-boot state can be changed. */
	if (!(r->flags & EC_FLASH_PROTECT_RO_NOW))
		r->writable_flags |= EC_FLASH_PROTECT_RO_AT_BOOT;

#ifdef CHIP_lm4
	/*
	 * If RW protection isn't enabled this boot, it can be enabled if the *
	 * WP GPIO is asserted.
	 */
	if (!(r->flags & EC_FLASH_PROTECT_RW_NOW) &&
	    (r->flags & EC_FLASH_PROTECT_GPIO_ASSERTED))
		r->writable_flags |= EC_FLASH_PROTECT_RW_NOW;

#elif defined(CHIP_stm32)
	/* RW protection can only be changed at boot */
	r->valid_flags |= EC_FLASH_PROTECT_RW_AT_BOOT;
	r->writable_flags |= EC_FLASH_PROTECT_RW_AT_BOOT;
#endif

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_PROTECT,
		     flash_command_protect,
		     EC_VER_MASK(1));

static int flash_command_region_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_region_info *p = args->params;
	struct ec_response_flash_region_info *r = args->response;

	switch (p->region) {
	case EC_FLASH_REGION_RO:
		r->offset = CONFIG_SECTION_RO_OFF;
		r->size = CONFIG_SECTION_RO_SIZE;
		break;
	case EC_FLASH_REGION_RW:
		r->offset = CONFIG_SECTION_RW_OFF;
		r->size = CONFIG_SECTION_RW_SIZE;
		break;
	case EC_FLASH_REGION_WP_RO:
		r->offset = CONFIG_SECTION_RO_OFF;
		r->size = CONFIG_SECTION_RO_SIZE;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_REGION_INFO,
		     flash_command_region_info,
		     EC_VER_MASK(EC_VER_FLASH_REGION_INFO));

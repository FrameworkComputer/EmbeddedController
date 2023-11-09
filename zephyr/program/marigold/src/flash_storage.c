/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <atomic.h>
#include <zephyr/init.h>
#include "gpio/gpio_int.h"
#include "console.h"
#include "task.h"
#include "util.h"
#include "zephyr_console_shim.h"
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <flash.h>

#include "flash_storage.h"

#define CPRINTS(format, args...) cprints(CC_GPIO, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_GPIO, format, ## args)

LOG_MODULE_REGISTER(flashstorage, LOG_LEVEL_ERR);

static struct ec_flash_flags_info current_flags;
bool flash_storage_dirty;

bool check_flags_valid_header(void)
{
	if (current_flags.magic != FLASH_FLAGS_MAGIC ||
		current_flags.length != (sizeof(current_flags) - 8) ||
		current_flags.version != FLASH_FLAGS_VERSION) {
		return false;
	} else {
		return true;
	}
}

void flash_storage_load_defaults(void)
{
		CPRINTS("Init flash storage to defaults");
		memset(&current_flags, 0x00, sizeof(current_flags));
		current_flags.magic = FLASH_FLAGS_MAGIC;
		current_flags.length = (sizeof(current_flags) - 8);
		current_flags.version = FLASH_FLAGS_VERSION;
		flash_storage_dirty = true;
}

int flash_storage_initialize(void)
{

	int rv;

	rv = crec_flash_physical_read(SPI_FLAGS_REGION,
									sizeof(current_flags),
									(void *)&current_flags);
	if (rv != EC_SUCCESS)
		CPRINTS("Could not load flash storage");

	/*Check structure is valid*/
	if (check_flags_valid_header() == false) {
		CPRINTS("loading flash default flags");
		flash_storage_load_defaults();
	}

	return rv;
}

int flash_storage_update(enum ec_flash_flags_idx idx, uint8_t v)
{
	if (idx >= FLASH_FLAGS_MAX)
		return EC_ERROR_PARAM1;

	if (check_flags_valid_header() == false)
		flash_storage_initialize();

	if (current_flags.flags[idx] != v) {
		current_flags.flags[idx] = v;
		flash_storage_dirty = true;
	}
	return EC_SUCCESS;
}

int flash_storage_commit(void)
{
	int rv = EC_SUCCESS;

	if (check_flags_valid_header() == false)
		flash_storage_initialize();

	if (flash_storage_dirty) {


		rv = crec_flash_physical_erase(SPI_FLAGS_REGION, SPI_FLAGS_SIZE);

		if (rv != EC_SUCCESS) {
			CPRINTS("SPI fail to erase");
			return rv;
		}

		current_flags.update_number += 1;

		rv = crec_flash_physical_write(SPI_FLAGS_REGION,
								sizeof(current_flags),
								(void *)&current_flags);

		if (rv != EC_SUCCESS) {
			CPRINTS("SPI fail to write");
			return rv;
		}

		CPRINTS("%s, update:%d", __func__, current_flags.update_number);

		flash_storage_dirty = false;
	}

	return rv;
}

int flash_storage_get(enum ec_flash_flags_idx idx)
{
	if (idx >= FLASH_FLAGS_MAX)
		return -1;

	if (check_flags_valid_header() == false)
		flash_storage_initialize();

	return current_flags.flags[idx];
}

static int cmd_flash_flags(int argc, const char **argv)
{
	int data;
	int i, d;
	char *e;


	if (argc >= 3) {

		i = strtoi(argv[2], &e, 0);

		if (*e)
			return EC_ERROR_PARAM2;

		if (!strcasecmp(argv[1], "read")) {
			data = flash_storage_get(i);
			CPRINTS("Flash data:%d", data);
		} else if (argc >= 4 && !strcasecmp(argv[1], "write")) {

			d = strtoi(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
			flash_storage_update(i, d);
			flash_storage_commit();
		} else {
			return EC_ERROR_PARAM3;
		}
		return EC_SUCCESS;
	}

	return EC_ERROR_PARAM2;
}
DECLARE_CONSOLE_COMMAND(flashflag, cmd_flash_flags,
			"[read/write] i [d]",
			"read or write bytes from flags structure");

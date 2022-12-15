/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "util.h"
#include "hooks.h"

#include "board.h"
#include "gpio.h"

#include "spi.h"
#include "spi_chip.h"
#include "spi_flash.h"

#include "flash_storage.h"






#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

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

	spi_mux_control(1);

	rv = spi_flash_read((void *)&current_flags, SPI_FLAGS_REGION, sizeof(current_flags));
	if (rv != EC_SUCCESS)
		CPRINTS("Could not load flash storage");

	spi_mux_control(0);

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

		spi_mux_control(1);

		rv = spi_flash_erase(SPI_FLAGS_REGION, 0x1000);

		if (rv != EC_SUCCESS) {
			CPRINTS("SPI fail to erase");
			goto fail;
		}

		current_flags.update_number += 1;

		rv = spi_flash_write(SPI_FLAGS_REGION,
								sizeof(current_flags),
								(void *)&current_flags);

		if (rv != EC_SUCCESS) {
			CPRINTS("SPI fail to write");
			goto fail;
		}

		CPRINTS("%s, update:%d", __func__, current_flags.update_number);

		spi_mux_control(0);
		flash_storage_dirty = false;
	}

	return rv;

fail:
	spi_mux_control(0);
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

static int cmd_flash_flags(int argc, char **argv)
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

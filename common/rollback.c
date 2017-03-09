/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Rollback protection logic. */

#include "common.h"
#include "console.h"
#include "flash.h"
#include "rollback.h"
#include "system.h"
#include "util.h"

/* Number of rollback regions */
#define ROLLBACK_REGIONS 2

/*
 * Note: Do not change this structure without also updating
 * common/firmware_image.S .image.ROLLBACK section.
 */
struct rollback_data {
	int32_t rollback_min_version;
	uint32_t cookie;
};

/* We need at least 2 erasable blocks in the rollback region. */
BUILD_ASSERT(CONFIG_ROLLBACK_SIZE >= ROLLBACK_REGIONS*CONFIG_FLASH_ERASE_SIZE);
BUILD_ASSERT(sizeof(struct rollback_data) <= CONFIG_FLASH_ERASE_SIZE);

static uintptr_t get_rollback_offset(int region)
{
	return CONFIG_ROLLBACK_OFF + region * CONFIG_FLASH_ERASE_SIZE;
}

static int read_rollback(int region, struct rollback_data *data)
{
	uintptr_t offset;

	offset = get_rollback_offset(region);

	if (flash_read(offset, sizeof(*data), (char *)data))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/*
 * Get the most recent rollback information.
 *
 * @rollback_min_version: Minimum version to accept for rollback protection,
 *                        or 0 if no rollback information is present.
 *
 * Return most recent region index on success (>= 0, or 0 if no rollback
 * region is valid), negative value on error.
 */
static int get_latest_rollback(int32_t *rollback_min_version)
{
	int region;
	int min_region = 0;

	*rollback_min_version = 0;

	for (region = 0; region < ROLLBACK_REGIONS; region++) {
		struct rollback_data data;

		if (read_rollback(region, &data))
			return -1;

		/* Check if not initialized or invalid cookie. */
		if (data.cookie != CROS_EC_ROLLBACK_COOKIE)
			continue;

		if (data.rollback_min_version > *rollback_min_version) {
			min_region = region;
			*rollback_min_version = data.rollback_min_version;
		}
	}

	return min_region;
}

int32_t rollback_get_minimum_version(void)
{
	int32_t rollback_min_version;

	if (get_latest_rollback(&rollback_min_version) < 0)
		return -1;

	return rollback_min_version;
}

int rollback_update(int32_t next_min_version)
{
	struct rollback_data data;
	uintptr_t offset;
	int region;
	int32_t current_min_version;
	int ret;

	region = get_latest_rollback(&current_min_version);

	if (region < 0)
		return EC_ERROR_UNKNOWN;

	/* Do not accept to decrement the value. */
	if (next_min_version < current_min_version)
		return EC_ERROR_INVAL;

	/* No need to update if version is already correct. */
	if (next_min_version == current_min_version)
		return EC_SUCCESS;

	/* Use the other region. */
	region = (region + 1) % ROLLBACK_REGIONS;

	offset = get_rollback_offset(region);

	data.rollback_min_version = next_min_version;
	data.cookie = CROS_EC_ROLLBACK_COOKIE;

	if (system_unsafe_to_overwrite(offset, CONFIG_FLASH_ERASE_SIZE))
		return EC_ERROR_ACCESS_DENIED;

	ret = flash_erase(offset, CONFIG_FLASH_ERASE_SIZE);
	if (ret)
		return ret;

	return flash_write(offset, sizeof(data), (char *)&data);
}

static int command_rollback_info(int argc, char **argv)
{
	int region, ret, min_region;
	int32_t rollback_min_version;

	min_region = get_latest_rollback(&rollback_min_version);

	if (min_region < 0)
		return EC_ERROR_UNKNOWN;

	ccprintf("rollback minimum version: %d\n", rollback_min_version);

	for (region = 0; region < ROLLBACK_REGIONS; region++) {
		struct rollback_data data;

		ret = read_rollback(region, &data);
		if (ret)
			return ret;

		ccprintf("rollback %d: %08x %08x%s\n",
			region, data.rollback_min_version, data.cookie,
			min_region == region ? " *" : "");
	}

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(rollbackinfo, command_rollback_info,
			     NULL,
			     "Print rollback info");

static int command_rollback_update(int argc, char **argv)
{
	int32_t min_version;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	min_version = strtoi(argv[1], &e, 0);

	if (*e || min_version < 0)
		return EC_ERROR_PARAM1;

	return rollback_update(min_version);
}
DECLARE_CONSOLE_COMMAND(rollbackupdate, command_rollback_update,
			"min_version",
			"Update rollback info");

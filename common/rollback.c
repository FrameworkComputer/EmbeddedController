/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Rollback protection logic. */

#include "common.h"
#include "console.h"
#include "flash.h"
#include "rollback.h"
#include "sha256.h"
#include "system.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/* Number of rollback regions */
#define ROLLBACK_REGIONS 2

/*
 * Note: Do not change this structure without also updating
 * common/firmware_image.S .image.ROLLBACK section.
 */
struct rollback_data {
	int32_t id; /* Incrementing number to indicate which region to use. */
	int32_t rollback_min_version;
#ifdef CONFIG_ROLLBACK_SECRET_SIZE
	uint8_t secret[CONFIG_ROLLBACK_SECRET_SIZE];
#endif
	/* cookie must always be last, as it validates the rest of the data. */
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
 * @data: Returns most recent rollback data block. The data is filled
 *        with zeros if no valid rollback block is present
 *
 * Return most recent region index on success (>= 0, or 0 if no rollback
 * region is valid), negative value on error.
 */
static int get_latest_rollback(struct rollback_data *data)
{
	int region;
	int min_region = -1;
	int max_id = -1;

	for (region = 0; region < ROLLBACK_REGIONS; region++) {
		struct rollback_data tmp_data;

		if (read_rollback(region, &tmp_data))
			return -1;

		/* Check if not initialized or invalid cookie. */
		if (tmp_data.cookie != CROS_EC_ROLLBACK_COOKIE)
			continue;

		if (tmp_data.id > max_id) {
			min_region = region;
			max_id = tmp_data.id;
		}
	}

	if (min_region >= 0) {
		if (read_rollback(min_region, data))
			return -1;
	} else {
		min_region = 0;
		memset(data, 0, sizeof(*data));
	}

	return min_region;
}

int32_t rollback_get_minimum_version(void)
{
	struct rollback_data data;

	if (get_latest_rollback(&data) < 0)
		return -1;

	return data.rollback_min_version;
}

#ifdef CONFIG_ROLLBACK_SECRET_SIZE
int rollback_get_secret(uint8_t *secret)
{
	struct rollback_data data;
	uint8_t first;
	int i = 0;

	if (get_latest_rollback(&data) < 0)
		return EC_ERROR_UNKNOWN;

	/* Check that secret is not full of 0x00 or 0xff */
	first = data.secret[0];
	if (first == 0x00 || first == 0xff) {
		for (i = 1; i < sizeof(data.secret); i++) {
			if (data.secret[i] != first)
				goto good;
		}
		return EC_ERROR_UNKNOWN;
	}

good:
	memcpy(secret, data.secret, sizeof(data.secret));
	return EC_SUCCESS;
}
#endif

int rollback_lock(void)
{
	int ret;

	/* Already locked */
	if (flash_get_protect() & EC_FLASH_PROTECT_ROLLBACK_NOW)
		return EC_SUCCESS;

	CPRINTS("Protecting rollback");

	/* This may do nothing if WP is not enabled, or RO is not protected. */
	ret = flash_set_protect(EC_FLASH_PROTECT_ROLLBACK_AT_BOOT, -1);

	if (!(flash_get_protect() & EC_FLASH_PROTECT_ROLLBACK_NOW) &&
	      flash_get_protect() & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) {
		/*
		 * If flash protection is still not enabled (some chips may
		 * be able to enable it immediately), reboot.
		 */
		cflush();
		system_reset(SYSTEM_RESET_HARD | SYSTEM_RESET_PRESERVE_FLAGS);
	}

	return ret;
}

#ifdef CONFIG_ROLLBACK_UPDATE

#ifdef CONFIG_ROLLBACK_SECRET_SIZE
static int add_entropy(uint8_t *dst, const uint8_t *src,
			uint8_t *add, unsigned int add_len)
{
#ifdef CONFIG_SHA256
BUILD_ASSERT(SHA256_DIGEST_SIZE == CONFIG_ROLLBACK_SECRET_SIZE);
	struct sha256_ctx ctx;
	uint8_t *hash;
	uint8_t extra;
	int i;

	SHA256_init(&ctx);
	SHA256_update(&ctx, src, CONFIG_ROLLBACK_SECRET_SIZE);
	SHA256_update(&ctx, add, add_len);
#ifdef CONFIG_ROLLBACK_SECRET_LOCAL_ENTROPY_SIZE
	/* Add some locally produced entropy */
	for (i = 0; i < CONFIG_ROLLBACK_SECRET_LOCAL_ENTROPY_SIZE; i++) {
		if (!board_get_entropy(&extra, 1))
			return 0;
		SHA256_update(&ctx, &extra, 1);
	}
#endif
	hash = SHA256_final(&ctx);

	memcpy(dst, hash, CONFIG_ROLLBACK_SECRET_SIZE);
#else
#error "Adding entropy to secret in rollback region requires SHA256."
#endif
	return 1;
}
#endif /* CONFIG_ROLLBACK_SECRET_SIZE */

/**
 * Update rollback block.
 *
 * @param next_min_version	Minimum version to update in rollback block. Can
 *				be a negative value if entropy is provided (in
 *				that case the current minimum version is kept).
 * @param entropy		Entropy to be added to rollback block secret
 *				(can be NULL, in that case no entropy is added).
 * @param len			entropy length
 *
 * @return EC_SUCCESS on success, EC_ERROR_* on error.
 */
static int rollback_update(int32_t next_min_version,
			   uint8_t *entropy, unsigned int length)
{
	struct rollback_data data;
	uintptr_t offset;
	int region;

	if (flash_get_protect() & EC_FLASH_PROTECT_ROLLBACK_NOW)
		return EC_ERROR_ACCESS_DENIED;

	region = get_latest_rollback(&data);

	if (region < 0)
		return EC_ERROR_UNKNOWN;

#ifdef CONFIG_ROLLBACK_SECRET_SIZE
	if (entropy) {
		/* Do not accept to decrease the value. */
		if (next_min_version < data.rollback_min_version)
			next_min_version = data.rollback_min_version;
	} else
#endif
	{
		/* Do not accept to decrease the value. */
		if (next_min_version < data.rollback_min_version)
			return EC_ERROR_INVAL;

		/* No need to update if version is already correct. */
		if (next_min_version == data.rollback_min_version)
			return EC_SUCCESS;
	}

	/* Use the other region. */
	region = (region + 1) % ROLLBACK_REGIONS;

	offset = get_rollback_offset(region);

	data.id = data.id + 1;
	data.rollback_min_version = next_min_version;
#ifdef CONFIG_ROLLBACK_SECRET_SIZE
	/*
	 * If we are provided with some entropy, add it to secret. Otherwise,
	 * data.secret is left untouched and written back to the other region.
	 */
	if (entropy) {
		if (!add_entropy(data.secret, data.secret, entropy, length))
			return EC_ERROR_UNCHANGED;
	}
#endif
	data.cookie = CROS_EC_ROLLBACK_COOKIE;

	/* Offset should never be part of active image. */
	if (system_unsafe_to_overwrite(offset, CONFIG_FLASH_ERASE_SIZE))
		return EC_ERROR_UNKNOWN;

	if (flash_erase(offset, CONFIG_FLASH_ERASE_SIZE))
		return EC_ERROR_UNKNOWN;

	if (flash_write(offset, sizeof(data), (char *)&data))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

int rollback_update_version(int32_t next_min_version)
{
	return rollback_update(next_min_version, NULL, 0);
}

int rollback_add_entropy(uint8_t *data, unsigned int len)
{
	return rollback_update(-1, data, len);
}

static int command_rollback_update(int argc, char **argv)
{
	int32_t min_version;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	min_version = strtoi(argv[1], &e, 0);

	if (*e || min_version < 0)
		return EC_ERROR_PARAM1;

	return rollback_update_version(min_version);
}
DECLARE_CONSOLE_COMMAND(rollbackupdate, command_rollback_update,
			"min_version",
			"Update rollback info");

#ifdef CONFIG_ROLLBACK_SECRET_SIZE
static int command_rollback_add_entropy(int argc, char **argv)
{
	int len;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	len = strlen(argv[1]);

	return rollback_add_entropy(argv[1], len);
}
DECLARE_CONSOLE_COMMAND(rollbackaddent, command_rollback_add_entropy,
			"data",
			"Add entropy to rollback block");
#endif
#endif /* CONFIG_ROLLBACK_UPDATE */

static int command_rollback_info(int argc, char **argv)
{
	int region, ret, min_region;
	int32_t rw_rollback_version;
	struct rollback_data data;

	min_region = get_latest_rollback(&data);

	if (min_region < 0)
		return EC_ERROR_UNKNOWN;

	rw_rollback_version = system_get_rollback_version(SYSTEM_IMAGE_RW);

	ccprintf("rollback minimum version: %d\n", data.rollback_min_version);
	ccprintf("RW rollback version: %d\n", rw_rollback_version);

	for (region = 0; region < ROLLBACK_REGIONS; region++) {
		struct rollback_data data;

		ret = read_rollback(region, &data);
		if (ret)
			return ret;

		ccprintf("rollback %d: %08x %08x %08x",
			region, data.id, data.rollback_min_version,
			data.cookie);
#ifdef CONFIG_ROLLBACK_SECRET_SIZE
		if (!system_is_locked()) {
			/* If system is unlocked, show some of the secret. */
			ccprintf(" [%02x..%02x]", data.secret[0],
				data.secret[CONFIG_ROLLBACK_SECRET_SIZE-1]);
		}
#endif
		if (min_region == region)
			ccprintf(" *");
		ccprintf("\n");
	}

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(rollbackinfo, command_rollback_info,
			     NULL,
			     "Print rollback info");

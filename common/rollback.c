/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Rollback protection logic. */

#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "flash.h"
#include "hooks.h"
#include "host_command.h"
#ifdef CONFIG_MPU
#include "mpu.h"
#endif
#include "otp_key.h"
#include "rollback.h"
#include "rollback_private.h"
#include "sha256.h"
#include "system.h"
#include "task.h"
#include "trng.h"
#include "util.h"

#ifdef CONFIG_ROLLBACK_SECRET_SIZE
#ifdef CONFIG_BORINGSSL_CRYPTO
#include "openssl/mem.h"
#define secure_clear(buffer, size) OPENSSL_cleanse(buffer, size)
#elif defined(CONFIG_LIBCRYPTOC)
#include "cryptoc/util.h"
#define secure_clear(buffer, size) always_memset(buffer, 0, size)
#else
/* Copied from OpenSSL crypto/mem_clr.c
 * Function call through volatile pointer should survive through optimization.
 */
static void *(*volatile memset_fn)(void *, int, size_t) = memset;

test_export_static void secure_clear(void *buffer, size_t size)
{
	memset_fn(buffer, 0, size);
}
#endif
#endif

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/* Number of rollback regions */
#define ROLLBACK_REGIONS 2

static int get_rollback_offset(int region)
{
#ifdef CONFIG_FLASH_MULTIPLE_REGION
	int rv;
	int rollback_start_bank = crec_flash_bank_index(CONFIG_ROLLBACK_OFF);

	rv = crec_flash_bank_start_offset(rollback_start_bank + region);
	ASSERT(rv >= 0);
	return rv;
#else
	return CONFIG_ROLLBACK_OFF + region * CONFIG_FLASH_ERASE_SIZE;
#endif
}

/*
 * When MPU is available, read rollback with interrupts disabled, to minimize
 * time protection is left open.
 */
static void lock_rollback(uint32_t key)
{
#ifdef CONFIG_ROLLBACK_MPU_PROTECT
	mpu_lock_rollback(1);
	irq_unlock(key);
#endif
}

static uint32_t unlock_rollback(void)
{
#ifdef CONFIG_ROLLBACK_MPU_PROTECT
	uint32_t key;

	key = irq_lock();
	mpu_lock_rollback(0);
	return key;
#else
	return 0;
#endif
}

static void clear_rollback(struct rollback_data *data)
{
#ifdef CONFIG_ROLLBACK_SECRET_SIZE
	secure_clear(data->secret, sizeof(data->secret));
#endif
}

int read_rollback(int region, struct rollback_data *data)
{
	int offset;
	int ret = EC_SUCCESS;
	uint32_t key;

	offset = get_rollback_offset(region);

	key = unlock_rollback();
	if (crec_flash_read(offset, sizeof(*data), (char *)data))
		ret = EC_ERROR_UNKNOWN;
	lock_rollback(key);

	return ret;
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
test_mockable_static int get_latest_rollback(struct rollback_data *data)
{
	int ret = -1;
	int region;
	int min_region = -1;
	int max_id = -1;
	struct rollback_data tmp_data;

	for (region = 0; region < ROLLBACK_REGIONS; region++) {
		if (read_rollback(region, &tmp_data))
			goto failed;

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
			goto failed;
	} else {
		min_region = 0;
		clear_rollback(data);
	}
	ret = min_region;

failed:
	clear_rollback(&tmp_data);
	return ret;
}

int32_t rollback_get_minimum_version(void)
{
	struct rollback_data data;
	int32_t ret = -1;

	if (get_latest_rollback(&data) < 0)
		goto failed;
	ret = data.rollback_min_version;

failed:
	clear_rollback(&data);
	return ret;
}

#ifdef CONFIG_ROLLBACK_SECRET_SIZE
test_mockable enum ec_error_list rollback_get_secret(uint8_t *secret)
{
	enum ec_error_list ret = EC_ERROR_UNKNOWN;
	struct rollback_data data;

	if (get_latest_rollback(&data) < 0)
		goto failed;

	/* Check that secret is not full of 0x00 or 0xff */
	if (bytes_are_trivial(data.secret, sizeof(data.secret)))
		goto failed;

	memcpy(secret, data.secret, sizeof(data.secret));
	ret = EC_SUCCESS;
failed:
	clear_rollback(&data);
	return ret;
}
#endif

#ifdef CONFIG_ROLLBACK_UPDATE
static int get_rollback_erase_size_bytes(int region)
{
	int erase_size;

#ifndef CONFIG_FLASH_MULTIPLE_REGION
	erase_size = CONFIG_FLASH_ERASE_SIZE;
#else
	int rollback_start_bank = crec_flash_bank_index(CONFIG_ROLLBACK_OFF);

	erase_size = crec_flash_bank_erase_size(rollback_start_bank + region);
#endif
	ASSERT(erase_size > 0);
	ASSERT(ROLLBACK_REGIONS * erase_size <= CONFIG_ROLLBACK_SIZE);
	ASSERT(sizeof(struct rollback_data) <= erase_size);
	return erase_size;
}

#ifdef CONFIG_ROLLBACK_SECRET_SIZE
#if (defined CONFIG_SHA256_SW || defined CONFIG_SHA256_HW_ACCELERATE)
static int add_entropy(uint8_t *dst, const uint8_t *src, const uint8_t *add,
		       unsigned int add_len)
{
	int ret = 0;
	BUILD_ASSERT(SHA256_DIGEST_SIZE == CONFIG_ROLLBACK_SECRET_SIZE);
	struct sha256_ctx ctx;
	uint8_t *hash;

	SHA256_init(&ctx);
	SHA256_update(&ctx, src, CONFIG_ROLLBACK_SECRET_SIZE);
	SHA256_update(&ctx, add, add_len);
#ifdef CONFIG_ROLLBACK_SECRET_LOCAL_ENTROPY_SIZE
	/* Add some locally produced entropy */
	for (int i = 0; i < CONFIG_ROLLBACK_SECRET_LOCAL_ENTROPY_SIZE; i++) {
		uint8_t extra;

		if (!board_get_entropy(&extra, 1))
			goto failed;
		SHA256_update(&ctx, &extra, 1);
	}
#endif
	hash = SHA256_final(&ctx);

	memcpy(dst, hash, CONFIG_ROLLBACK_SECRET_SIZE);
	ret = 1;

#ifdef CONFIG_ROLLBACK_SECRET_LOCAL_ENTROPY_SIZE
failed:
#endif
	secure_clear(&ctx, sizeof(ctx));
	return ret;
}
#else
#error "Adding entropy to secret in rollback region requires SHA256."
#endif /* CONFIG_SHA256_SW */
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
static int rollback_update(int32_t next_min_version, const uint8_t *entropy,
			   unsigned int length)
{
	/*
	 * When doing flash_write operation, the data needs to be in blocks
	 * of CONFIG_FLASH_WRITE_SIZE, pad rollback_data as required.
	 */
	uint8_t block[CONFIG_FLASH_WRITE_SIZE *
		      DIV_ROUND_UP(sizeof(struct rollback_data),
				   CONFIG_FLASH_WRITE_SIZE)];
	struct rollback_data *data = (struct rollback_data *)block;
	BUILD_ASSERT(sizeof(block) >= sizeof(*data));
	int erase_size, offset, region, ret;
	uint32_t key;

	if (crec_flash_get_protect() & EC_FLASH_PROTECT_ROLLBACK_NOW) {
		ret = EC_ERROR_ACCESS_DENIED;
		goto out;
	}

	/* Initialize the rest of the block. */
	memset(&block[sizeof(*data)], 0xff, sizeof(block) - sizeof(*data));

	region = get_latest_rollback(data);

	if (region < 0) {
		ret = EC_ERROR_UNKNOWN;
		goto out;
	}

#ifdef CONFIG_ROLLBACK_SECRET_SIZE
	if (entropy) {
		/* Do not accept to decrease the value. */
		if (next_min_version < data->rollback_min_version)
			next_min_version = data->rollback_min_version;
	} else
#endif
	{
		/* Do not accept to decrease the value. */
		if (next_min_version < data->rollback_min_version) {
			ret = EC_ERROR_INVAL;
			goto out;
		}

		/* No need to update if version is already correct. */
		if (next_min_version == data->rollback_min_version) {
			ret = EC_SUCCESS;
			goto out;
		}
	}

	/* Use the other region. */
	region = (region + 1) % ROLLBACK_REGIONS;

	offset = get_rollback_offset(region);

	data->id = data->id + 1;
	data->rollback_min_version = next_min_version;
#ifdef CONFIG_ROLLBACK_SECRET_SIZE
	/*
	 * If we are provided with some entropy, add it to secret. Otherwise,
	 * data.secret is left untouched and written back to the other region.
	 */
	if (entropy) {
		if (!add_entropy(data->secret, data->secret, entropy, length)) {
			ret = EC_ERROR_UNCHANGED;
			goto out;
		}
	}
#endif
	data->cookie = CROS_EC_ROLLBACK_COOKIE;

	erase_size = get_rollback_erase_size_bytes(region);

	if (erase_size < 0) {
		ret = EC_ERROR_UNKNOWN;
		goto out;
	}

	/* Offset should never be part of active image. */
	if (system_unsafe_to_overwrite(offset, erase_size)) {
		ret = EC_ERROR_UNKNOWN;
		goto out;
	}

	key = unlock_rollback();
	if (crec_flash_erase(offset, erase_size)) {
		ret = EC_ERROR_UNKNOWN;
		lock_rollback(key);
		goto out;
	}

	ret = crec_flash_write(offset, sizeof(block), block);
	lock_rollback(key);

out:
	clear_rollback(data);
	return ret;
}

int rollback_update_version(int32_t next_min_version)
{
	return rollback_update(next_min_version, NULL, 0);
}

int rollback_add_entropy(const uint8_t *data, unsigned int len)
{
	if (IS_ENABLED(CONFIG_OTP_KEY)) {
		uint32_t status = EC_ERROR_UNKNOWN;

		status = otp_key_provision();
		if (status != EC_SUCCESS) {
			ccprintf("failed to provision OTP key with status=%d",
				 status);
			return status;
		}
	}

	return rollback_update(-1, data, len);
}

static int command_rollback_update(int argc, const char **argv)
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
DECLARE_CONSOLE_COMMAND(rollbackupdate, command_rollback_update, "min_version",
			"Update rollback info");

#ifdef CONFIG_ROLLBACK_SECRET_SIZE
static int command_rollback_add_entropy(int argc, const char **argv)
{
	uint8_t rand[CONFIG_ROLLBACK_SECRET_SIZE];
	const uint8_t *data;
	int len;

	if (argc < 2) {
		if (!IS_ENABLED(CONFIG_RNG))
			return EC_ERROR_PARAM_COUNT;

		trng_init();
		trng_rand_bytes(rand, sizeof(rand));
		trng_exit();

		data = rand;
		len = sizeof(rand);
	} else {
		data = argv[1];
		len = strlen(argv[1]);
	}

	return rollback_add_entropy(data, len);
}
DECLARE_CONSOLE_COMMAND(rollbackaddent, command_rollback_add_entropy, "[data]",
			"Add entropy to rollback block");

#ifdef CONFIG_RNG
static int add_entropy_action;
static int add_entropy_rv = EC_RES_UNAVAILABLE;

static void add_entropy_deferred(void)
{
	uint8_t rand[CONFIG_ROLLBACK_SECRET_SIZE];
	int repeat = 1;

	/*
	 * If asked to reset the old secret, just add entropy multiple times,
	 * which will ping-pong between the blocks.
	 */
	if (add_entropy_action == ADD_ENTROPY_RESET_ASYNC)
		repeat = ROLLBACK_REGIONS;

	trng_init();
	do {
		trng_rand_bytes(rand, sizeof(rand));
		if (rollback_add_entropy(rand, sizeof(rand)) != EC_SUCCESS) {
			add_entropy_rv = EC_RES_ERROR;
			goto out;
		}
	} while (--repeat);

	add_entropy_rv = EC_RES_SUCCESS;
out:
	trng_exit();
}
DECLARE_DEFERRED(add_entropy_deferred);

static enum ec_status
hc_rollback_add_entropy(struct host_cmd_handler_args *args)
{
	const struct ec_params_rollback_add_entropy *p = args->params;

	switch (p->action) {
	case ADD_ENTROPY_ASYNC:
	case ADD_ENTROPY_RESET_ASYNC:
		if (add_entropy_rv == EC_RES_BUSY)
			return EC_RES_BUSY;

		add_entropy_action = p->action;
		add_entropy_rv = EC_RES_BUSY;
		hook_call_deferred(&add_entropy_deferred_data, 0);

		return EC_RES_SUCCESS;

	case ADD_ENTROPY_GET_RESULT:
		return add_entropy_rv;
	}

	return EC_RES_INVALID_PARAM;
}
DECLARE_HOST_COMMAND(EC_CMD_ADD_ENTROPY, hc_rollback_add_entropy,
		     EC_VER_MASK(0));
#endif /* CONFIG_RNG */
#endif /* CONFIG_ROLLBACK_SECRET_SIZE */
#endif /* CONFIG_ROLLBACK_UPDATE */

static int command_rollback_info(int argc, const char **argv)
{
	int ret = EC_ERROR_UNKNOWN;
	int region, min_region;
	int32_t rw_rollback_version;
	struct rollback_data data;

	min_region = get_latest_rollback(&data);

	if (min_region < 0)
		goto failed;

	rw_rollback_version = system_get_rollback_version(EC_IMAGE_RW);

	ccprintf("rollback minimum version: %d\n", data.rollback_min_version);
	ccprintf("RW rollback version: %d\n", rw_rollback_version);

	for (region = 0; region < ROLLBACK_REGIONS; region++) {
		ret = read_rollback(region, &data);
		if (ret)
			goto failed;

		ccprintf("rollback %d: %08x %08x %08x", region, data.id,
			 data.rollback_min_version, data.cookie);
#ifdef CONFIG_ROLLBACK_SECRET_SIZE
		if (!system_is_locked()) {
			/* If system is unlocked, show some of the secret. */
			ccprintf(" [%02x..%02x]", data.secret[0],
				 data.secret[CONFIG_ROLLBACK_SECRET_SIZE - 1]);
		}
#endif
		if (min_region == region)
			ccprintf(" *");
		ccprintf("\n");
	}
	ret = EC_SUCCESS;

failed:
	clear_rollback(&data);
	return ret;
}
DECLARE_SAFE_CONSOLE_COMMAND(rollbackinfo, command_rollback_info, NULL,
			     "Print rollback info");

static enum ec_status
host_command_rollback_info(struct host_cmd_handler_args *args)
{
	int ret = EC_RES_UNAVAILABLE;
	struct ec_response_rollback_info *r = args->response;
	int min_region;
	struct rollback_data data;

	min_region = get_latest_rollback(&data);

	if (min_region < 0)
		goto failed;

	r->id = data.id;
	r->rollback_min_version = data.rollback_min_version;
	r->rw_rollback_version = system_get_rollback_version(EC_IMAGE_RW);

	args->response_size = sizeof(*r);
	ret = EC_RES_SUCCESS;

failed:
	clear_rollback(&data);
	return ret;
}
DECLARE_HOST_COMMAND(EC_CMD_ROLLBACK_INFO, host_command_rollback_info,
		     EC_VER_MASK(0));

/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "rsa.h"
#include "sha256.h"
#include "shared_mem.h"
#include "vboot.h"

#define CPRINTS(format, args...) cprints(CC_VBOOT, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_VBOOT, format, ## args)

int vboot_is_padding_valid(const uint8_t *data, uint32_t start, uint32_t end)
{
	const uint32_t *data32 = (const uint32_t *)data;
	int i;

	if (start > end)
		return EC_ERROR_INVAL;

	if (start % 4 || end % 4)
		return EC_ERROR_INVAL;

	for (i = start / 4; i < end / 4; i++) {
		if (data32[i] != 0xffffffff)
			return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

int vboot_verify(const uint8_t *data, int len,
		 const struct rsa_public_key *key, const uint8_t *sig)
{
	struct sha256_ctx ctx;
	uint8_t *hash;
	uint32_t *workbuf;
	int err = EC_SUCCESS;

	if (SHARED_MEM_ACQUIRE_CHECK(3 * RSANUMBYTES, (char **)&workbuf))
		return EC_ERROR_MEMORY_ALLOCATION;

	/* Compute hash of the RW firmware */
	SHA256_init(&ctx);
	SHA256_update(&ctx, data, len);
	hash = SHA256_final(&ctx);

	/* Verify the data */
	if (rsa_verify(key, sig, hash, workbuf) != 1)
		err = EC_ERROR_VBOOT_DATA_VERIFY;

	shared_mem_release(workbuf);

	return err;
}

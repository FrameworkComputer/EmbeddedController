/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "dcrypto.h"
#include "internal.h"
#include "endian.h"
#include "registers.h"

#include "cryptoc/util.h"

#include "console.h"

const char *const dcrypto_app_names[] = {
	"RESERVED",
	"NVMEM",
	"U2F_ATTEST",
	"U2F_ORIGIN",
	"U2F_WRAP",
	/* This key signs data from H1's configured by mn50/scribe. */
	"PERSO_AUTH",
	"PINWEAVER",
};

static void name_hash(enum dcrypto_appid appid,
		      uint32_t digest[SHA256_DIGEST_WORDS])
{
	LITE_SHA256_CTX ctx;
	const char *name = dcrypto_app_names[appid];
	size_t x;

	/* The PERSO_AUTH digest was improperly defined, so now this exception
	 * exists to prevent data loss.
	 */
	if (appid == PERSO_AUTH) {
		digest[0] = 0x2019da34;
		digest[1] = 0xf1a01a13;
		digest[2] = 0x0fb9f73f;
		digest[3] = 0xf2e85f76;
		digest[4] = 0x5ecb7690;
		digest[5] = 0x09f732c9;
		digest[6] = 0xe540bf14;
		digest[7] = 0xcc46799a;
		return;
	}

	DCRYPTO_SHA256_init(&ctx, 0);
	HASH_update(&ctx, name, strlen(name));
	memcpy(digest, HASH_final(&ctx), SHA256_DIGEST_SIZE);

	/* The digests were originally endian swapped because xxd was used to
	 * print them so this operation is needed to keep the derived keys the
	 * same. Any changes to they key derivation process must result in the
	 * same keys being produced given the same inputs, or devices will
	 * effectively be reset and user data will be lost by the key change.
	 */
	for (x = 0; x < SHA256_DIGEST_WORDS; ++x)
		digest[x] = __builtin_bswap32(digest[x]);
}

int DCRYPTO_appkey_init(enum dcrypto_appid appid, struct APPKEY_CTX *ctx)
{
	uint32_t digest[SHA256_DIGEST_WORDS];

	memset(ctx, 0, sizeof(*ctx));
	name_hash(appid, digest);

	if (!dcrypto_ladder_compute_usr(appid, digest))
		return 0;

	return 1;
}

void DCRYPTO_appkey_finish(struct APPKEY_CTX *ctx)
{
	always_memset(ctx, 0, sizeof(struct APPKEY_CTX));
	GREG32(KEYMGR, AES_WIPE_SECRETS) = 1;
}

int DCRYPTO_appkey_derive(enum dcrypto_appid appid, const uint32_t input[8],
			  uint32_t output[8])
{
	uint32_t digest[SHA256_DIGEST_WORDS];

	name_hash(appid, digest);
	return !!dcrypto_ladder_derive(appid, digest, input, output);
}

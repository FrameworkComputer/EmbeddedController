/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "dcrypto.h"
#include "internal.h"
#include "endian.h"
#include "registers.h"

#include "cryptoc/util.h"

const struct {
	const char *name;
	/* SHA256(name) */
	const uint32_t digest[SHA256_DIGEST_WORDS];
} dcrypto_app_names[] = {
	{
		"RESERVED",
		{
			0x89ef2e22,  0x0032b61a,  0x7b349ab1,  0x3f512449,
			0x4cd161dd,  0x2a6cac94,  0x109a045a,  0x23d669ea
		}
	},
	{
		"NVMEM",
		{
			0xd137e92f,  0x0f39686e,  0xd663f548,  0x9b570397,
			0x5801c4ce,  0x8e7c7654,  0xa2a13c85,  0x875779b6
		}
	},
	{
		"U2F_ATTEST",
		{
			0xe108bde1,  0xb87820a9,  0x8b4b943a,  0xc7c1dbc4,
			0xa027d3f1,  0x96538c5f,  0x49a07d16,  0xd0c8e1da
		}
	},
	{
		"U2F_ORIGIN",
		{
			0xeb4ba9f1,  0x12b0ec6c,  0xd0791cd4,  0x4a1f4e6d,
			0x51e60c00,  0xad84c2c0,  0x38b78b24,  0x1ded57ea
		}
	},
	{
		"U2F_WRAP",
		{
			0xa013e112,  0x4cb0134c,  0x1cab1edf,  0xbd741b61,
			0xcd375bcd,  0x8065e8cc,  0xc892ed69,  0x72436c7d
		}
	},
};

int DCRYPTO_appkey_init(enum dcrypto_appid appid, struct APPKEY_CTX *ctx)
{
	memset(ctx, 0, sizeof(*ctx));

	if (!dcrypto_ladder_compute_usr(
			appid, dcrypto_app_names[appid].digest))
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
	return !!dcrypto_ladder_derive(appid, dcrypto_app_names[appid].digest,
				       input, output);
}

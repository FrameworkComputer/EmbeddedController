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
	/* SHA256(name, strlen(name)) */
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

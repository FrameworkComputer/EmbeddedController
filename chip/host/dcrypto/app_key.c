/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"

static int dcrypto_appkey_init_flag_ = -1;

int DCRYPTO_appkey_init(enum dcrypto_appid appid, struct APPKEY_CTX *ctx)
{
	if (dcrypto_appkey_init_flag_ != -1)
		return 0;

	dcrypto_appkey_init_flag_ = appid;
	return 1;
}

void DCRYPTO_appkey_finish(struct APPKEY_CTX *ctx)
{
	memset(ctx, 0, sizeof(struct APPKEY_CTX));
	dcrypto_appkey_init_flag_ = -1;
}

int DCRYPTO_appkey_derive(enum dcrypto_appid appid, const uint32_t input[8],
			  uint32_t output[8])
{
	/* See README.md for while this is a passthrough. */
	memcpy(output, input, SHA256_DIGEST_SIZE);
	return 1;
}

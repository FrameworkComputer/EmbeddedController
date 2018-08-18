/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Provides the minimal declarations needed by pinweaver to build on
 * CHIP_HOST. While it might be preferable to simply use the original dcrypto.h,
 * That would require incorporating additional headers / dependencies such as
 * cryptoc.
 */

#ifndef __CROS_EC_DCRYPTO_HOST_H
#define __CROS_EC_DCRYPTO_HOST_H
#include <stdint.h>
#include <string.h>

/* Allow tests to return a faked result for the purpose of testing. If
 * this is not set, a combination of cryptoc and openssl are used for the
 * dcrypto implementation.
 */
#ifndef CONFIG_DCRYPTO_MOCK

/* If not using the mock struct definitions, use the ones from Cr50. */
#include "chip/g/dcrypto/dcrypto.h"

#else  /* defined(CONFIG_DCRYPTO_MOCK) */

#include <sha256.h>

#define HASH_CTX sha256_ctx

/* Used as a replacement for declarations in cryptoc that are used by Cr50, but
 * add unnecessary complexity to the test code.
 */
struct dcrypto_mock_ctx_t {
	struct HASH_CTX hash;
};
#define LITE_HMAC_CTX struct dcrypto_mock_ctx_t
#define LITE_SHA256_CTX struct HASH_CTX

void HASH_update(struct HASH_CTX *ctx, const void *data, size_t len);
uint8_t *HASH_final(struct HASH_CTX *ctx);

#define AES256_BLOCK_CIPHER_KEY_SIZE 32
#define SHA256_DIGEST_SIZE 32

enum dcrypto_appid {
	RESERVED = 0,
	NVMEM = 1,
	U2F_ATTEST = 2,
	U2F_ORIGIN = 3,
	U2F_WRAP = 4,
	PERSO_AUTH = 5,
	PINWEAVER = 6,
	/* This enum value should not exceed 7. */
};

void DCRYPTO_SHA256_init(LITE_SHA256_CTX *ctx, uint32_t sw_required);

void DCRYPTO_HMAC_SHA256_init(LITE_HMAC_CTX *ctx, const void *key,
			      unsigned int len);
const uint8_t *DCRYPTO_HMAC_final(LITE_HMAC_CTX *ctx);

int DCRYPTO_aes_ctr(uint8_t *out, const uint8_t *key, uint32_t key_bits,
		    const uint8_t *iv, const uint8_t *in, size_t in_len);

struct APPKEY_CTX {};

int DCRYPTO_appkey_init(enum dcrypto_appid appid, struct APPKEY_CTX *ctx);

void DCRYPTO_appkey_finish(struct APPKEY_CTX *ctx);

int DCRYPTO_appkey_derive(enum dcrypto_appid appid, const uint32_t input[8],
			  uint32_t output[8]);

#endif  /* CONFIG_DCRYPTO_MOCK */

#endif  /* __CROS_EC_HOST_DCRYPTO_H */

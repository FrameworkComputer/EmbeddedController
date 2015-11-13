/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Crypto wrapper library for CR50.
 */
#ifndef EC_BOARD_CR50_DCRYPTO_DCRYPTO_H_
#define EC_BOARD_CR50_DCRYPTO_DCRYPTO_H_

/* TODO(vbendeb) don't forget to disable this for prod builds. */
#define CRYPTO_TEST_SETUP

#include <inttypes.h>

enum cipher_mode {
	CIPHER_MODE_ECB = 0,
	CIPHER_MODE_CTR = 1,
	CIPHER_MODE_CBC = 2,
	CIPHER_MODE_GCM = 3
};

enum encrypt_mode {
	DECRYPT_MODE = 0,
	ENCRYPT_MODE = 1
};

int DCRYPTO_aes_init(const uint8_t *key, uint32_t key_len, const uint8_t *iv,
		enum cipher_mode c_mode, enum encrypt_mode e_mode);
int DCRYPTO_aes_block(const uint8_t *in, uint8_t *out);

void DCRYPTO_aes_write_iv(const uint8_t *iv);
void DCRYPTO_aes_read_iv(uint8_t *iv);

#endif /* ! EC_BOARD_CR50_DCRYPTO_DCRYPTO_H_ */

/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _INCLUDE_RSA_H
#define _INCLUDE_RSA_H

#include "common.h"

#define RSANUMBYTES 256  /* 2048 bit key length */
#define RSANUMWORDS (RSANUMBYTES / sizeof(uint32_t))

/* 2048-bit RSA public key definition */
struct rsa_public_key {
	uint32_t n[RSANUMWORDS];  /* modulus as little endian array */
	uint32_t rr[RSANUMWORDS]; /* R^2 as little endian array */
	uint32_t n0inv;           /* -1 / n[0] mod 2^32 */
};

int rsa_verify(const struct rsa_public_key *key,
	       const uint8_t *signature,
	       const uint8_t *sha,
	       uint32_t *workbuf32);

#endif /* _INCLUDE_RSA_H */

/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _INCLUDE_RSA_H
#define _INCLUDE_RSA_H

#include "config.h"

#ifndef CONFIG_RSA_KEY_SIZE
#define CONFIG_RSA_KEY_SIZE 2048 /* default to 2048-bit key length */
#endif

#define RSANUMBYTES ((CONFIG_RSA_KEY_SIZE)/8)
#define RSANUMWORDS (RSANUMBYTES / sizeof(uint32_t))

#ifdef CONFIG_RSA /* reserve space for public key only if used */
/*
 * The size of the public key structure is
 * 2 x RSANUMBYTES for n and rr fields
 * plus 4 for n0inv, aligned on a multiple of 16
 * Put numerical constants here to please the linker script.
 */
#if CONFIG_RSA_KEY_SIZE == 2048
#define RSA_PUBLIC_KEY_SIZE 528
#elif CONFIG_RSA_KEY_SIZE == 4096
#define RSA_PUBLIC_KEY_SIZE 1040
#elif CONFIG_RSA_KEY_SIZE == 8192
#define RSA_PUBLIC_KEY_SIZE 2064
#else
#error Unsupported RSA key size
#endif
#endif /* CONFIG_RSA */

#ifndef __ASSEMBLER__

#include "common.h"

/* RSA public key definition */
struct rsa_public_key {
	uint32_t n[RSANUMWORDS];  /* modulus as little endian array */
	uint32_t rr[RSANUMWORDS]; /* R^2 as little endian array */
	uint32_t n0inv;           /* -1 / n[0] mod 2^32 */
};

int rsa_verify(const struct rsa_public_key *key,
	       const uint8_t *signature,
	       const uint8_t *sha,
	       uint32_t *workbuf32);

#endif /* !__ASSEMBLER__ */

#endif /* _INCLUDE_RSA_H */

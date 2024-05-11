/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CURVE25519_H
#define __CROS_EC_CURVE25519_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Curve25519.
 *
 * Curve25519 is an elliptic curve. See https://tools.ietf.org/html/rfc7748.
 */


/* X25519.
 *
 * X25519 is the Diffie-Hellman primitive built from curve25519. It is
 * sometimes referred to as “curve25519”, but “X25519” is a more precise
 * name.
 * See http://cr.yp.to/ecdh.html and https://tools.ietf.org/html/rfc7748.
 */

#define X25519_PRIVATE_KEY_LEN 32
#define X25519_PUBLIC_VALUE_LEN 32

/**
 * Generate a public/private key pair.
 * @param out_public_value generated public key.
 * @param out_private_value generated private key.
 */
void X25519_keypair(uint8_t out_public_value[32], uint8_t out_private_key[32]);

/**
 * Diffie-Hellman function.
 * @param out_shared_key
 * @param private_key
 * @param out_public_value
 * @return one on success and zero on error.
 *
 * X25519() writes a shared key to @out_shared_key that is calculated from the
 * given private key and the peer's public value.
 *
 * Don't use the shared key directly, rather use a KDF and also include the two
 * public values as inputs.
 */
int X25519(uint8_t out_shared_key[32], const uint8_t private_key[32],
	   const uint8_t peers_public_value[32]);

/**
 * Compute the matching public key.
 * @param out_public_value computed public key.
 * @param private_key private key to use.
 *
 * X25519_public_from_private() calculates a Diffie-Hellman public value from
 * the given private key and writes it to @out_public_value.
 */
void X25519_public_from_private(uint8_t out_public_value[32],
				const uint8_t private_key[32]);

/*
 * Low-level x25519 function, defined by either the generic or cortex-m0
 * implementation. Must not be called directly.
 */
void x25519_scalar_mult(uint8_t out[32],
			const uint8_t scalar[32],
			const uint8_t point[32]);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_CURVE25519_H */

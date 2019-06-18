/* Copyright 2015, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

/* This code is mostly taken from the ref10 version of Ed25519 in SUPERCOP
 * 20141124 (http://bench.cr.yp.to/supercop.html). That code is released as
 * public domain but this file has the ISC license just to keep licencing
 * simple.
 *
 * The field functions are shared by Ed25519 and X25519 where possible. */

#include "common.h"
#include "curve25519.h"
#include "trng.h"
#include "util.h"
#define CRYPTO_memcmp safe_memcmp

#ifdef CONFIG_RNG
void X25519_keypair(uint8_t out_public_value[32], uint8_t out_private_key[32]) {
  rand_bytes(out_private_key, 32);

  /* All X25519 implementations should decode scalars correctly (see
   * https://tools.ietf.org/html/rfc7748#section-5). However, if an
   * implementation doesn't then it might interoperate with random keys a
   * fraction of the time because they'll, randomly, happen to be correctly
   * formed.
   *
   * Thus we do the opposite of the masking here to make sure that our private
   * keys are never correctly masked and so, hopefully, any incorrect
   * implementations are deterministically broken.
   *
   * This does not affect security because, although we're throwing away
   * entropy, a valid implementation of scalarmult should throw away the exact
   * same bits anyway. */
  out_private_key[0] |= 7;
  out_private_key[31] &= 63;
  out_private_key[31] |= 128;

  X25519_public_from_private(out_public_value, out_private_key);
}
#endif

int X25519(uint8_t out_shared_key[32], const uint8_t private_key[32],
           const uint8_t peer_public_value[32]) {
  static const uint8_t kZeros[32] = {0};
  x25519_scalar_mult(out_shared_key, private_key, peer_public_value);
  /* The all-zero output results when the input is a point of small order. */
  return CRYPTO_memcmp(kZeros, out_shared_key, 32) != 0;
}

void X25519_public_from_private(uint8_t out_public_value[32],
                                const uint8_t private_key[32]) {
  static const uint8_t kMongomeryBasePoint[32] = {9};
  x25519_scalar_mult(out_public_value, private_key, kMongomeryBasePoint);
}

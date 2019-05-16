/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __INCLUDE_CRYPTO_API_H
#define __INCLUDE_CRYPTO_API_H

#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Calculate hash of an arbitrary data
 *
 * Up to SHA_DIGEST_SIZE byte hash can be generated, if hash_len is
 * longer - it is padded with zeros.
 *
 * @param p_buf: pointer to beginning of data
 * @param num_bytes: length of data in bytes
 * @param p_hash: pointer to where computed hash will be stored
 * @param hash_len: length in bytes to use from sha computation. If this
 *		    value exceeds SHA1 size (20 bytes), the rest of the
 *		    hash is filled up with zeros.
 */
void app_compute_hash(uint8_t *p_buf, size_t num_bytes,
		      uint8_t *p_hash, size_t hash_len);

#define CIPHER_SALT_SIZE 16

/*
 * Encrypt/decrypt a flat blob.
 *
 * Encrypt or decrypt the input buffer, and write the correspondingly
 * ciphered output to out.  The number of bytes produced is equal to
 * the number of input bytes.
 *
 * This API is expected to be applied to a single contiguous region. WARNING:
 * Presently calling this function more than once with "in" pointing to
 * logically different buffers will result in using the same IV value
 * internally and as such reduce encryption efficiency.
 *
 * @param salt pointer to a unique value to be associated with this blob,
 *	       used for derivation of the proper IV, the size of this value
 *	       is as defined by CIPHER_SALT_SIZE above.
 *	       WARNING: a given salt/"in" pair must be unique (it is an ERROR
 *	       to use a given salt with more than one unique buffer).  For an
 *	       example, a good salt would be a digest of the plaintext input.
 * @param out Destination pointer where to write plaintext / ciphertext.
 * @param in  Source pointer where to read ciphertext / plaintext.
 * @param len Number of bytes to read from in / write to out.
 * @return non-zero on success, and zero otherwise.
 */
int app_cipher(const void *salt, void *out, const void *in, size_t size);

/*
 * Return a Boolean showing if crypto hardware is enabled.
 */
int crypto_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_CRYPTO_API_H */

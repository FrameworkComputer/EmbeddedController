/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Key ID, used to quickly match keys with signatures. There's not a standard
 * fingerprint for private keys, so we're using the sha1sum of the public key
 * in our keyb format. Pretty much anything would work as long as it's
 * resistant to collisions and easy to compare.
 *
 * Note: This file is copied from
 * src/platform/vboot_reference/firmware/2lib/include/2id.h
 * and should be updated if necessary.
 */

#ifndef VBOOT_REFERENCE_VBOOT_2ID_H_
#define VBOOT_REFERENCE_VBOOT_2ID_H_
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VB2_ID_NUM_BYTES 20

struct vb2_id {
	uint8_t raw[VB2_ID_NUM_BYTES];
} __attribute__((packed));

#define EXPECTED_ID_SIZE VB2_ID_NUM_BYTES

/* IDs to use for "keys" with sig_alg==VB2_SIG_NONE */
#define VB2_ID_NONE_SHA1            \
	{                           \
		{                   \
			0x00, 0x01, \
		}                   \
	}
#define VB2_ID_NONE_SHA256          \
	{                           \
		{                   \
			0x02, 0x56, \
		}                   \
	}
#define VB2_ID_NONE_SHA512          \
	{                           \
		{                   \
			0x05, 0x12, \
		}                   \
	}

#ifdef __cplusplus
}
#endif

#endif /* VBOOT_REFERENCE_VBOOT_2ID_H_ */

/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Common utility APIs for vboot 2.1
 */

#include "common.h"
#include "rsa.h"
#include "rwsig.h"
#include "vb21_struct.h"
#include "vboot.h"

int vb21_is_packed_key_valid(const struct vb21_packed_key *key)
{
	if (key->c.magic != VB21_MAGIC_PACKED_KEY)
		return EC_ERROR_VBOOT_KEY_MAGIC;
	if (key->key_size != sizeof(struct rsa_public_key))
		return EC_ERROR_VBOOT_KEY_SIZE;
	return EC_SUCCESS;
}

int vb21_is_signature_valid(const struct vb21_signature *sig,
			    const struct vb21_packed_key *key)
{
	if (sig->c.magic != VB21_MAGIC_SIGNATURE)
		return EC_ERROR_VBOOT_SIG_MAGIC;
	if (sig->sig_size != RSANUMBYTES)
		return EC_ERROR_VBOOT_SIG_SIZE;
	if (key->sig_alg != sig->sig_alg)
		return EC_ERROR_VBOOT_SIG_ALGORITHM;
	if (key->hash_alg != sig->hash_alg)
		return EC_ERROR_VBOOT_HASH_ALGORITHM;
	/* Sanity check signature offset and data size. */
	if (sig->sig_offset < sizeof(*sig))
		return EC_ERROR_VBOOT_SIG_OFFSET;
	if (sig->sig_offset + RSANUMBYTES > CONFIG_RW_SIG_SIZE)
		return EC_ERROR_VBOOT_SIG_OFFSET;
	if (sig->data_size > CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE)
		return EC_ERROR_VBOOT_DATA_SIZE;
	return EC_SUCCESS;
}

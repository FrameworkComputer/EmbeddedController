/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Common utility APIs for vboot 2.1
 */

#include "common.h"
#include "flash.h"
#include "host_command.h"
#include "rsa.h"
#include "rwsig.h"
#include "system.h"
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
	/* Validity check signature offset and data size. */
	if (sig->sig_offset < sizeof(*sig))
		return EC_ERROR_VBOOT_SIG_OFFSET;
	if (sig->sig_offset + RSANUMBYTES > CONFIG_RW_SIG_SIZE)
		return EC_ERROR_VBOOT_SIG_OFFSET;
	if (sig->data_size > CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE)
		return EC_ERROR_VBOOT_DATA_SIZE;
	return EC_SUCCESS;
}

const struct vb21_packed_key *vb21_get_packed_key(void)
{
#ifdef CONFIG_MAPPED_STORAGE
	return (const struct vb21_packed_key *)(CONFIG_RO_PUBKEY_READ_ADDR);
#else
	static struct vb21_packed_key key;

	crec_flash_read(CONFIG_RO_PUBKEY_STORAGE_OFF, sizeof(key),
			(char *)&key);
	return &key;
#endif
}

static void read_rwsig_info(struct ec_response_rwsig_info *r)
{
	const struct vb21_packed_key *vb21_key;
	int rv;

	vb21_key = vb21_get_packed_key();

	r->sig_alg = vb21_key->sig_alg;
	r->hash_alg = vb21_key->hash_alg;
	r->key_version = vb21_key->key_version;
	{
		BUILD_ASSERT(sizeof(r->key_id) == sizeof(vb21_key->id),
			     "key ID sizes must match");
	}
	{
		BUILD_ASSERT(sizeof(vb21_key->id) == sizeof(vb21_key->id.raw),
			     "key ID sizes must match");
	}
	memcpy(r->key_id, vb21_key->id.raw, sizeof(r->key_id));

	rv = vb21_is_packed_key_valid(vb21_key);
	r->key_is_valid = (rv == EC_SUCCESS);
}

static int command_rwsig_info(int argc, const char **argv)
{
	int i;
	struct ec_response_rwsig_info r;

	read_rwsig_info(&r);

	ccprintf("sig_alg: %d\n", r.sig_alg);
	ccprintf("key_version: %d\n", r.key_version);
	ccprintf("hash_alg: %d\n", r.hash_alg);
	ccprintf("key_is_valid: %d\n", r.key_is_valid);

	ccprintf("key_id: ");
	for (i = 0; i < sizeof(r.key_id); i++)
		ccprintf("%x", r.key_id[i]);
	ccprintf("\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rwsiginfo, command_rwsig_info, NULL,
			"Display rwsig info on console.");

static enum ec_status
host_command_rwsig_info(struct host_cmd_handler_args *args)
{
	struct ec_response_rwsig_info *r = args->response;

	read_rwsig_info(r);
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_RWSIG_INFO, host_command_rwsig_info,
		     EC_VER_MASK(EC_VER_RWSIG_INFO));

/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test vboot
 */

#include "common.h"
#include "rsa.h"
#include "test_util.h"
#include "vboot.h"
#include "rsa2048-3.h"
#include "rwsig.h"

struct vboot_key {
	struct vb21_packed_key vb21_key;
	struct rsa_public_key key_data;
};

struct vboot_sig {
	struct vb21_signature vb21_sig;
	uint8_t sig_data[RSANUMBYTES];
};

static void reset_data(struct vboot_key *k, struct vboot_sig *s)
{
	k->vb21_key.c.magic = VB21_MAGIC_PACKED_KEY;
	k->vb21_key.key_offset = sizeof(struct vb21_packed_key);
	k->vb21_key.key_size = sizeof(rsa_data);
	memcpy(&k->key_data, rsa_data, sizeof(rsa_data));

	s->vb21_sig.c.magic = VB21_MAGIC_SIGNATURE;
	s->vb21_sig.sig_size = RSANUMBYTES;
	s->vb21_sig.sig_offset = sizeof(struct vb21_signature);
	s->vb21_sig.sig_alg = k->vb21_key.sig_alg;
	s->vb21_sig.hash_alg = k->vb21_key.hash_alg;
	s->vb21_sig.data_size = CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE - 32;
	memcpy(s->sig_data, sig, sizeof(s->sig_data));
}

static int test_vboot(void)
{
	struct vboot_key k;
	struct vboot_sig s;
	uint8_t data[CONFIG_RW_SIZE];
	int len;
	int err;

	/* Success */
	reset_data(&k, &s);
	memset(data, 0xff, CONFIG_RW_SIZE);
	err = vb21_is_packed_key_valid(&k.vb21_key);
	TEST_ASSERT(err == EC_SUCCESS);
	err = vb21_is_signature_valid(&s.vb21_sig, &k.vb21_key);
	TEST_ASSERT(err == EC_SUCCESS);
	len = s.vb21_sig.data_size;
	err = vboot_is_padding_valid(data, len,
				     CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE);
	TEST_ASSERT(err == EC_SUCCESS);

	/* Invalid magic */
	reset_data(&k, &s);
	k.vb21_key.c.magic = VB21_MAGIC_SIGNATURE;
	err = vb21_is_packed_key_valid(&k.vb21_key);
	TEST_ASSERT(err == EC_ERROR_VBOOT_KEY_MAGIC);

	/* Invalid key size */
	reset_data(&k, &s);
	k.vb21_key.key_size--;
	err = vb21_is_packed_key_valid(&k.vb21_key);
	TEST_ASSERT(err == EC_ERROR_VBOOT_KEY_SIZE);

	/* Invalid magic */
	reset_data(&k, &s);
	s.vb21_sig.c.magic = VB21_MAGIC_PACKED_KEY;
	err = vb21_is_signature_valid(&s.vb21_sig, &k.vb21_key);
	TEST_ASSERT(err == EC_ERROR_VBOOT_SIG_MAGIC);

	/* Invalid sig size */
	reset_data(&k, &s);
	s.vb21_sig.sig_size--;
	err = vb21_is_signature_valid(&s.vb21_sig, &k.vb21_key);
	TEST_ASSERT(err == EC_ERROR_VBOOT_SIG_SIZE);

	/* Sig algorithm mismatch */
	reset_data(&k, &s);
	s.vb21_sig.sig_alg++;
	err = vb21_is_signature_valid(&s.vb21_sig, &k.vb21_key);
	TEST_ASSERT(err == EC_ERROR_VBOOT_SIG_ALGORITHM);

	/* Hash algorithm mismatch */
	reset_data(&k, &s);
	s.vb21_sig.hash_alg++;
	err = vb21_is_signature_valid(&s.vb21_sig, &k.vb21_key);
	TEST_ASSERT(err == EC_ERROR_VBOOT_HASH_ALGORITHM);

	/* Invalid sig_offset */
	reset_data(&k, &s);
	s.vb21_sig.sig_offset--;
	err = vb21_is_signature_valid(&s.vb21_sig, &k.vb21_key);
	TEST_ASSERT(err == EC_ERROR_VBOOT_SIG_OFFSET);

	/* Invalid data size */
	reset_data(&k, &s);
	s.vb21_sig.data_size = CONFIG_RW_SIZE;
	err = vb21_is_signature_valid(&s.vb21_sig, &k.vb21_key);
	TEST_ASSERT(err == EC_ERROR_VBOOT_DATA_SIZE);

	/* Invalid padding */
	reset_data(&k, &s);
	len = s.vb21_sig.data_size;
	data[len] = 0;
	err = vboot_is_padding_valid(data, len,
				     CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE);
	TEST_ASSERT(err == EC_ERROR_INVAL);

	/* Invalid padding size */
	reset_data(&k, &s);
	len = s.vb21_sig.data_size + 1;
	err = vboot_is_padding_valid(data, len,
				     CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE);
	TEST_ASSERT(err == EC_ERROR_INVAL);

	/* Padding size is too large */
	reset_data(&k, &s);
	len = s.vb21_sig.data_size + 64;
	err = vboot_is_padding_valid(data, len,
				     CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE);
	TEST_ASSERT(err == EC_ERROR_INVAL);

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_vboot);

	test_print_result();
}

/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_INCLUDE_PINWEAVER_H
#define __CROS_EC_INCLUDE_PINWEAVER_H

/* This is required before pinweaver_types.h to provide __packed and __aligned
 * while preserving the ability of pinweaver_types.h to be used in code outside
 * of src/platform/ec.
 */
#include <common.h>
#include <pinweaver_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PW_STORAGE_VERSION 0

#define BITS_PER_LEVEL_MIN 1
#define BITS_PER_LEVEL_MAX 5
#define HEIGHT_MIN 1
/* This will crash for logk == 0 so that condition must not be allowed when
 * using this.
 */
#define HEIGHT_MAX(logk) ((sizeof(struct label_t) * 8) / logk)

#define PW_LOG_ENTRY_COUNT 2

/* Persistent information used by this feature. */
struct merkle_tree_t {
	/* log2(Fan out). */
	struct bits_per_level_t bits_per_level;
	/* Height of the tree or param_l / bits_per_level. */
	struct height_t height;

	/* Root hash of the Merkle tree. */
	uint8_t root[PW_HASH_SIZE];

	/* Random bits used as part of the key derivation process. */
	uint8_t key_derivation_nonce[16];

	/* Key used to compute the HMACs of the metadata of the leaves. */
	uint8_t PW_ALIGN_TO_WRD hmac_key[32];

	/* Key used to encrypt and decrypt the metadata of the leaves. */
	uint8_t PW_ALIGN_TO_WRD wrap_key[32];
};

/* Long term flash storage for tree metadata. */
struct PW_PACKED pw_long_term_storage_t {
	uint16_t storage_version;

	/* log2(Fan out). */
	struct bits_per_level_t bits_per_level;
	/* Height of the tree or param_l / bits_per_level. */
	struct height_t height;

	/* Random bits used as part of the key derivation process. */
	uint8_t key_derivation_nonce[16];
};

struct PW_PACKED pw_log_storage_t {
	uint16_t storage_version;
	uint32_t restart_count;
	struct pw_get_log_entry_t entries[PW_LOG_ENTRY_COUNT];
};

/* Do not remove fields within the same PW_LEAF_MAJOR_VERSION. */
/* Encrypted part of the leaf data.
 */
struct PW_PACKED PW_ALIGN_TO_BLK leaf_sensitive_data_t {
	uint8_t low_entropy_secret[PW_SECRET_SIZE];
	uint8_t high_entropy_secret[PW_SECRET_SIZE];
	uint8_t reset_secret[PW_SECRET_SIZE];
};

/* Represents leaf data in a form that can be exported for storage. */
struct PW_PACKED wrapped_leaf_data_t {
	/* This is first so that head.leaf_version will be the first field
	 * in the struct to keep the meaning of the struct from becoming
	 * ambiguous across versions.
	 */
	struct leaf_header_t head;
	/* Covers .head, .pub, and .cipher_text. */
	uint8_t hmac[PW_HASH_SIZE];
	uint8_t iv[PW_WRAP_BLOCK_SIZE];
	struct leaf_public_data_t pub;
	uint8_t cipher_text[sizeof(struct leaf_sensitive_data_t)];
};

/* Represents encrypted leaf data after the lengths and version in the header
 * have been validated.
 */
struct imported_leaf_data_t {
	/* This is first so that head.leaf_version will be the first field
	 * in the struct to keep the meaning of the struct from becoming
	 * ambiguous across versions.
	 */
	const struct leaf_header_t *head;
	/* Covers .head, .pub, and .cipher_text. */
	const uint8_t *hmac;
	const uint8_t *iv;
	const struct leaf_public_data_t *pub;
	const uint8_t *cipher_text;
	const uint8_t (*hashes)[PW_HASH_SIZE];
};

/* The leaf data in a clear text working format. */
struct leaf_data_t {
	struct leaf_public_data_t pub;
	struct leaf_sensitive_data_t sec;
};

/* Key names for nvmem_vars */
#define PW_TREE_VAR "pwT0"
#define PW_LOG_VAR0 "pwL0"
/* The maximum key-value pair space allowed for the values of PinWeaver until
 * the Cr50 NVRAM implementation is updated to use a separate object per
 * key value pair.
 */
#define PW_MAX_VAR_USAGE 192

/* Initializes the PinWeaver feature.
 *
 * This needs to be called prior to handling any messages.
 */
void pinweaver_init(void);

/* Handler for incoming messages after they have been reconstructed.
 *
 * merkle_tree->root needs to be updated with new_root outside of this function.
 */
int pw_handle_request(struct merkle_tree_t *merkle_tree,
		      struct pw_request_t *request,
		      struct pw_response_t *response);

/******************************************************************************/
/* Struct helper functions.
 */

/* Sets up pointers to the relevant fields inside an wrapped leaf based on the
 * length fields in the header. These fields should be validated prior to
 * calling this function.
 */
void import_leaf(const struct unimported_leaf_data_t *unimported,
		 struct imported_leaf_data_t *imported);

/* Calculate how much is needed to add to the size of structs containing
 * an struct unimported_leaf_data_t because the variable length fields at the
 * end of the struct are not included by sizeof().
 */
#define PW_LEAF_PAYLOAD_SIZE (sizeof(struct wrapped_leaf_data_t) - \
		sizeof(struct unimported_leaf_data_t))


/******************************************************************************/
/* Utility functions exported for better test coverage.
 */

/* Computes the total number of the sibling hashes along a path. */
int get_path_auxiliary_hash_count(const struct merkle_tree_t *merkle_tree);

/* Computes the parent hash for an array of child hashes. */
void compute_hash(const uint8_t hashes[][PW_HASH_SIZE], uint16_t num_hashes,
		  struct index_t location,
		  const uint8_t child_hash[PW_HASH_SIZE],
		  uint8_t result[PW_HASH_SIZE]);

/* This should only be used in tests. */
void force_restart_count(uint32_t mock_value);

/* NV RAM log functions exported for use in test code. */
int store_log_data(const struct pw_log_storage_t *log);
int store_merkle_tree(const struct merkle_tree_t *merkle_tree);
int log_insert_leaf(struct label_t label, const uint8_t root[PW_HASH_SIZE],
		    const uint8_t hmac[PW_HASH_SIZE]);
int log_remove_leaf(struct label_t label, const uint8_t root[PW_HASH_SIZE]);
int log_auth(struct label_t label, const uint8_t root[PW_HASH_SIZE], int code,
	     struct pw_timestamp_t timestamp);

#ifdef __cplusplus
}
#endif

#endif  /* __CROS_EC_INCLUDE_PINWEAVER_H */

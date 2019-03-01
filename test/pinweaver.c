/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pinweaver.h>

#include <dcrypto.h>
#include <nvmem_vars.h>
#include <sha256.h>
#include <stdint.h>
#include <string.h>
#include <timer.h>
#include <util.h>
#include <pinweaver_types.h>

#include "test_util.h"

#include <stdlib.h>

struct pw_test_data_t {
	union {
		struct pw_request_t request;
		struct pw_response_t response;
		/* Reserve space for the variable length fields. */
		uint8_t tpm_buffer_size[PW_MAX_MESSAGE_SIZE];
	};
} PW_ALIGN_TO_WRD;

/******************************************************************************/
/* Test data
 */
const int EMPTY_TREE_PATH_LENGTH = 18;
const struct merkle_tree_t EMPTY_TREE = {
		{2} /* bits_per_level */,
		{6} /* height */,
		/* root */
		{0x81, 0xaa, 0xe9, 0xde, 0x93, 0xf4, 0xdf, 0x88,
		 0x18, 0xfa, 0xff, 0xbd, 0xb7, 0x09, 0xc0, 0x86,
		 0x48, 0xdd, 0xcd, 0x35, 0x00, 0xf2, 0x88, 0xd6,
		 0x3f, 0xa6, 0x5e, 0x80, 0x10, 0x19, 0x41, 0x17},
		 /* key derivation nonce. */
		{0x75, 0xf8, 0x43, 0xf7, 0x23, 0xbd, 0x2a, 0x0f,
		 0x8d, 0x34, 0xbf, 0xa6, 0x6d, 0xf9, 0x44, 0x38},
		/* hmac_key */
		{0x96, 0xc6, 0xb1, 0x64, 0xb6, 0xa7, 0xa8, 0x01,
		 0xd5, 0x1d, 0x8e, 0x97, 0x24, 0x86, 0xf8, 0x6f,
		 0xd4, 0x84, 0x0f, 0x95, 0x52, 0x93, 0x8d, 0x7d,
		 0x00, 0xbb, 0xba, 0xc8, 0xed, 0x7f, 0xa4, 0x7a},
		/* wrap_key */
		{0x95, 0xc9, 0x0a, 0xd4, 0xb3, 0x61, 0x1b, 0xcf,
		 0x1b, 0x49, 0x2b, 0xd6, 0x5d, 0xbc, 0x80, 0xa9,
		 0xf4, 0x83, 0xf2, 0x84, 0xd4, 0x04, 0x57, 0x7f,
		 0x02, 0xae, 0x37, 0x64, 0xae, 0xda, 0x71, 0x2a},
};

const struct leaf_data_t DEFAULT_LEAF = {
	/*pub*/
	{
		/* label = {0, 1, 2, 3, 0, 1} */
		{0x1b1llu},
		/* delay_schedule */
		{{{5}, {20} }, {{6}, {60} }, {{7}, {300} }, {{8}, {600} },
		 {{9}, {1800} }, {{10}, {3600} }, {{50}, {PW_BLOCK_ATTEMPTS} },
				{{0}, {0} },
		 {{0}, {0} }, {{0}, {0} }, {{0}, {0} }, {{0}, {0} },
		 {{0}, {0} }, {{0}, {0} }, {{0}, {0} }, {{0}, {0} }, },
		/*timestamp*/
		{0, 0},
		/* attempt_count */
		{0},
		/* valid_pcr_criteria */
		{{{0, 0}, {0} }, {{0, 0}, {0} } },
	},
	/*sec*/
	{
		/* low_entropy_secret */
		{0xba, 0xbc, 0x98, 0x9d, 0x97, 0x20, 0xcf, 0xea,
		 0xaa, 0xbd, 0xb2, 0xe3, 0xe0, 0x2c, 0x5c, 0x55,
		 0x06, 0x60, 0x93, 0xbd, 0x07, 0xe2, 0xba, 0x92,
		 0x10, 0x19, 0x24, 0xb1, 0x29, 0x33, 0x5a, 0xe2},
		/* high_entropy_secret */
		{0xe3, 0x46, 0xe3, 0x62, 0x01, 0x5d, 0xfe, 0x0a,
		 0xd3, 0x67, 0xd7, 0xef, 0xab, 0x01, 0xad, 0x0e,
		 0x3a, 0xed, 0xe8, 0x2f, 0x99, 0xd1, 0x2d, 0x13,
		 0x4d, 0x4e, 0xe4, 0x02, 0xbe, 0x71, 0x8e, 0x40},
		/* reset_secret */
		{0x8c, 0x33, 0x8c, 0xa7, 0x0f, 0x81, 0xa4, 0xee,
		 0x24, 0xcd, 0x04, 0x84, 0x9c, 0xa8, 0xfd, 0xdd,
		 0x14, 0xb0, 0xad, 0xe6, 0xb7, 0x6a, 0x10, 0xfc,
		 0x03, 0x22, 0xcb, 0x71, 0x31, 0xd3, 0x74, 0xd6},
	},
};

const struct leaf_header_t DEFAULT_HEAD = {
		{
				.minor = PW_LEAF_MINOR_VERSION,
				.major = PW_LEAF_MAJOR_VERSION,
		},
		sizeof(DEFAULT_LEAF.pub),
		sizeof(DEFAULT_LEAF.sec),
};

const uint8_t DEFAULT_IV[] = {
		0xaa, 0x65, 0x97, 0xc7, 0x02, 0x23, 0xb8, 0xdc,
		0xb3, 0x55, 0xca, 0x3a, 0xab, 0xd0, 0x03, 0x90,
};

const uint8_t EMPTY_HMAC[32] = {};

const uint32_t DEFAULT_STORAGE_SEED[8] = {
		0xe9e9880b, 0xb2a9fa0e, 0x9dcf22af, 0xc40156d0,
		0xca8535dc, 0x748606ee, 0x68f0f627, 0x7df7558a,
};

/* This is not the actual hmac. */
const uint8_t DEFAULT_HMAC[] = {
		0x87, 0x7e, 0xe2, 0xb2, 0x60, 0xeb, 0xf3, 0x4b,
		0x80, 0x3e, 0xca, 0xcb, 0xe6, 0x24, 0x21, 0x86,
		0xd9, 0xe3, 0x91, 0xf7, 0x2d, 0x16, 0x59, 0xd8,
		0x0f, 0x37, 0x0a, 0xf4, 0x64, 0x19, 0x44, 0xe7,
};

const uint8_t ROOT_WITH_DEFAULT_HMAC[] = {
		0x24, 0xad, 0xe4, 0xad, 0xf2, 0xdc, 0x40, 0x26,
		0x15, 0x03, 0x16, 0x6f, 0x3c, 0x32, 0x05, 0x99,
		0xf8, 0x25, 0x22, 0x92, 0xb9, 0xc7, 0xcd, 0x18,
		0x37, 0xc2, 0xf2, 0x72, 0x31, 0xdd, 0xc4, 0xaf,
};

/* This is not the actual hmac. */
const uint8_t OTHER_HMAC[] = {
		0xec, 0x64, 0x73, 0x39, 0xcf, 0x53, 0xb7, 0x08,
		0x85, 0x8f, 0xb6, 0x20, 0x25, 0x98, 0x59, 0x97,
		0x58, 0x8c, 0x7a, 0x80, 0x10, 0xb4, 0xc1, 0xc8,
		0x8a, 0xdf, 0xe3, 0x69, 0x07, 0xd1, 0xc4, 0xdc,
};

const uint8_t ROOT_WITH_OTHER_HMAC[] = {
		0xdf, 0xce, 0xf4, 0xba, 0x18, 0xe8, 0xd0, 0x1d,
		0xcb, 0x3b, 0x29, 0x41, 0x44, 0x01, 0x6e, 0x72,
		0xe3, 0x19, 0x9a, 0x44, 0x62, 0x44, 0x2a, 0xf1,
		0xaf, 0x66, 0xb6, 0xf0, 0x61, 0x05, 0x9d, 0xc0,
};

const uint8_t DEFAULT_PCR_DIGEST[] = {
		0xdf, 0xce, 0xf4, 0xba, 0x18, 0xe8, 0xd0, 0x1d,
		0xcb, 0x3b, 0x29, 0x41, 0x44, 0x01, 0x6e, 0x72,
		0xe3, 0x19, 0x9a, 0x44, 0x62, 0x44, 0x2a, 0xf1,
		0xaf, 0x66, 0xb6, 0xf0, 0x61, 0x05, 0x9d, 0xc0,
};

/******************************************************************************/
/* Config Variables and defines for Mocks.
 */

struct pw_long_term_storage_t MOCK_pw_long_term_storage;
struct pw_log_storage_t MOCK_pw_log_storage;
int MOCK_getvar_ret = EC_SUCCESS;
int MOCK_setvar_ret = EC_SUCCESS;

const uint8_t *MOCK_rand_bytes_src;
size_t MOCK_rand_bytes_offset;
size_t MOCK_rand_bytes_len;

void (*MOCK_hash_update_cb)(const void *data, size_t len);
static void auth_hash_update_cb(const void *data, size_t len);

const uint8_t *MOCK_hmac;
size_t MOCK_DECRYPTO_init_counter;
size_t MOCK_DECRYPTO_release_counter;

#define MOCK_AES_XOR_BYTE(b) ((uint8_t)(0x77 + (b & 15)))
int MOCK_aes_fail;
int MOCK_appkey_derive_fail;
enum dcrypto_appid MOCK_hwctx_appkey;

#define PW_VALID_PCR_CRITERIA_SIZE \
		(sizeof(struct valid_pcr_value_t) * PW_MAX_PCR_CRITERIA_COUNT)

/******************************************************************************/
/* Helper functions
 */

static void convert_response_to_new_version(uint8_t req_type,
						struct pw_response_t *response)
{
	if (response->header.version == 0) {
		if (req_type == PW_TRY_AUTH) {
			unsigned char *src = (unsigned char *)
				(&response->data.try_auth.reset_secret);
			memmove(src + PW_SECRET_SIZE, src,
				PW_LEAF_PAYLOAD_SIZE);
			memcpy(src, DEFAULT_LEAF.sec.reset_secret,
			       PW_SECRET_SIZE);
			response->header.data_length += PW_SECRET_SIZE;
		}
	}
}

static int do_request(struct merkle_tree_t *merkle_tree,
		      struct pw_test_data_t *buf)
{
	uint8_t req_type = buf->request.header.type.v;
	int ret = pw_handle_request(merkle_tree, &buf->request, &buf->response);
	size_t offset = buf->response.header.data_length +
			sizeof(buf->response.header);

	/* Zero out bytes that won't be sent for testing.*/
	memset(buf->tpm_buffer_size + offset, 0,
	       sizeof(buf->tpm_buffer_size) - offset);

	if (buf->request.header.version < PW_PROTOCOL_VERSION)
		convert_response_to_new_version(req_type, &buf->response);

	return ret;
}

static const char *pw_error_str(int code)
{
	switch (code) {
	case EC_SUCCESS:
		return "EC_SUCCESS";
	case EC_ERROR_UNKNOWN:
		return "EC_ERROR_UNKNOWN";
	case EC_ERROR_UNIMPLEMENTED:
		return "EC_ERROR_UNIMPLEMENTED";
	case PW_ERR_VERSION_MISMATCH:
		return "PW_ERR_VERSION_MISMATCH";
	case PW_ERR_LENGTH_INVALID:
		return "PW_ERR_LENGTH_INVALID";
	case PW_ERR_TYPE_INVALID:
		return "PW_ERR_TYPE_INVALID";
	case PW_ERR_BITS_PER_LEVEL_INVALID:
		return "PW_ERR_BITS_PER_LEVEL_INVALID";
	case PW_ERR_HEIGHT_INVALID:
		return "PW_ERR_HEIGHT_INVALID";
	case PW_ERR_LABEL_INVALID:
		return "PW_ERR_LABEL_INVALID";
	case PW_ERR_DELAY_SCHEDULE_INVALID:
		return "PW_ERR_DELAY_SCHEDULE_INVALID";
	case PW_ERR_PATH_AUTH_FAILED:
		return "PW_ERR_PATH_AUTH_FAILED";
	case PW_ERR_LEAF_VERSION_MISMATCH:
		return "PW_ERR_LEAF_VERSION_MISMATCH";
	case PW_ERR_HMAC_AUTH_FAILED:
		return "PW_ERR_HMAC_AUTH_FAILED";
	case PW_ERR_LOWENT_AUTH_FAILED:
		return "PW_ERR_LOWENT_AUTH_FAILED";
	case PW_ERR_RESET_AUTH_FAILED:
		return "PW_ERR_RESET_AUTH_FAILED";
	case PW_ERR_CRYPTO_FAILURE:
		return "PW_ERR_CRYPTO_FAILURE";
	case PW_ERR_RATE_LIMIT_REACHED:
		return "PW_ERR_RATE_LIMIT_REACHED";
	case PW_ERR_ROOT_NOT_FOUND:
		return "PW_ERR_ROOT_NOT_FOUND";
	case PW_ERR_NV_EMPTY:
		return "PW_ERR_NV_EMPTY";
	case PW_ERR_NV_LENGTH_MISMATCH:
		return "PW_ERR_NV_LENGTH_MISMATCH";
	case PW_ERR_NV_VERSION_MISMATCH:
		return "PW_ERR_NV_VERSION_MISMATCH";
	default:
		return "?";
	}
}

/* Pinweaver specific return code check. This prints the string representation
 * of the return code instead of just the number.
 */
#define TEST_RET_EQ(n, m) \
	do { \
		int val1 = n; \
		int val2 = m; \
		if (val1 != val2) { \
			ccprintf("%d: ASSERTION failed: %s (%d) != %s (%d)\n", \
				 __LINE__, pw_error_str(val1), val1, \
				 pw_error_str(val2), val2); \
			task_dump_trace(); \
			return EC_ERROR_UNKNOWN; \
		} \
	} while (0)

/* Allows mock functions when that don't return success / failure to have
 * assertions.
 */
#define TEST_ASRT_NORET(n) \
	do { \
		if (!(n)) { \
			int x = 0;\
			ccprintf("%d: ASSERTION failed: %s\n", __LINE__, #n); \
			task_dump_trace(); \
			x = 1 / x; \
		} \
	} while (0)

/* For debugging and generating test data. */
void print_array(const uint8_t *data, size_t n) __attribute__ ((unused));
void print_array(const uint8_t *data, size_t n)
{
	size_t x;

	if (n > 0) {
		ccprintf("uint8_t data[] = {");
		for (x = 0; x < n - 1; ++x) {
			if ((x & 7) != 7)
				ccprintf("0x%02x, ", data[x]);
			else
				ccprintf("0x%02x,\n", data[x]);
		}
		ccprintf("0x%02x};\n", data[x]);
	}
}

/* For exporting structs. This is useful for validating the results of crypto
 * operations.
 */
void print_hex(const uint8_t *data, size_t n) __attribute__ ((unused));
void print_hex(const uint8_t *data, size_t n)
{
	size_t x;

	for (x = 0; x < n; ++x)
		ccprintf("%02x ", data[x]);
}

/* Initialize the log.
 * For num_operations:
 * < 0   only zero out the storage.
 * == 0  only initialize the tree
 * > 0   cyclically applies operations in the following order:
 *           insert
 *           auth failed
 *           auth success
 *           remove
 * So for num_operations == 4 the complete set of operations will be written to
 * the log.
 */
static void setup_storage(int num_operations)
{
	MOCK_getvar_ret = EC_SUCCESS;
	MOCK_setvar_ret = EC_SUCCESS;

	memset(&MOCK_pw_long_term_storage, 0,
	       sizeof(MOCK_pw_long_term_storage));
	memset(&MOCK_pw_log_storage, 0, sizeof(MOCK_pw_log_storage));

	if (num_operations < 0)
		return;
	--num_operations;

	store_merkle_tree(&EMPTY_TREE);

	while (num_operations > 0) {
		--num_operations;

		log_insert_leaf(DEFAULT_LEAF.pub.label, ROOT_WITH_DEFAULT_HMAC,
				DEFAULT_HMAC);

		if (num_operations < 0)
			return;
		--num_operations;

		log_auth(DEFAULT_LEAF.pub.label, ROOT_WITH_OTHER_HMAC,
			 PW_ERR_LOWENT_AUTH_FAILED,
			 (struct pw_timestamp_t) {7, 99});

		if (num_operations < 0)
			return;
		--num_operations;

		log_auth(DEFAULT_LEAF.pub.label, ROOT_WITH_DEFAULT_HMAC,
			 EC_SUCCESS,
			 (struct pw_timestamp_t) {10, 100});

		if (num_operations < 0)
			return;
		--num_operations;

		log_remove_leaf(DEFAULT_LEAF.pub.label, EMPTY_TREE.root);
	}
}

static void setup_default_empty_path(uint8_t hashes[][PW_HASH_SIZE])
{
	uint8_t num_siblings = (1 << EMPTY_TREE.bits_per_level.v) - 1;
	const uint8_t level_hashes[5][PW_HASH_SIZE] = {
			/* Values for level 5 are all 0 for empty. */
			/* SHA256 for level 5, values for level 4. */
			{0x38, 0x72, 0x3a, 0x2e, 0x5e, 0x8a, 0x17, 0xaa,
			 0x79, 0x50, 0xdc, 0x00, 0x82, 0x09, 0x94, 0x4e,
			 0x89, 0x8f, 0x69, 0xa7, 0xbd, 0x10, 0xa2, 0x3c,
			 0x83, 0x9d, 0x34, 0x1e, 0x93, 0x5f, 0xd5, 0xca},
			/* SHA256 for level 4, values for level 3. */
			{0xfe, 0xc1, 0x2b, 0x09, 0x33, 0x31, 0x28, 0x34,
			 0x79, 0x1f, 0x07, 0x64, 0x1a, 0xed, 0x30, 0x53,
			 0x11, 0x1f, 0x15, 0x3e, 0x1e, 0x3e, 0xd1, 0xf0,
			 0xcd, 0x16, 0xcb, 0x39, 0x25, 0xfd, 0x5f, 0x84},
			/* SHA256 for level 3, values for level 2. */
			{0xb6, 0xd4, 0x9c, 0x89, 0x76, 0x45, 0x9c, 0xe9,
			 0x9c, 0x0b, 0xad, 0x5d, 0x71, 0xdf, 0x92, 0x77,
			 0xf6, 0x82, 0x62, 0x63, 0x81, 0x9f, 0xc9, 0x2f,
			 0x61, 0x9c, 0x29, 0x67, 0x52, 0x37, 0x01, 0x51},
			/* SHA256 for level 2, values for level 1. */
			{0x87, 0xeb, 0x61, 0x6b, 0x2c, 0x42, 0x07, 0x5e,
			 0x70, 0x2d, 0x48, 0x49, 0xf2, 0xe0, 0x13, 0x11,
			 0xc4, 0xe6, 0x98, 0xfa, 0x22, 0x7e, 0x65, 0xc6,
			 0x66, 0x33, 0x6b, 0xb6, 0xd7, 0xb9, 0x45, 0xfa},
			/* SHA256 for level 1, values for level 0. */
			{0x80, 0x91, 0x04, 0x3f, 0x6c, 0x29, 0x06, 0x35,
			 0x86, 0x99, 0x21, 0x88, 0x1f, 0xd9, 0xae, 0xb8,
			 0x35, 0x94, 0x26, 0x19, 0x64, 0x68, 0x4f, 0x4f,
			 0x4c, 0x66, 0x13, 0xa9, 0x66, 0x69, 0x25, 0x0e},};
	uint8_t hx;
	uint8_t kx;

	/* Empty first level. */
	memset(hashes, 0, num_siblings * PW_HASH_SIZE);
	hashes += num_siblings;

	for (hx = 1; hx < EMPTY_TREE.height.v; ++hx) {
		for (kx = 0; kx < num_siblings; ++kx) {
			memcpy(hashes, level_hashes[hx - 1], PW_HASH_SIZE);
			++hashes;
		}
	}
}

static void setup_default_unimported_leaf_data_and_hashes(
		const struct leaf_data_t *leaf_data,
		const uint8_t hmac[PW_HASH_SIZE],
		const struct leaf_header_t *header,
		struct unimported_leaf_data_t *data)
{
	memcpy(&data->head, header, sizeof(*header));
	memcpy(data->hmac, hmac, sizeof(data->hmac));
	memcpy(data->iv, DEFAULT_IV, sizeof(DEFAULT_IV));
	memcpy(data->payload, &leaf_data->pub, header->pub_len);
	DCRYPTO_aes_ctr(data->payload + header->pub_len,
			EMPTY_TREE.wrap_key, sizeof(EMPTY_TREE.wrap_key) * 8,
			DEFAULT_IV, (const uint8_t *)&leaf_data->sec,
			header->sec_len);
	setup_default_empty_path((void *)(data->payload + header->pub_len +
					  header->sec_len));
}

static void setup_reset_tree_defaults(struct merkle_tree_t *merkle_tree,
				      struct pw_request_t *request)
{
	MOCK_DECRYPTO_init_counter = 0;
	MOCK_DECRYPTO_release_counter = 0;

	memset(merkle_tree, 0, sizeof(*merkle_tree));
	memset(&MOCK_pw_long_term_storage, 0,
	       sizeof(MOCK_pw_long_term_storage));
	memset(&MOCK_pw_log_storage, 0, sizeof(MOCK_pw_log_storage));

	request->header.version = PW_PROTOCOL_VERSION;
	request->header.type.v = PW_RESET_TREE;
	request->header.data_length = sizeof(struct pw_request_reset_tree_t);

	request->data.reset_tree.bits_per_level.v = 2; /* k = 4 */
	request->data.reset_tree.height.v = 6; /* L = 12 */

	MOCK_rand_bytes_src = (uint8_t *)EMPTY_TREE.key_derivation_nonce;
	MOCK_rand_bytes_offset = 0;
	MOCK_rand_bytes_len = sizeof(EMPTY_TREE.key_derivation_nonce);
	MOCK_appkey_derive_fail = EC_SUCCESS;
	MOCK_setvar_ret = EC_SUCCESS;
}

static void setup_insert_leaf_defaults(struct merkle_tree_t *merkle_tree,
				       struct pw_request_t *request)
{
	MOCK_DECRYPTO_init_counter = 0;
	MOCK_DECRYPTO_release_counter = 0;

	memcpy(merkle_tree, &EMPTY_TREE, sizeof(EMPTY_TREE));
	memset(&MOCK_pw_log_storage, 0, sizeof(MOCK_pw_log_storage));

	request->header.version = PW_PROTOCOL_VERSION;
	request->header.type.v = PW_INSERT_LEAF;
	request->header.data_length = sizeof(struct pw_request_insert_leaf_t) +
			get_path_auxiliary_hash_count(&EMPTY_TREE) *
			PW_HASH_SIZE;

	request->data.insert_leaf.label.v = DEFAULT_LEAF.pub.label.v;
	memcpy(&request->data.insert_leaf.delay_schedule,
	       &DEFAULT_LEAF.pub.delay_schedule,
	       sizeof(DEFAULT_LEAF.pub.delay_schedule));
	memcpy(&request->data.insert_leaf.valid_pcr_criteria,
	       &DEFAULT_LEAF.pub.valid_pcr_criteria,
	       sizeof(DEFAULT_LEAF.pub.valid_pcr_criteria));
	memcpy(&request->data.insert_leaf.low_entropy_secret,
	       &DEFAULT_LEAF.sec.low_entropy_secret,
	       sizeof(DEFAULT_LEAF.sec.low_entropy_secret));
	memcpy(&request->data.insert_leaf.high_entropy_secret,
	       &DEFAULT_LEAF.sec.high_entropy_secret,
	       sizeof(DEFAULT_LEAF.sec.high_entropy_secret));
	memcpy(&request->data.insert_leaf.reset_secret,
	       &DEFAULT_LEAF.sec.reset_secret,
	       sizeof(DEFAULT_LEAF.sec.reset_secret));
	setup_default_empty_path(request->data.insert_leaf.path_hashes);

	MOCK_rand_bytes_src = DEFAULT_IV;
	MOCK_rand_bytes_offset = 0;
	MOCK_rand_bytes_len = sizeof(DEFAULT_IV);
	MOCK_hash_update_cb = 0;
	MOCK_hmac = DEFAULT_HMAC;
	MOCK_aes_fail = 0;
	MOCK_setvar_ret = EC_SUCCESS;
}

static void setup_remove_leaf_defaults(struct merkle_tree_t *merkle_tree,
				       struct pw_request_t *request)
{
	MOCK_DECRYPTO_init_counter = 0;
	MOCK_DECRYPTO_release_counter = 0;

	memcpy(merkle_tree, &EMPTY_TREE, sizeof(EMPTY_TREE));
	memcpy(merkle_tree->root, ROOT_WITH_DEFAULT_HMAC,
	       sizeof(ROOT_WITH_DEFAULT_HMAC));
	memset(&MOCK_pw_log_storage, 0, sizeof(MOCK_pw_log_storage));

	request->header.version = PW_PROTOCOL_VERSION;
	request->header.type.v = PW_REMOVE_LEAF;
	request->header.data_length =
			sizeof(struct pw_request_remove_leaf_t) +
			get_path_auxiliary_hash_count(&EMPTY_TREE) *
			PW_HASH_SIZE;

	request->data.remove_leaf.leaf_location = DEFAULT_LEAF.pub.label;
	memcpy(request->data.remove_leaf.leaf_hmac, DEFAULT_HMAC,
	       sizeof(request->data.remove_leaf.leaf_hmac));
	setup_default_empty_path(request->data.remove_leaf.path_hashes);

	MOCK_setvar_ret = EC_SUCCESS;
}

static void setup_try_auth_defaults_with_leaf(
		const struct leaf_data_t *leaf_data,
		uint8_t protocol_version,
		uint8_t minor_version,
		struct merkle_tree_t *merkle_tree,
		struct pw_request_t *request)
{
	struct leaf_header_t header = DEFAULT_HEAD;

	MOCK_DECRYPTO_init_counter = 0;
	MOCK_DECRYPTO_release_counter = 0;

	memcpy(merkle_tree, &EMPTY_TREE, sizeof(EMPTY_TREE));
	if (leaf_data->pub.attempt_count.v != 6 &&
	    leaf_data->pub.attempt_count.v != 10) {
		memcpy(merkle_tree->root, ROOT_WITH_DEFAULT_HMAC,
		       sizeof(ROOT_WITH_DEFAULT_HMAC));

		/* Gets overwritten by auth_hash_update_cb. */
		MOCK_hmac = DEFAULT_HMAC;
	} else
		/* Gets overwritten by auth_hash_update_cb. */
		MOCK_hmac = EMPTY_HMAC;

	header.leaf_version.minor = minor_version;
	memset(&MOCK_pw_log_storage, 0, sizeof(MOCK_pw_log_storage));

	request->header.version = protocol_version;
	request->header.type.v = PW_TRY_AUTH;
	request->header.data_length =
			sizeof(struct pw_request_try_auth_t) +
			PW_LEAF_PAYLOAD_SIZE +
			get_path_auxiliary_hash_count(&EMPTY_TREE) *
			PW_HASH_SIZE;

	if (minor_version == 0) {
		header.pub_len -= PW_VALID_PCR_CRITERIA_SIZE;
		request->header.data_length -= PW_VALID_PCR_CRITERIA_SIZE;
	}

	memcpy(request->data.try_auth.low_entropy_secret,
	       DEFAULT_LEAF.sec.low_entropy_secret,
	       sizeof(request->data.try_auth.low_entropy_secret));
	setup_default_unimported_leaf_data_and_hashes(
			leaf_data, MOCK_hmac, &header,
			&request->data.try_auth.unimported_leaf_data);

	force_restart_count(0);
	force_time((timestamp_t){.val = 0});
	MOCK_rand_bytes_src = DEFAULT_IV;
	MOCK_rand_bytes_offset = 0;
	MOCK_rand_bytes_len = sizeof(DEFAULT_IV);
	MOCK_hash_update_cb = auth_hash_update_cb;
	MOCK_aes_fail = 0;
	MOCK_setvar_ret = EC_SUCCESS;
}

static void setup_try_auth_defaults(struct merkle_tree_t *merkle_tree,
				    struct pw_request_t *request)
{
	setup_try_auth_defaults_with_leaf(&DEFAULT_LEAF, PW_PROTOCOL_VERSION,
					  PW_LEAF_MINOR_VERSION, merkle_tree,
					  request);
}

static void setup_reset_auth_defaults(struct merkle_tree_t *merkle_tree,
				      struct pw_request_t *request)
{
	struct leaf_public_data_t *pub =
			(void *)request->data.reset_auth.unimported_leaf_data
					.payload;

	MOCK_DECRYPTO_init_counter = 0;
	MOCK_DECRYPTO_release_counter = 0;
	memcpy(merkle_tree, &EMPTY_TREE, sizeof(EMPTY_TREE));
	memset(&MOCK_pw_log_storage, 0, sizeof(MOCK_pw_log_storage));

	request->header.version = PW_PROTOCOL_VERSION;
	request->header.type.v = PW_RESET_AUTH;
	request->header.data_length =
			sizeof(struct pw_request_reset_auth_t) +
			PW_LEAF_PAYLOAD_SIZE +
			get_path_auxiliary_hash_count(&EMPTY_TREE) *
			PW_HASH_SIZE;

	memcpy(request->data.reset_auth.reset_secret,
	       DEFAULT_LEAF.sec.reset_secret,
	       sizeof(request->data.reset_auth.reset_secret));

	setup_default_unimported_leaf_data_and_hashes(
			&DEFAULT_LEAF, EMPTY_HMAC, &DEFAULT_HEAD,
			&request->data.try_auth.unimported_leaf_data);
	pub->attempt_count.v = 6;

	MOCK_rand_bytes_src = DEFAULT_IV;
	MOCK_rand_bytes_offset = 0;
	MOCK_rand_bytes_len = sizeof(DEFAULT_IV);
	MOCK_hash_update_cb = auth_hash_update_cb;
	MOCK_hmac = EMPTY_HMAC; /* Gets overwritten by auth_hash_update_cb. */
	MOCK_aes_fail = 0;
	MOCK_setvar_ret = EC_SUCCESS;
}

static void setup_get_log_defaults(struct merkle_tree_t *merkle_tree,
				   struct pw_request_t *request)
{
	MOCK_DECRYPTO_init_counter = 0;
	MOCK_DECRYPTO_release_counter = 0;

	memcpy(merkle_tree, &EMPTY_TREE, sizeof(*merkle_tree));

	request->header.version = PW_PROTOCOL_VERSION;
	request->header.type.v = PW_GET_LOG;
	request->header.data_length = sizeof(struct pw_request_get_log_t);

	/* Chosen not to match any of the root hashes in the log. */
	memcpy(request->data.get_log.root, OTHER_HMAC,
	       sizeof(OTHER_HMAC));

	setup_storage(1);
}

static void setup_log_replay_defaults_with_leaf(
		const struct leaf_data_t *leaf_data,
		struct merkle_tree_t *merkle_tree,
		struct pw_request_t *request)
{
	MOCK_DECRYPTO_init_counter = 0;
	MOCK_DECRYPTO_release_counter = 0;

	memcpy(merkle_tree, &EMPTY_TREE, sizeof(*merkle_tree));
	if (leaf_data->pub.attempt_count.v != 6 &&
	    leaf_data->pub.attempt_count.v != 10)
		/* Gets overwritten by auth_hash_update_cb. */
		MOCK_hmac = DEFAULT_HMAC;
	else
		/* Gets overwritten by auth_hash_update_cb. */
		MOCK_hmac = EMPTY_HMAC;

	request->header.version = PW_PROTOCOL_VERSION;
	request->header.type.v = PW_LOG_REPLAY;
	request->header.data_length =
			sizeof(struct pw_request_log_replay_t) +
			PW_LEAF_PAYLOAD_SIZE +
			get_path_auxiliary_hash_count(&EMPTY_TREE) *
			PW_HASH_SIZE;

	memcpy(request->data.log_replay.log_root, ROOT_WITH_DEFAULT_HMAC,
	       sizeof(ROOT_WITH_DEFAULT_HMAC));

	setup_default_unimported_leaf_data_and_hashes(
			leaf_data, MOCK_hmac, &DEFAULT_HEAD,
			&request->data.try_auth.unimported_leaf_data);

	MOCK_hash_update_cb = auth_hash_update_cb;

	setup_storage(4);
}

static void setup_log_replay_defaults(struct merkle_tree_t *merkle_tree,
				      struct pw_request_t *request)
{
	setup_log_replay_defaults_with_leaf(&DEFAULT_LEAF, merkle_tree,
					    request);
}

/* Increases the length of the pub and cipher_text by 4 each. */
static void setup_mock_future_version(
		struct unimported_leaf_data_t *unimported_leaf_data,
		uint16_t *req_length)
{
	uint8_t *start = unimported_leaf_data->payload;
	const uint8_t size_increase = 4;
	const uint16_t cipher_text_offset = unimported_leaf_data->head.pub_len;
	const uint16_t hashes_offset = cipher_text_offset +
				       unimported_leaf_data->head.sec_len;

	/* Shift hashes by 8*/
	memmove(start + hashes_offset + size_increase * 2,
		start + hashes_offset,
		get_path_auxiliary_hash_count(&EMPTY_TREE) *
		PW_HASH_SIZE);

	/* Shift cipher_text by 4*/
	memmove(start + cipher_text_offset + size_increase,
		start + cipher_text_offset,
		unimported_leaf_data->head.sec_len);

	++unimported_leaf_data->head.leaf_version.minor;
	unimported_leaf_data->head.pub_len += size_increase;
	unimported_leaf_data->head.sec_len += size_increase;
	*req_length += size_increase * 2;
}

static int test_handle_short_msg(struct merkle_tree_t *merkle_tree,
				 struct pw_test_data_t *buf,
				 const uint8_t root[PW_HASH_SIZE])
{
	int ret = do_request(merkle_tree, buf);

	TEST_RET_EQ(buf->response.header.result_code, ret);
	TEST_ASSERT(buf->response.header.version == PW_PROTOCOL_VERSION);
	TEST_ASSERT(buf->response.header.data_length == 0);
	TEST_ASSERT_ARRAY_EQ(buf->response.header.root, root, PW_HASH_SIZE);
	TEST_ASSERT_ARRAY_EQ(buf->response.header.root, merkle_tree->root,
			     PW_HASH_SIZE);
	return ret;
}

/* Changes MOCK_hmac in a deterministic way based on the contents of the data
 * with the goal of making it easier to catch bugs in the handling of try_auth
 * and reset_auth requests.
 */
static void auth_hash_update_cb(const void *data, size_t len)
{
	const struct leaf_data_t *leaf_data = data;

	if (len != sizeof(leaf_data->pub) && len != sizeof(leaf_data->pub) + 4)
		return;

	switch (leaf_data->pub.attempt_count.v) {
	case 10:
	case 6:
		MOCK_hmac = EMPTY_HMAC;
		break;
	case 16:
		MOCK_hmac = OTHER_HMAC;
		break;
	default:
		MOCK_hmac = DEFAULT_HMAC;
		break;
	}
}

/******************************************************************************/
/* Mock implementations of TPM functionality.
 */

void get_storage_seed(void *buf, size_t *len)
{
	*len = *len < sizeof(DEFAULT_STORAGE_SEED) ? *len :
	       sizeof(DEFAULT_STORAGE_SEED);
	memcpy(buf, DEFAULT_STORAGE_SEED, *len);
}

uint8_t get_current_pcr_digest(const uint8_t bitmask[2],
			       uint8_t sha256_of_selected_pcr[32])
{
	memcpy(sha256_of_selected_pcr, DEFAULT_PCR_DIGEST, 32);
	return 0;
}

/******************************************************************************/
/* Mock implementations of nvmem_vars functionality.
 */
const struct tuple *getvar(const uint8_t *key, uint8_t key_len)
{
	struct tuple *var = NULL;
	size_t i;

	const struct {
		size_t key_len;
		const void *key;
		size_t val_size;
		const void *val;
	} vars[] = {
		{sizeof(PW_TREE_VAR) - 1, PW_TREE_VAR,
		 sizeof(MOCK_pw_long_term_storage), &MOCK_pw_long_term_storage},
		{sizeof(PW_LOG_VAR0) - 1, PW_LOG_VAR0,
		 sizeof(MOCK_pw_log_storage), &MOCK_pw_log_storage},
	};

	if (!key || !key_len)
		return NULL;

	if (MOCK_getvar_ret != EC_SUCCESS)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(vars); i++) {
		if ((key_len != vars[i].key_len) ||
		    memcmp(key, vars[i].key, key_len)) {
			continue;
		}
		var = malloc(sizeof(struct tuple) + key_len + vars[i].val_size);
		var->flags = 0;
		var->val_len = vars[i].val_size;
		memcpy(var->data_ + var->key_len, vars[i].val, var->val_len);
		break;
	}

	return var;
}

void freevar(const struct tuple *var)
{
	if (!var)
		return;

	/* This typecast is OK because we know that 'var' came from malloc. */
	free((void *)var);
}
const uint8_t *tuple_val(const struct tuple *tpl)
{
	return tpl->data_ + tpl->key_len;
}

int setvar(const uint8_t *key, uint8_t key_len, const uint8_t *val,
	   uint8_t val_len)
{
	if (MOCK_setvar_ret != EC_SUCCESS)
		return MOCK_setvar_ret;

	if (key_len == (sizeof(PW_TREE_VAR) - 1) &&
	    memcmp(key, PW_TREE_VAR, (sizeof(PW_TREE_VAR) - 1)) == 0) {
		TEST_ASSERT(val_len == sizeof(MOCK_pw_long_term_storage));
		memcpy(&MOCK_pw_long_term_storage, val, val_len);
		return EC_SUCCESS;
	} else if (key_len == (sizeof(PW_LOG_VAR0) - 1) &&
		   memcmp(key, PW_LOG_VAR0, (sizeof(PW_LOG_VAR0) - 1)) == 0) {
		TEST_ASSERT(val_len == sizeof(struct pw_log_storage_t));
		memcpy(&MOCK_pw_log_storage, val, val_len);
		return EC_SUCCESS;
	} else
		return EC_ERROR_UNKNOWN;
}

/******************************************************************************/
/* Mock implementations of TRNG functionality.
 */

void rand_bytes(void *buffer, size_t len)
{
	if (!MOCK_rand_bytes_src)
		return;

	TEST_ASRT_NORET(len <= MOCK_rand_bytes_len - MOCK_rand_bytes_offset);

	memcpy(buffer, MOCK_rand_bytes_src + MOCK_rand_bytes_offset, len);
	MOCK_rand_bytes_offset += len;
	if (MOCK_rand_bytes_len == MOCK_rand_bytes_offset)
		MOCK_rand_bytes_offset = 0;
}

/******************************************************************************/
/* Mock implementations of Dcrypto functionality.
 */

void HASH_update(struct HASH_CTX *ctx, const void *data, size_t len)
{
	if (MOCK_hash_update_cb)
		MOCK_hash_update_cb(data, len);
	if (ctx)
		SHA256_update(ctx, data, len);
}

uint8_t *HASH_final(struct HASH_CTX *ctx)
{
	++MOCK_DECRYPTO_release_counter;
	return SHA256_final(ctx);
}

void DCRYPTO_SHA256_init(LITE_SHA256_CTX *ctx, uint32_t sw_required)
{
	SHA256_init(ctx);
	++MOCK_DECRYPTO_init_counter;
}

void DCRYPTO_HMAC_SHA256_init(LITE_HMAC_CTX *ctx, const void *key,
			      unsigned int len)
{
	TEST_ASRT_NORET(len == sizeof(EMPTY_TREE.hmac_key));
	TEST_ASRT_NORET(memcmp(key, EMPTY_TREE.hmac_key,
			       sizeof(EMPTY_TREE.hmac_key)) == 0);
	SHA256_init(&ctx->hash);
	++MOCK_DECRYPTO_init_counter;
}

const uint8_t *DCRYPTO_HMAC_final(LITE_HMAC_CTX *ctx)
{
	++MOCK_DECRYPTO_release_counter;
	return MOCK_hmac;
}

/* Perform a symmetric transformation of the data to simulate AES without
 * requiring a full AES-CTR implementation.
 *
 * 1 for success 0 for fail
 */
int DCRYPTO_aes_ctr(uint8_t *out, const uint8_t *key, uint32_t key_bits,
		    const uint8_t *iv, const uint8_t *in, size_t in_len)
{
	size_t x;

	if (MOCK_aes_fail) {
		--MOCK_aes_fail;
		return 0;
	}

	TEST_ASSERT(key_bits == 256);
	TEST_ASSERT_ARRAY_EQ(key, EMPTY_TREE.wrap_key,
			     sizeof(EMPTY_TREE.wrap_key));
	TEST_ASSERT_ARRAY_EQ(iv, DEFAULT_IV, sizeof(DEFAULT_IV));
	TEST_ASSERT(in_len == sizeof(struct leaf_sensitive_data_t));

	for (x = 0; x < in_len; ++x)
		out[x] = MOCK_AES_XOR_BYTE(x) ^ in[x];
	return 1;
}

/* 1 for success 0 for fail*/
int DCRYPTO_appkey_init(enum dcrypto_appid appid, struct APPKEY_CTX *ctx)
{
	MOCK_hwctx_appkey = appid;
	return 1;
}

void DCRYPTO_appkey_finish(struct APPKEY_CTX *ctx)
{
	MOCK_hwctx_appkey = 0;
}

/* 1 for success 0 for fail*/
int DCRYPTO_appkey_derive(enum dcrypto_appid appid, const uint32_t input[8],
			  uint32_t output[8])
{
	TEST_ASSERT(appid == PINWEAVER);
	TEST_ASSERT(MOCK_hwctx_appkey == appid);

	if (MOCK_appkey_derive_fail != EC_SUCCESS)
		return 0;

	if (input[6] ^ DEFAULT_STORAGE_SEED[6])
		memcpy(output, EMPTY_TREE.hmac_key,
		       sizeof(EMPTY_TREE.hmac_key));
	else
		memcpy(output, EMPTY_TREE.wrap_key,
		       sizeof(EMPTY_TREE.wrap_key));
	return 1;
}

/******************************************************************************/
/* Reusable test cases.
 */

static int check_dcrypto_mutex_usage(void)
{
	if (MOCK_DECRYPTO_init_counter == MOCK_DECRYPTO_release_counter)
		return EC_SUCCESS;
	ccprintf("ASSERTION failed: DCRYPTO init(%d) != DCRYPTO release(%d)\n",
		 MOCK_DECRYPTO_init_counter, MOCK_DECRYPTO_release_counter);
	return EC_ERROR_UNKNOWN;
}

static int invalid_length_with_leaf_head(
		size_t head_offset,
		void (*defaults)(struct merkle_tree_t *, struct pw_request_t *))
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct leaf_header_t *req_head = (void *)&buf + head_offset;
	uint8_t old_root[PW_HASH_SIZE];

	defaults(&merkle_tree, &buf.request);
	memcpy(old_root, merkle_tree.root, sizeof(old_root));

	buf.request.header.data_length = 0;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, old_root),
		    PW_ERR_LENGTH_INVALID);

	defaults(&merkle_tree, &buf.request);

	++buf.request.header.data_length;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, old_root),
		    PW_ERR_LENGTH_INVALID);

	defaults(&merkle_tree, &buf.request);

	++req_head->pub_len;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, old_root),
		    PW_ERR_LENGTH_INVALID);

	defaults(&merkle_tree, &buf.request);

	++req_head->leaf_version.minor;
	--req_head->pub_len;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, old_root),
		    PW_ERR_LENGTH_INVALID);
	return check_dcrypto_mutex_usage();

}

/******************************************************************************/
/* Basic operation test cases.
 */

static int get_path_auxiliary_hash_count_test(void)
{
	struct merkle_tree_t merkle_tree;

	memcpy(&merkle_tree, &EMPTY_TREE, sizeof(merkle_tree));

	TEST_ASSERT(get_path_auxiliary_hash_count(&merkle_tree) ==
				    EMPTY_TREE_PATH_LENGTH);
	return EC_SUCCESS;
}

static int compute_hash_test(void)
{
	const uint8_t hashes[4][PW_HASH_SIZE] = {
		{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	};
	const struct {
		struct index_t index;
		uint8_t result[PW_HASH_SIZE];
	} test_cases[] = {
		{{0},
		 {0xd5, 0xd9, 0x25, 0xb6, 0xa9, 0x90, 0x24, 0x12,
		  0x39, 0x0e, 0xfa, 0xd4, 0x8d, 0x55, 0x45, 0xf3,
		  0x23, 0x6c, 0x6d, 0xff, 0xcc, 0xc8, 0xe1, 0x39,
		  0xc7, 0xc3, 0x25, 0xf0, 0xd2, 0xa8, 0xf2, 0x0c}
		},
		{{1},
		 {0x64, 0x3e, 0x56, 0xbc, 0xb9, 0xda, 0x18, 0xaf,
		  0xa0, 0x8c, 0x1f, 0xf8, 0x5e, 0xba, 0x58, 0xd0,
		  0xe1, 0x99, 0x61, 0xe0, 0xe2, 0x12, 0xe9, 0x14,
		  0xb5, 0x33, 0x46, 0x35, 0x52, 0x1e, 0xaf, 0x91}
		},
		{{3},
		 {0xd0, 0x90, 0xc7, 0x3d, 0x12, 0xfb, 0xbc, 0xbc,
		  0x78, 0xcc, 0xbe, 0x58, 0x21, 0x14, 0xcf, 0x38,
		  0x68, 0x49, 0x20, 0xe9, 0x61, 0xcb, 0x35, 0xc4,
		  0x95, 0xb0, 0x14, 0x5a, 0x35, 0x43, 0x3e, 0x73}
		},
	};
	uint8_t result[PW_HASH_SIZE];
	size_t x;

	for (x = 0; x < ARRAY_SIZE(test_cases); ++x) {
		compute_hash(hashes, 3, test_cases[x].index, hashes[3], result);
		TEST_ASSERT_ARRAY_EQ(result, test_cases[x].result,
				     sizeof(result));
	}

	return EC_SUCCESS;
}

/******************************************************************************/
/* Header validation test cases.
 */

static int handle_request_version_mismatch(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_reset_tree_defaults(&merkle_tree, &buf.request);

	buf.request.header.version = PW_PROTOCOL_VERSION + 1;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_HMAC),
		    PW_ERR_VERSION_MISMATCH);
	return EC_SUCCESS;
}

static int handle_request_invalid_type(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	memcpy(&merkle_tree, &EMPTY_TREE, sizeof(merkle_tree));
	memset(&buf.response, 0x77, sizeof(buf.response));

	buf.request.header.version = PW_PROTOCOL_VERSION;
	buf.request.header.type.v = PW_MT_INVALID;
	buf.request.header.data_length = 0;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_TYPE_INVALID);
	return EC_SUCCESS;
}

/******************************************************************************/
/* Reset Tree test cases.
 */

static int handle_reset_tree_invalid_length(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_reset_tree_defaults(&merkle_tree, &buf.request);

	++buf.request.header.data_length;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_HMAC),
		    PW_ERR_LENGTH_INVALID);
	return check_dcrypto_mutex_usage();
}

static int handle_reset_tree_bits_per_level_invalid(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_reset_tree_defaults(&merkle_tree, &buf.request);

	/* Test lower bound. */
	buf.request.data.reset_tree.bits_per_level.v = BITS_PER_LEVEL_MIN - 1;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, merkle_tree.root),
		    PW_ERR_BITS_PER_LEVEL_INVALID);

	setup_reset_tree_defaults(&merkle_tree, &buf.request);

	/* Test upper bound. */
	buf.request.data.reset_tree.bits_per_level.v = BITS_PER_LEVEL_MAX + 1;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_HMAC),
		    PW_ERR_BITS_PER_LEVEL_INVALID);
	return check_dcrypto_mutex_usage();
}

static int handle_reset_tree_height_invalid(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_reset_tree_defaults(&merkle_tree, &buf.request);

	/* Test lower bound. */
	buf.request.data.reset_tree.height.v = HEIGHT_MIN - 1;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, merkle_tree.root),
		    PW_ERR_HEIGHT_INVALID);

	setup_reset_tree_defaults(&merkle_tree, &buf.request);

	/* Test upper bound. */
	buf.request.data.reset_tree.height.v =
			HEIGHT_MAX(buf.request.data.reset_tree
						   .bits_per_level.v) + 1;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_HMAC),
		    PW_ERR_HEIGHT_INVALID);
	return check_dcrypto_mutex_usage();
}

static int handle_reset_tree_crypto_failure(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_reset_tree_defaults(&merkle_tree, &buf.request);

	/* Test lower bound. */
	MOCK_appkey_derive_fail = PW_ERR_CRYPTO_FAILURE;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_HMAC),
		    PW_ERR_CRYPTO_FAILURE);
	return check_dcrypto_mutex_usage();
}

static int handle_reset_tree_nv_fail(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_reset_tree_defaults(&merkle_tree, &buf.request);

	MOCK_setvar_ret = PW_ERR_NV_LENGTH_MISMATCH;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, merkle_tree.root),
		    PW_ERR_NV_LENGTH_MISMATCH);
	return EC_SUCCESS;
}

static int handle_reset_tree_success(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_reset_tree_defaults(&merkle_tree, &buf.request);

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    EC_SUCCESS);

	TEST_ASSERT_ARRAY_EQ((uint8_t *)&merkle_tree, (uint8_t *)&EMPTY_TREE,
			     sizeof(EMPTY_TREE));

	TEST_ASSERT(MOCK_pw_long_term_storage.storage_version ==
		    PW_STORAGE_VERSION);
	TEST_ASSERT(MOCK_pw_long_term_storage.bits_per_level.v ==
		    EMPTY_TREE.bits_per_level.v);
	TEST_ASSERT(MOCK_pw_long_term_storage.height.v ==
		    EMPTY_TREE.height.v);
	TEST_ASSERT_ARRAY_EQ(MOCK_pw_long_term_storage.key_derivation_nonce,
			     EMPTY_TREE.key_derivation_nonce,
			     sizeof(EMPTY_TREE.key_derivation_nonce));

	TEST_ASSERT(MOCK_pw_log_storage.storage_version == PW_STORAGE_VERSION);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].type.v == PW_RESET_TREE);
	TEST_ASSERT_ARRAY_EQ(MOCK_pw_log_storage.entries[0].root,
			     EMPTY_TREE.root, sizeof(EMPTY_TREE.root));

	return check_dcrypto_mutex_usage();
}

/******************************************************************************/
/* Insert leaf test cases.
 */

static int handle_insert_leaf_invalid_length(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_insert_leaf_defaults(&merkle_tree, &buf.request);

	++buf.request.header.data_length;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_LENGTH_INVALID);
	return check_dcrypto_mutex_usage();
}

static int handle_insert_leaf_label_invalid(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_insert_leaf_defaults(&merkle_tree, &buf.request);

	buf.request.data.insert_leaf.label.v |= 0x030000;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_LABEL_INVALID);
	return check_dcrypto_mutex_usage();
}

static int handle_insert_leaf_delay_schedule_invalid(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct delay_schedule_entry_t (*ds)[PW_SCHED_COUNT] =
			&buf.request.data.insert_leaf.delay_schedule;

	setup_insert_leaf_defaults(&merkle_tree, &buf.request);

	/* Non-increasing attempt_count. */
	(*ds)[1].attempt_count.v = 0;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_DELAY_SCHEDULE_INVALID);

	setup_insert_leaf_defaults(&merkle_tree, &buf.request);

	/* Non-increasing time_diff. */
	(*ds)[1].time_diff.v = 0;
	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_DELAY_SCHEDULE_INVALID);

	setup_insert_leaf_defaults(&merkle_tree, &buf.request);

	/* attempt_count noise. */
	(*ds)[14].attempt_count.v = 99;
	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_DELAY_SCHEDULE_INVALID);

	setup_insert_leaf_defaults(&merkle_tree, &buf.request);

	/* time_diff noise. */
	(*ds)[14].time_diff.v = 99;
	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_DELAY_SCHEDULE_INVALID);

	setup_insert_leaf_defaults(&merkle_tree, &buf.request);

	/* Empty delay_schedule. */
	memset(&(*ds)[0], 0, sizeof(*ds));
	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_DELAY_SCHEDULE_INVALID);
	return check_dcrypto_mutex_usage();
}

static int handle_insert_leaf_path_auth_failed(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_insert_leaf_defaults(&merkle_tree, &buf.request);

	buf.request.data.insert_leaf.path_hashes[0][0] ^= 0xff;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_PATH_AUTH_FAILED);
	return check_dcrypto_mutex_usage();
}

static int handle_insert_leaf_crypto_failure(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_insert_leaf_defaults(&merkle_tree, &buf.request);

	MOCK_aes_fail = 1;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_CRYPTO_FAILURE);
	return check_dcrypto_mutex_usage();
}

static int handle_insert_leaf_nv_fail(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_insert_leaf_defaults(&merkle_tree, &buf.request);

	MOCK_setvar_ret = PW_ERR_NV_LENGTH_MISMATCH;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, merkle_tree.root),
		    PW_ERR_NV_LENGTH_MISMATCH);
	return EC_SUCCESS;
}

static int handle_insert_leaf_success(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	size_t x;
	const uint8_t *plain_text = (const uint8_t *)&DEFAULT_LEAF.sec;
	struct wrapped_leaf_data_t *wrapped_leaf_data =
			(void *)&buf.response.data.insert_leaf
					.unimported_leaf_data;

	setup_insert_leaf_defaults(&merkle_tree, &buf.request);

	TEST_RET_EQ(do_request(&merkle_tree, &buf), EC_SUCCESS);

	TEST_ASSERT(buf.response.header.version == PW_PROTOCOL_VERSION);
	TEST_ASSERT(buf.response.header.data_length ==
				    sizeof(buf.response.data.insert_leaf) +
				    PW_LEAF_PAYLOAD_SIZE);
	TEST_RET_EQ(buf.response.header.result_code, EC_SUCCESS);

	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, ROOT_WITH_DEFAULT_HMAC,
			     sizeof(ROOT_WITH_DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, merkle_tree.root,
			     PW_HASH_SIZE);
	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.insert_leaf.unimported_leaf_data.hmac,
			DEFAULT_HMAC, sizeof(DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(
			(uint8_t *)&wrapped_leaf_data->pub,
			(uint8_t *)&DEFAULT_LEAF.pub, sizeof(DEFAULT_LEAF.pub));
	for (x = 0; x < sizeof(DEFAULT_LEAF.sec); ++x)
		TEST_ASSERT(plain_text[x] ==
			    (wrapped_leaf_data->cipher_text[x] ^
			     MOCK_AES_XOR_BYTE(x)));

	TEST_ASSERT(MOCK_pw_log_storage.entries[0].type.v == PW_INSERT_LEAF);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].label.v ==
		    DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT_ARRAY_EQ(MOCK_pw_log_storage.entries[0].root,
			     ROOT_WITH_DEFAULT_HMAC,
			     sizeof(ROOT_WITH_DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(MOCK_pw_log_storage.entries[0].leaf_hmac,
			     DEFAULT_HMAC,
			     sizeof(DEFAULT_HMAC));

	return check_dcrypto_mutex_usage();
}

static int handle_insert_leaf_old_protocol_success(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	size_t x;
	int hash_count;
	unsigned char *src;
	const uint8_t *plain_text = (const uint8_t *)&DEFAULT_LEAF.sec;
	struct wrapped_leaf_data_t *wrapped_leaf_data =
			(void *)&buf.response.data.insert_leaf
					.unimported_leaf_data;

	setup_insert_leaf_defaults(&merkle_tree, &buf.request);

	// Make changes to simulate the protocol 0 request.
	buf.request.header.version = 0;
	hash_count =
		get_path_auxiliary_hash_count(&merkle_tree);
	src = (unsigned char *)
		(&buf.request.data.insert_leaf.valid_pcr_criteria);
	memmove(src, src + PW_VALID_PCR_CRITERIA_SIZE,
		hash_count * PW_HASH_SIZE);
	buf.request.header.data_length -= PW_VALID_PCR_CRITERIA_SIZE;

	TEST_RET_EQ(do_request(&merkle_tree, &buf), EC_SUCCESS);

	TEST_ASSERT(buf.response.header.version == 0);
	TEST_ASSERT(buf.response.header.data_length ==
				    sizeof(buf.response.data.insert_leaf) +
				    PW_LEAF_PAYLOAD_SIZE);
	TEST_RET_EQ(buf.response.header.result_code, EC_SUCCESS);

	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, ROOT_WITH_DEFAULT_HMAC,
			     sizeof(ROOT_WITH_DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, merkle_tree.root,
			     PW_HASH_SIZE);
	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.insert_leaf.unimported_leaf_data.hmac,
			DEFAULT_HMAC, sizeof(DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(
			(uint8_t *)&wrapped_leaf_data->pub,
			(uint8_t *)&DEFAULT_LEAF.pub, sizeof(DEFAULT_LEAF.pub));
	for (x = 0; x < sizeof(DEFAULT_LEAF.sec); ++x)
		TEST_ASSERT(plain_text[x] ==
			    (wrapped_leaf_data->cipher_text[x] ^
			     MOCK_AES_XOR_BYTE(x)));

	TEST_ASSERT(MOCK_pw_log_storage.entries[0].type.v == PW_INSERT_LEAF);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].label.v ==
		    DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT_ARRAY_EQ(MOCK_pw_log_storage.entries[0].root,
			     ROOT_WITH_DEFAULT_HMAC,
			     sizeof(ROOT_WITH_DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(MOCK_pw_log_storage.entries[0].leaf_hmac,
			     DEFAULT_HMAC,
			     sizeof(DEFAULT_HMAC));

	return check_dcrypto_mutex_usage();
}

/******************************************************************************/
/* Remove leaf test cases.
 */

static int handle_remove_leaf_invalid_length(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_remove_leaf_defaults(&merkle_tree, &buf.request);

	++buf.request.header.data_length;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf,
					  ROOT_WITH_DEFAULT_HMAC),
		    PW_ERR_LENGTH_INVALID);
	return check_dcrypto_mutex_usage();
}

static int handle_remove_leaf_label_invalid(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_remove_leaf_defaults(&merkle_tree, &buf.request);

	buf.request.data.remove_leaf.leaf_location.v |= 0x030000;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf,
					  ROOT_WITH_DEFAULT_HMAC),
		    PW_ERR_LABEL_INVALID);
	return check_dcrypto_mutex_usage();
}

static int handle_remove_leaf_path_auth_failed(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_remove_leaf_defaults(&merkle_tree, &buf.request);

	buf.request.data.remove_leaf.path_hashes[0][0] ^= 0xff;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf,
					  ROOT_WITH_DEFAULT_HMAC),
		    PW_ERR_PATH_AUTH_FAILED);
	return check_dcrypto_mutex_usage();
}

static int handle_remove_leaf_nv_fail(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_remove_leaf_defaults(&merkle_tree, &buf.request);

	MOCK_setvar_ret = PW_ERR_NV_LENGTH_MISMATCH;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, merkle_tree.root),
		    PW_ERR_NV_LENGTH_MISMATCH);
	return EC_SUCCESS;
}

static int handle_remove_leaf_success(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_remove_leaf_defaults(&merkle_tree, &buf.request);

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    EC_SUCCESS);

	TEST_ASSERT(MOCK_pw_log_storage.entries[0].type.v ==
		    PW_REMOVE_LEAF);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].label.v ==
		    DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT_ARRAY_EQ(MOCK_pw_log_storage.entries[0].root,
			     EMPTY_TREE.root, sizeof(EMPTY_TREE.root));
	return check_dcrypto_mutex_usage();
}

/******************************************************************************/
/* Try auth test cases.
 */

static int handle_try_auth_invalid_length(void)
{
	return invalid_length_with_leaf_head(
			(size_t)&((struct pw_request_t *)0)->data.try_auth
					.unimported_leaf_data.head,
			setup_try_auth_defaults);
}

static int handle_try_auth_leaf_version_mismatch(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct leaf_header_t *req_head =
			&buf.request.data.try_auth.unimported_leaf_data.head;

	setup_try_auth_defaults(&merkle_tree, &buf.request);

	++req_head->leaf_version.major;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf,
					  ROOT_WITH_DEFAULT_HMAC),
		    PW_ERR_LEAF_VERSION_MISMATCH);
	return check_dcrypto_mutex_usage();
}

static int handle_try_auth_label_invalid(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct leaf_data_t leaf_data;

	memcpy(&leaf_data, &DEFAULT_LEAF, sizeof(DEFAULT_LEAF));
	leaf_data.pub.label.v |= 0x030000;
	setup_try_auth_defaults_with_leaf(&leaf_data, PW_PROTOCOL_VERSION,
					  PW_LEAF_MINOR_VERSION, &merkle_tree,
					  &buf.request);

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf,
					  ROOT_WITH_DEFAULT_HMAC),
		    PW_ERR_LABEL_INVALID);
	return check_dcrypto_mutex_usage();
}

static int handle_try_auth_path_auth_failed(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	uint8_t (*path_hashes)[32] =
			(void *)buf.request.data.try_auth.unimported_leaf_data
					.payload +
			sizeof(struct leaf_public_data_t) +
			sizeof(struct leaf_sensitive_data_t);

	setup_try_auth_defaults(&merkle_tree, &buf.request);

	(*path_hashes)[0] ^= 0xff;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf,
					  ROOT_WITH_DEFAULT_HMAC),
		    PW_ERR_PATH_AUTH_FAILED);
	return check_dcrypto_mutex_usage();
}

static int handle_try_auth_hmac_auth_failed(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_try_auth_defaults(&merkle_tree, &buf.request);

	MOCK_hash_update_cb = 0;
	MOCK_hmac = EMPTY_TREE.root;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf,
					  ROOT_WITH_DEFAULT_HMAC),
		    PW_ERR_HMAC_AUTH_FAILED);
	return check_dcrypto_mutex_usage();
}

static int handle_try_auth_crypto_failure(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_try_auth_defaults(&merkle_tree, &buf.request);

	MOCK_aes_fail = 1;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf,
					  ROOT_WITH_DEFAULT_HMAC),
		    PW_ERR_CRYPTO_FAILURE);
	return check_dcrypto_mutex_usage();
}

static int check_try_auth_rate_limit_reached_response(
		struct merkle_tree_t *merkle_tree,
		struct pw_test_data_t *buf,
		const struct time_diff_t seconds_to_wait)
{
	uint8_t old_root[PW_HASH_SIZE];

	memcpy(old_root, merkle_tree->root, sizeof(old_root));

	TEST_RET_EQ(do_request(merkle_tree, buf), PW_ERR_RATE_LIMIT_REACHED);

	TEST_ASSERT(buf->response.header.version == PW_PROTOCOL_VERSION);
	TEST_ASSERT(buf->response.header.data_length ==
		    sizeof(struct pw_response_try_auth_t) +
		    PW_LEAF_PAYLOAD_SIZE);
	TEST_RET_EQ(buf->response.header.result_code,
		    PW_ERR_RATE_LIMIT_REACHED);
	TEST_ASSERT_ARRAY_EQ(buf->response.header.root, old_root,
			     sizeof(old_root));
	TEST_ASSERT_ARRAY_EQ(buf->response.header.root, merkle_tree->root,
			     sizeof(merkle_tree->root));
	TEST_ASSERT(buf->response.data.try_auth.seconds_to_wait.v ==
		    seconds_to_wait.v);
	TEST_ASSERT_MEMSET(buf->response.data.try_auth.high_entropy_secret,
			   0, PW_SECRET_SIZE);
	TEST_ASSERT_MEMSET((uint8_t *)&buf->response.data.try_auth
			   .unimported_leaf_data, 0,
			   sizeof(buf->response.data.try_auth
			   .unimported_leaf_data) + PW_LEAF_PAYLOAD_SIZE);

	return check_dcrypto_mutex_usage();
}

static int handle_try_auth_rate_limit_reached(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct leaf_data_t leaf_data = {};

	/* Test PW_BLOCK_ATTEMPTS. */
	memcpy(&leaf_data, &DEFAULT_LEAF, sizeof(DEFAULT_LEAF));
	leaf_data.pub.attempt_count.v = 51;
	force_restart_count(1);
	force_time((timestamp_t){.val = 7200llu * SECOND});
	setup_try_auth_defaults_with_leaf(&leaf_data, PW_PROTOCOL_VERSION,
					  PW_LEAF_MINOR_VERSION, &merkle_tree,
					  &buf.request);

	TEST_RET_EQ(check_try_auth_rate_limit_reached_response(
			&merkle_tree, &buf,
			(const struct time_diff_t){PW_BLOCK_ATTEMPTS}),
		    EC_SUCCESS);

	memcpy(&leaf_data, &DEFAULT_LEAF, sizeof(DEFAULT_LEAF));
	memset(leaf_data.pub.delay_schedule, 0,
	       sizeof(leaf_data.pub.delay_schedule));
	leaf_data.pub.delay_schedule[0].attempt_count.v = 5;
	leaf_data.pub.delay_schedule[0].time_diff.v = PW_BLOCK_ATTEMPTS;
	leaf_data.pub.attempt_count.v = 6;
	force_restart_count(1);
	force_time((timestamp_t){.val = 7200llu * SECOND});
	setup_try_auth_defaults_with_leaf(&leaf_data, PW_PROTOCOL_VERSION,
					  PW_LEAF_MINOR_VERSION, &merkle_tree,
					  &buf.request);

	TEST_RET_EQ(check_try_auth_rate_limit_reached_response(
			&merkle_tree, &buf,
			(const struct time_diff_t){PW_BLOCK_ATTEMPTS}),
		    EC_SUCCESS);

	memcpy(&leaf_data, &DEFAULT_LEAF, sizeof(DEFAULT_LEAF));
	memset(leaf_data.pub.delay_schedule, 0,
	       sizeof(leaf_data.pub.delay_schedule));
	leaf_data.pub.delay_schedule[0].attempt_count.v = 5;
	leaf_data.pub.delay_schedule[0].time_diff.v = PW_BLOCK_ATTEMPTS;
	leaf_data.pub.attempt_count.v = 6;
	force_restart_count(1);
	force_time((timestamp_t){.val = 7200llu * SECOND});
	setup_try_auth_defaults_with_leaf(&leaf_data, PW_PROTOCOL_VERSION,
					  PW_LEAF_MINOR_VERSION, &merkle_tree,
					  &buf.request);

	TEST_RET_EQ(check_try_auth_rate_limit_reached_response(
			&merkle_tree, &buf,
			(const struct time_diff_t){PW_BLOCK_ATTEMPTS}),
		    EC_SUCCESS);

	/* Test same boot_count case. */
	memcpy(&leaf_data, &DEFAULT_LEAF, sizeof(DEFAULT_LEAF));
	leaf_data.pub.attempt_count.v = 10;
	leaf_data.pub.timestamp.boot_count = 0;
	leaf_data.pub.timestamp.timer_value = 7200llu;
	setup_try_auth_defaults_with_leaf(&leaf_data, PW_PROTOCOL_VERSION,
					  PW_LEAF_MINOR_VERSION, &merkle_tree,
					  &buf.request);
	force_restart_count(0);
	force_time((timestamp_t){.val = (leaf_data.pub.timestamp.timer_value +
					 3599llu) * SECOND});

	TEST_RET_EQ(check_try_auth_rate_limit_reached_response(
			&merkle_tree, &buf, (const struct time_diff_t){1}),
		    EC_SUCCESS);

	/* Test boot_count + 1 case. */
	memcpy(&leaf_data, &DEFAULT_LEAF, sizeof(DEFAULT_LEAF));
	leaf_data.pub.attempt_count.v = 10;
	leaf_data.pub.timestamp.boot_count = 0;
	leaf_data.pub.timestamp.timer_value = 7200llu;
	setup_try_auth_defaults_with_leaf(&leaf_data, PW_PROTOCOL_VERSION,
					  PW_LEAF_MINOR_VERSION, &merkle_tree,
					  &buf.request);
	force_restart_count(1);
	force_time((timestamp_t){.val = 3599llu * SECOND});

	TEST_RET_EQ(check_try_auth_rate_limit_reached_response(
			&merkle_tree, &buf, (const struct time_diff_t){1}),
		    EC_SUCCESS);

	return check_dcrypto_mutex_usage();
}

static int handle_try_auth_nv_fail(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_try_auth_defaults(&merkle_tree, &buf.request);
	force_restart_count(0);
	force_time((timestamp_t){.val = 65 * SECOND});

	MOCK_setvar_ret = PW_ERR_NV_LENGTH_MISMATCH;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, merkle_tree.root),
		    PW_ERR_NV_LENGTH_MISMATCH);
	return EC_SUCCESS;
}

static int handle_try_auth_lowent_auth_failed(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct leaf_data_t leaf_data = {};
	struct leaf_public_data_t *pub =
			(void *)buf.response.data.try_auth.unimported_leaf_data
					.payload;
	struct leaf_sensitive_data_t sec = {};
	uint8_t *resp_cipher_text = (void *)pub + sizeof(*pub);

	memcpy(&leaf_data, &DEFAULT_LEAF, sizeof(DEFAULT_LEAF));
	leaf_data.pub.attempt_count.v = 5;
	leaf_data.sec.low_entropy_secret[
			sizeof(leaf_data.sec.low_entropy_secret) - 1] =
				~leaf_data.sec.low_entropy_secret[
				 sizeof(leaf_data.sec.low_entropy_secret) - 1];

	setup_try_auth_defaults_with_leaf(&leaf_data, PW_PROTOCOL_VERSION,
					  PW_LEAF_MINOR_VERSION, &merkle_tree,
					  &buf.request);

	force_restart_count(1);
	force_time((timestamp_t){.val = (65ull * SECOND)});

	TEST_RET_EQ(do_request(&merkle_tree, &buf), PW_ERR_LOWENT_AUTH_FAILED);

	TEST_ASSERT(buf.response.header.version == PW_PROTOCOL_VERSION);
	TEST_ASSERT(buf.response.header.data_length ==
		    sizeof(struct pw_response_try_auth_t) +
		    PW_LEAF_PAYLOAD_SIZE);
	TEST_RET_EQ(buf.response.header.result_code, PW_ERR_LOWENT_AUTH_FAILED);

	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, EMPTY_TREE.root,
			     sizeof(EMPTY_TREE.root));
	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, merkle_tree.root,
			     sizeof(merkle_tree.root));

	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.try_auth.unimported_leaf_data.hmac,
			EMPTY_HMAC, sizeof(EMPTY_HMAC));
	TEST_ASSERT_ARRAY_EQ(buf.response.data.try_auth.unimported_leaf_data.iv,
			     DEFAULT_IV, sizeof(DEFAULT_IV));
	DCRYPTO_aes_ctr((uint8_t *)&sec, EMPTY_TREE.wrap_key,
			sizeof(EMPTY_TREE.wrap_key) * 8, DEFAULT_IV,
			resp_cipher_text, sizeof(sec));
	TEST_ASSERT(pub->label.v == leaf_data.pub.label.v);
	TEST_ASSERT_ARRAY_EQ(
			(uint8_t *)&pub->delay_schedule,
			(uint8_t *)&leaf_data.pub.delay_schedule,
			sizeof(leaf_data.pub.delay_schedule));
	TEST_ASSERT_ARRAY_EQ((uint8_t *)&sec, (uint8_t *)&leaf_data.sec,
			     sizeof(leaf_data.sec));
	TEST_ASSERT(pub->attempt_count.v == leaf_data.pub.attempt_count.v + 1);
	TEST_ASSERT(pub->timestamp.boot_count == 1);

	TEST_ASSERT_MEMSET(buf.response.data.try_auth.high_entropy_secret,
			   0, PW_SECRET_SIZE);

	/* A threshold of 100 is used since some time will pass after
	 * force_time() is called.
	 */
	TEST_ASSERT(pub->timestamp.timer_value - 65ull < 100);

	/* Validate the log entry for a failed auth attempt. */
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].type.v == PW_TRY_AUTH);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].label.v ==
		    DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].return_code ==
				    PW_ERR_LOWENT_AUTH_FAILED);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].timestamp.boot_count ==
		    pub->timestamp.boot_count);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].timestamp.timer_value ==
		    pub->timestamp.timer_value);
	TEST_ASSERT_ARRAY_EQ(MOCK_pw_log_storage.entries[0].root,
			     EMPTY_TREE.root,
			     sizeof(EMPTY_TREE.root));
	return check_dcrypto_mutex_usage();
}

static int handle_try_auth_pcr_mismatch(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct leaf_data_t leaf_data = {};

	/* Test same boot_count case. */
	memcpy(&leaf_data, &DEFAULT_LEAF, sizeof(leaf_data));
	leaf_data.pub.attempt_count.v = 6;
	leaf_data.pub.valid_pcr_criteria[0].bitmask[0] = 1;
	memset(leaf_data.pub.valid_pcr_criteria[0].digest, 0, 32);
	setup_try_auth_defaults_with_leaf(&leaf_data, PW_PROTOCOL_VERSION,
					  PW_LEAF_MINOR_VERSION, &merkle_tree,
					  &buf.request);
	force_restart_count(0);
	force_time((timestamp_t){.val = 65 * SECOND});

	TEST_RET_EQ(do_request(&merkle_tree, &buf), PW_ERR_PCR_NOT_MATCH);

	return check_dcrypto_mutex_usage();
}

static int try_auth_success(uint8_t protocol_version,
			     uint8_t minor_version)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct leaf_data_t leaf_data = {};
	struct leaf_public_data_t *pub =
			(void *)buf.response.data.try_auth.unimported_leaf_data
					.payload;
	struct leaf_sensitive_data_t sec = {};
	uint8_t *resp_cipher_text = (void *)pub + sizeof(*pub);

	/* Test same boot_count case. */
	memcpy(&leaf_data, &DEFAULT_LEAF, sizeof(leaf_data));
	leaf_data.pub.attempt_count.v = 6;
	leaf_data.pub.valid_pcr_criteria[0].bitmask[0] = 1;
	memcpy(leaf_data.pub.valid_pcr_criteria[0].digest,
	       DEFAULT_PCR_DIGEST, 32);
	setup_try_auth_defaults_with_leaf(&leaf_data, protocol_version,
					  minor_version, &merkle_tree,
					  &buf.request);
	force_restart_count(0);
	force_time((timestamp_t){.val = 65 * SECOND});

	TEST_RET_EQ(do_request(&merkle_tree, &buf), EC_SUCCESS);

	TEST_ASSERT(buf.response.header.version == protocol_version);
	TEST_ASSERT(buf.response.header.data_length ==
				    sizeof(struct pw_response_try_auth_t) +
				    PW_LEAF_PAYLOAD_SIZE);
	TEST_RET_EQ(buf.response.header.result_code, EC_SUCCESS);

	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, ROOT_WITH_DEFAULT_HMAC,
			     sizeof(ROOT_WITH_DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, merkle_tree.root,
			     sizeof(merkle_tree.root));

	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.try_auth.unimported_leaf_data.hmac,
			DEFAULT_HMAC, sizeof(DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(buf.response.data.try_auth.unimported_leaf_data.iv,
			     DEFAULT_IV, sizeof(DEFAULT_IV));
	DCRYPTO_aes_ctr((uint8_t *)&sec, EMPTY_TREE.wrap_key,
			sizeof(EMPTY_TREE.wrap_key) * 8, DEFAULT_IV,
			resp_cipher_text, sizeof(sec));
	TEST_ASSERT(pub->label.v == leaf_data.pub.label.v);
	TEST_ASSERT_ARRAY_EQ(
			(uint8_t *)&pub->delay_schedule,
			(uint8_t *)&leaf_data.pub.delay_schedule,
			sizeof(leaf_data.pub.delay_schedule));
	if (protocol_version == PW_PROTOCOL_VERSION) {
		TEST_ASSERT_ARRAY_EQ((uint8_t *)&sec,
				     (uint8_t *)&DEFAULT_LEAF.sec,
				     sizeof(DEFAULT_LEAF.sec));
	}
	TEST_ASSERT(pub->attempt_count.v == 0);

	TEST_ASSERT_ARRAY_EQ(buf.response.data.try_auth.high_entropy_secret,
			     DEFAULT_LEAF.sec.high_entropy_secret,
			     sizeof(DEFAULT_LEAF.sec.high_entropy_secret));

	TEST_ASSERT_ARRAY_EQ(buf.response.data.try_auth.reset_secret,
			     DEFAULT_LEAF.sec.reset_secret,
			     sizeof(DEFAULT_LEAF.sec.reset_secret));

	/* Validate the log entry on success. */
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].type.v == PW_TRY_AUTH);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].label.v ==
		    DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].return_code == EC_SUCCESS);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].timestamp.boot_count ==
		    pub->timestamp.boot_count);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].timestamp.timer_value ==
		    pub->timestamp.timer_value);
	TEST_ASSERT_ARRAY_EQ(MOCK_pw_log_storage.entries[0].root,
			     ROOT_WITH_DEFAULT_HMAC,
			     sizeof(ROOT_WITH_DEFAULT_HMAC));

	/* Test boot_count + 1 case. */
	leaf_data.pub.attempt_count.v = 6;
	leaf_data.pub.timestamp.boot_count = 0;
	leaf_data.pub.timestamp.timer_value = 7200llu;
	setup_try_auth_defaults_with_leaf(&leaf_data, protocol_version,
					  minor_version, &merkle_tree,
					  &buf.request);
	force_restart_count(1);
	force_time((timestamp_t){.val = 65llu * SECOND});

	TEST_RET_EQ(do_request(&merkle_tree, &buf), EC_SUCCESS);

	TEST_ASSERT(buf.response.header.version == protocol_version);
	TEST_ASSERT(buf.response.header.data_length ==
		    sizeof(struct pw_response_try_auth_t) +
		    PW_LEAF_PAYLOAD_SIZE);
	TEST_RET_EQ(buf.response.header.result_code, EC_SUCCESS);

	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, ROOT_WITH_DEFAULT_HMAC,
			     sizeof(ROOT_WITH_DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, merkle_tree.root,
			     sizeof(merkle_tree.root));

	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.try_auth.unimported_leaf_data.hmac,
			DEFAULT_HMAC, sizeof(DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(buf.response.data.try_auth.unimported_leaf_data.iv,
			     DEFAULT_IV, sizeof(DEFAULT_IV));
	DCRYPTO_aes_ctr((uint8_t *)&sec, EMPTY_TREE.wrap_key,
			sizeof(EMPTY_TREE.wrap_key) * 8, DEFAULT_IV,
			resp_cipher_text, sizeof(sec));
	TEST_ASSERT(pub->label.v == leaf_data.pub.label.v);
	TEST_ASSERT_ARRAY_EQ(
			(uint8_t *)&pub->delay_schedule,
			(uint8_t *)&leaf_data.pub.delay_schedule,
			sizeof(leaf_data.pub.delay_schedule));
	if (protocol_version == PW_PROTOCOL_VERSION) {
		TEST_ASSERT_ARRAY_EQ((uint8_t *)&sec,
				     (uint8_t *)&DEFAULT_LEAF.sec,
				     sizeof(DEFAULT_LEAF.sec));
	}
	TEST_ASSERT(pub->attempt_count.v == 0);
	TEST_ASSERT_ARRAY_EQ(buf.response.data.try_auth.high_entropy_secret,
			     DEFAULT_LEAF.sec.high_entropy_secret,
			     sizeof(DEFAULT_LEAF.sec.high_entropy_secret));
	return check_dcrypto_mutex_usage();
}

static int handle_try_auth_success(void)
{
	return try_auth_success(PW_PROTOCOL_VERSION, PW_LEAF_MINOR_VERSION);
}

static int handle_try_auth_old_protocol_old_leaf_success(void)
{
	return try_auth_success(0, 0);
}

static int handle_try_auth_old_protocol_new_leaf_success(void)
{
	return try_auth_success(0, PW_LEAF_MINOR_VERSION);
}

/******************************************************************************/
/* Reset auth test cases.
 */

static int handle_reset_auth_invalid_length(void)
{
	return invalid_length_with_leaf_head(
			(size_t)&((struct pw_request_t *)0)->data.reset_auth
					.unimported_leaf_data.head,
			setup_reset_auth_defaults);
}

static int handle_reset_auth_label_invalid(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct leaf_public_data_t *pub =
			(void *)buf.request.data.reset_auth.unimported_leaf_data
					.payload;

	setup_reset_auth_defaults(&merkle_tree, &buf.request);
	pub->label.v |= 0x030000;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, merkle_tree.root),
		    PW_ERR_LABEL_INVALID);
	return check_dcrypto_mutex_usage();
}

static int handle_reset_auth_path_auth_failed(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	uint8_t (*path_hashes)[32] =
			(void *)buf.request.data.reset_auth.unimported_leaf_data
					.payload +
			sizeof(struct leaf_public_data_t) +
			sizeof(struct leaf_sensitive_data_t);

	setup_reset_auth_defaults(&merkle_tree, &buf.request);

	(*path_hashes)[0] ^= 0xff;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, merkle_tree.root),
		    PW_ERR_PATH_AUTH_FAILED);
	return check_dcrypto_mutex_usage();
}

static int handle_reset_auth_hmac_auth_failed(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_reset_auth_defaults(&merkle_tree, &buf.request);

	MOCK_hash_update_cb = 0;
	MOCK_hmac = EMPTY_TREE.root;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, merkle_tree.root),
		    PW_ERR_HMAC_AUTH_FAILED);
	return check_dcrypto_mutex_usage();
}

static int handle_reset_auth_crypto_failure(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_reset_auth_defaults(&merkle_tree, &buf.request);

	MOCK_aes_fail = 1;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, merkle_tree.root),
		    PW_ERR_CRYPTO_FAILURE);
	return check_dcrypto_mutex_usage();
}

static int handle_reset_auth_reset_auth_failed(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_reset_auth_defaults(&merkle_tree, &buf.request);

	buf.request.data.reset_auth.reset_secret[0] ^= 0xff;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, merkle_tree.root),
		    PW_ERR_RESET_AUTH_FAILED);
	return check_dcrypto_mutex_usage();
}

static int handle_reset_auth_nv_fail(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_reset_auth_defaults(&merkle_tree, &buf.request);

	MOCK_setvar_ret = PW_ERR_NV_LENGTH_MISMATCH;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, merkle_tree.root),
		    PW_ERR_NV_LENGTH_MISMATCH);
	return EC_SUCCESS;
}

static int handle_reset_auth_success(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct leaf_public_data_t *pub =
			(void *)buf.response.data.reset_auth
					.unimported_leaf_data.payload;
	struct leaf_sensitive_data_t sec = {};
	uint8_t *resp_cipher_text = (void *)pub + sizeof(*pub);

	setup_reset_auth_defaults(&merkle_tree, &buf.request);

	TEST_RET_EQ(do_request(&merkle_tree, &buf), EC_SUCCESS);

	TEST_ASSERT(buf.response.header.version == PW_PROTOCOL_VERSION);
	TEST_ASSERT(buf.response.header.data_length ==
		    sizeof(struct pw_response_reset_auth_t) +
		    PW_LEAF_PAYLOAD_SIZE);
	TEST_RET_EQ(buf.response.header.result_code, EC_SUCCESS);

	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, ROOT_WITH_DEFAULT_HMAC,
			     sizeof(ROOT_WITH_DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, merkle_tree.root,
			     sizeof(merkle_tree.root));

	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.reset_auth.high_entropy_secret,
			DEFAULT_LEAF.sec.high_entropy_secret,
			sizeof(DEFAULT_LEAF.sec.high_entropy_secret));
	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.reset_auth.unimported_leaf_data.hmac,
			DEFAULT_HMAC, sizeof(DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.reset_auth.unimported_leaf_data.iv,
			DEFAULT_IV, sizeof(DEFAULT_IV));
	DCRYPTO_aes_ctr((uint8_t *)&sec, EMPTY_TREE.wrap_key,
			sizeof(EMPTY_TREE.wrap_key) * 8, DEFAULT_IV,
			resp_cipher_text, sizeof(sec));
	TEST_ASSERT(pub->label.v == DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT_ARRAY_EQ(
			(const uint8_t *)&pub->delay_schedule,
			(const uint8_t *)&DEFAULT_LEAF.pub.delay_schedule,
			sizeof(DEFAULT_LEAF.pub.delay_schedule));
	TEST_ASSERT_ARRAY_EQ((uint8_t *)&sec, (uint8_t *)&DEFAULT_LEAF.sec,
			     sizeof(DEFAULT_LEAF.sec));
	TEST_ASSERT(pub->attempt_count.v == 0);

	/* Validate the log entry on success. */
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].type.v == PW_TRY_AUTH);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].label.v ==
		    DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].return_code == EC_SUCCESS);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].timestamp.boot_count ==
		    pub->timestamp.boot_count);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].timestamp.timer_value ==
		    pub->timestamp.timer_value);
	TEST_ASSERT_ARRAY_EQ(MOCK_pw_log_storage.entries[0].root,
			     ROOT_WITH_DEFAULT_HMAC,
			     sizeof(ROOT_WITH_DEFAULT_HMAC));

	/* Test with different minor version and struct lengths. */
	setup_reset_auth_defaults(&merkle_tree, &buf.request);
	setup_mock_future_version(
			&buf.request.data.reset_auth.unimported_leaf_data,
			&buf.request.header.data_length);

	TEST_RET_EQ(do_request(&merkle_tree, &buf), EC_SUCCESS);

	TEST_ASSERT(buf.response.header.version == PW_PROTOCOL_VERSION);
	TEST_ASSERT(buf.response.header.data_length ==
		    sizeof(struct pw_response_reset_auth_t) +
		    PW_LEAF_PAYLOAD_SIZE);
	TEST_RET_EQ(buf.response.header.result_code, EC_SUCCESS);

	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, ROOT_WITH_DEFAULT_HMAC,
			     sizeof(ROOT_WITH_DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, merkle_tree.root,
			     sizeof(merkle_tree.root));

	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.reset_auth.high_entropy_secret,
			DEFAULT_LEAF.sec.high_entropy_secret,
			sizeof(DEFAULT_LEAF.sec.high_entropy_secret));
	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.reset_auth.unimported_leaf_data.hmac,
			DEFAULT_HMAC, sizeof(DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.reset_auth.unimported_leaf_data.iv,
			DEFAULT_IV, sizeof(DEFAULT_IV));
	DCRYPTO_aes_ctr((uint8_t *)&sec, EMPTY_TREE.wrap_key,
			sizeof(EMPTY_TREE.wrap_key) * 8, DEFAULT_IV,
			resp_cipher_text, sizeof(sec));
	TEST_ASSERT(pub->label.v == DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT_ARRAY_EQ(
			(const uint8_t *)&pub->delay_schedule,
			(const uint8_t *)&DEFAULT_LEAF.pub.delay_schedule,
			sizeof(DEFAULT_LEAF.pub.delay_schedule));
	TEST_ASSERT_ARRAY_EQ((uint8_t *)&sec, (uint8_t *)&DEFAULT_LEAF.sec,
			     sizeof(DEFAULT_LEAF.sec));
	TEST_ASSERT(pub->attempt_count.v == 0);

	/* Validate the log entry on success. */
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].type.v == PW_TRY_AUTH);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].label.v ==
		    DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].return_code == EC_SUCCESS);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].timestamp.boot_count ==
		    pub->timestamp.boot_count);
	TEST_ASSERT(MOCK_pw_log_storage.entries[0].timestamp.timer_value ==
		    pub->timestamp.timer_value);
	TEST_ASSERT_ARRAY_EQ(MOCK_pw_log_storage.entries[0].root,
			     ROOT_WITH_DEFAULT_HMAC,
			     sizeof(ROOT_WITH_DEFAULT_HMAC));
	return check_dcrypto_mutex_usage();
}

/******************************************************************************/
/* Get log test cases.
 */

static int handle_get_log_invalid_length(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_get_log_defaults(&merkle_tree, &buf.request);

	++buf.request.header.data_length;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_LENGTH_INVALID);
	return check_dcrypto_mutex_usage();
}

static int handle_get_log_nv_fail(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_get_log_defaults(&merkle_tree, &buf.request);

	MOCK_getvar_ret = PW_ERR_NV_EMPTY;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_NV_EMPTY);
	return check_dcrypto_mutex_usage();
}

static int handle_get_log_success(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	const struct pw_get_log_entry_t (*view)[PW_LOG_ENTRY_COUNT] =
			(void *)buf.response.data.get_log;

	setup_get_log_defaults(&merkle_tree, &buf.request);
	setup_storage(4);

	TEST_RET_EQ(do_request(&merkle_tree, &buf), EC_SUCCESS);

	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, EMPTY_TREE.root,
			     sizeof(EMPTY_TREE.root));
	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, merkle_tree.root,
			     sizeof(merkle_tree.root));

	TEST_ASSERT(buf.response.header.version == PW_PROTOCOL_VERSION);
	TEST_ASSERT(buf.response.header.data_length ==
		    sizeof(struct pw_get_log_entry_t) * PW_LOG_ENTRY_COUNT);
	TEST_RET_EQ(buf.response.header.result_code, EC_SUCCESS);

	TEST_ASSERT(buf.response.header.version == PW_PROTOCOL_VERSION);

	TEST_ASSERT((*view)[0].type.v == PW_REMOVE_LEAF);
	TEST_ASSERT((*view)[0].label.v == DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT_ARRAY_EQ((*view)[0].root, EMPTY_TREE.root,
			     sizeof(EMPTY_TREE.root));

	TEST_ASSERT((*view)[1].type.v == PW_TRY_AUTH);
	TEST_ASSERT((*view)[1].label.v == DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT((*view)[1].return_code == EC_SUCCESS);
	TEST_ASSERT((*view)[1].timestamp.boot_count == 10);
	TEST_ASSERT((*view)[1].timestamp.timer_value == 100);
	TEST_ASSERT_ARRAY_EQ((*view)[1].root, ROOT_WITH_DEFAULT_HMAC,
			     sizeof(ROOT_WITH_DEFAULT_HMAC));

	setup_get_log_defaults(&merkle_tree, &buf.request);
	setup_storage(2);

	TEST_RET_EQ(do_request(&merkle_tree, &buf), EC_SUCCESS);

	TEST_ASSERT((*view)[0].type.v == PW_TRY_AUTH);
	TEST_ASSERT((*view)[0].label.v == DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT((*view)[0].return_code == PW_ERR_LOWENT_AUTH_FAILED);
	TEST_ASSERT((*view)[0].timestamp.boot_count == 7);
	TEST_ASSERT((*view)[0].timestamp.timer_value == 99);
	TEST_ASSERT_ARRAY_EQ((*view)[0].root, ROOT_WITH_OTHER_HMAC,
			     sizeof(ROOT_WITH_OTHER_HMAC));

	TEST_ASSERT((*view)[1].type.v == PW_INSERT_LEAF);
	TEST_ASSERT((*view)[1].label.v == DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT_ARRAY_EQ((*view)[1].root, ROOT_WITH_DEFAULT_HMAC,
			     sizeof(ROOT_WITH_DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ((*view)[1].leaf_hmac, DEFAULT_HMAC,
			     sizeof(DEFAULT_HMAC));

	setup_get_log_defaults(&merkle_tree, &buf.request);
	setup_storage(0);

	TEST_RET_EQ(do_request(&merkle_tree, &buf), EC_SUCCESS);

	TEST_ASSERT((*view)[0].type.v == PW_RESET_TREE);
	TEST_ASSERT_ARRAY_EQ((*view)[0].root, EMPTY_TREE.root,
			     sizeof(EMPTY_TREE.root));

	return check_dcrypto_mutex_usage();
}

/******************************************************************************/
/* Log replay test cases.
 */

static int handle_log_replay_invalid_length(void)
{
	return invalid_length_with_leaf_head(
			(size_t)&((struct pw_request_t *)0)->data.log_replay
					.unimported_leaf_data.head,
			setup_log_replay_defaults);
}

static int handle_log_replay_nv_fail(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_log_replay_defaults(&merkle_tree, &buf.request);

	MOCK_getvar_ret = PW_ERR_NV_EMPTY;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_NV_EMPTY);
	return check_dcrypto_mutex_usage();
}

static int handle_log_replay_root_not_found(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_log_replay_defaults(&merkle_tree, &buf.request);

	memcpy(buf.request.data.log_replay.log_root, DEFAULT_HMAC,
	       sizeof(DEFAULT_HMAC));

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_ROOT_NOT_FOUND);
	return check_dcrypto_mutex_usage();
}

static int handle_log_replay_type_invalid(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;

	setup_log_replay_defaults(&merkle_tree, &buf.request);

	memcpy(buf.request.data.log_replay.log_root, EMPTY_TREE.root,
	       sizeof(EMPTY_TREE.root));

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_TYPE_INVALID);
	return check_dcrypto_mutex_usage();
}

static int handle_log_replay_hmac_auth_failed(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct leaf_data_t leaf_data = {};

	memcpy(&leaf_data, &DEFAULT_LEAF, sizeof(leaf_data));
	leaf_data.pub.attempt_count.v = 7;
	setup_log_replay_defaults_with_leaf(&leaf_data, &merkle_tree,
					    &buf.request);

	memcpy(buf.request.data.log_replay.unimported_leaf_data.hmac,
	       EMPTY_HMAC, sizeof(EMPTY_HMAC));

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_HMAC_AUTH_FAILED);
	return check_dcrypto_mutex_usage();
}

static int handle_log_replay_crypto_failure(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct leaf_data_t leaf_data = {};

	memcpy(&leaf_data, &DEFAULT_LEAF, sizeof(leaf_data));
	leaf_data.pub.attempt_count.v = 7;
	setup_log_replay_defaults_with_leaf(&leaf_data, &merkle_tree,
					    &buf.request);

	MOCK_aes_fail = 1;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_CRYPTO_FAILURE);
	return check_dcrypto_mutex_usage();
}

static int handle_log_replay_label_invalid(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct leaf_data_t leaf_data = {};

	memcpy(&leaf_data, &DEFAULT_LEAF, sizeof(leaf_data));
	leaf_data.pub.label.v = 0;
	setup_log_replay_defaults_with_leaf(&leaf_data, &merkle_tree,
					    &buf.request);

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_LABEL_INVALID);
	return check_dcrypto_mutex_usage();
}

static int handle_log_replay_path_auth_failed(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	uint8_t (*path_hashes)[32] =
			(void *)buf.request.data.log_replay.unimported_leaf_data
					.payload +
			sizeof(struct leaf_public_data_t) +
			sizeof(struct leaf_sensitive_data_t);

	setup_log_replay_defaults(&merkle_tree, &buf.request);

	(*path_hashes)[0] ^= 0xff;

	TEST_RET_EQ(test_handle_short_msg(&merkle_tree, &buf, EMPTY_TREE.root),
		    PW_ERR_PATH_AUTH_FAILED);
	return check_dcrypto_mutex_usage();
}

static int handle_log_replay_success(void)
{
	struct merkle_tree_t merkle_tree;
	struct pw_test_data_t buf;
	struct leaf_data_t leaf_data = {};
	struct leaf_public_data_t *pub =
			(void *)buf.response.data.log_replay
					.unimported_leaf_data.payload;
	struct leaf_sensitive_data_t sec = {};
	uint8_t *resp_cipher_text = (void *)pub + sizeof(*pub);

	/*
	 * Test for auth success.
	 */
	setup_log_replay_defaults(&merkle_tree, &buf.request);

	TEST_RET_EQ(do_request(&merkle_tree, &buf), EC_SUCCESS);

	TEST_ASSERT(buf.response.header.version == PW_PROTOCOL_VERSION);
	TEST_ASSERT(buf.response.header.data_length ==
		    sizeof(struct pw_response_log_replay_t) +
		    PW_LEAF_PAYLOAD_SIZE);
	TEST_RET_EQ(buf.response.header.result_code, EC_SUCCESS);

	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, EMPTY_TREE.root,
			     sizeof(EMPTY_TREE.root));
	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, merkle_tree.root,
			     sizeof(merkle_tree.root));

	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.log_replay.unimported_leaf_data.hmac,
			DEFAULT_HMAC, sizeof(DEFAULT_HMAC));
	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.log_replay.unimported_leaf_data.iv,
			DEFAULT_IV, sizeof(DEFAULT_IV));
	DCRYPTO_aes_ctr((uint8_t *)&sec, EMPTY_TREE.wrap_key,
			sizeof(EMPTY_TREE.wrap_key) * 8, DEFAULT_IV,
			resp_cipher_text, sizeof(sec));
	TEST_ASSERT(pub->label.v == DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT_ARRAY_EQ(
			(const uint8_t *)&pub->delay_schedule,
			(const uint8_t *)&DEFAULT_LEAF.pub.delay_schedule,
			sizeof(DEFAULT_LEAF.pub.delay_schedule));
	TEST_ASSERT_ARRAY_EQ((uint8_t *)&sec, (uint8_t *)&DEFAULT_LEAF.sec,
			     sizeof(DEFAULT_LEAF.sec));
	TEST_ASSERT(pub->attempt_count.v == 0);
	TEST_ASSERT(pub->timestamp.boot_count == 10);
	TEST_ASSERT(pub->timestamp.timer_value == 100);

	/*
	 * Test for auth failed.
	 */
	memcpy(&leaf_data, &DEFAULT_LEAF, sizeof(leaf_data));
	leaf_data.pub.attempt_count.v = 15;
	setup_log_replay_defaults_with_leaf(&leaf_data, &merkle_tree,
					    &buf.request);
	memcpy(buf.request.data.log_replay.log_root, ROOT_WITH_OTHER_HMAC,
	       sizeof(ROOT_WITH_OTHER_HMAC));
	setup_storage(2);

	TEST_RET_EQ(do_request(&merkle_tree, &buf), EC_SUCCESS);

	TEST_ASSERT(buf.response.header.version == PW_PROTOCOL_VERSION);
	TEST_ASSERT(buf.response.header.data_length ==
		    sizeof(struct pw_response_log_replay_t) +
		    PW_LEAF_PAYLOAD_SIZE);
	TEST_RET_EQ(buf.response.header.result_code, EC_SUCCESS);

	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, EMPTY_TREE.root,
			     sizeof(EMPTY_TREE.root));
	TEST_ASSERT_ARRAY_EQ(buf.response.header.root, merkle_tree.root,
			     sizeof(merkle_tree.root));

	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.log_replay.unimported_leaf_data.hmac,
			OTHER_HMAC, sizeof(OTHER_HMAC));
	TEST_ASSERT_ARRAY_EQ(
			buf.response.data.log_replay.unimported_leaf_data.iv,
			DEFAULT_IV, sizeof(DEFAULT_IV));
	DCRYPTO_aes_ctr((uint8_t *)&sec, EMPTY_TREE.wrap_key,
			sizeof(EMPTY_TREE.wrap_key) * 8, DEFAULT_IV,
			resp_cipher_text, sizeof(sec));
	TEST_ASSERT(pub->label.v == DEFAULT_LEAF.pub.label.v);
	TEST_ASSERT_ARRAY_EQ(
			(const uint8_t *)&pub->delay_schedule,
			(const uint8_t *)&DEFAULT_LEAF.pub.delay_schedule,
			sizeof(DEFAULT_LEAF.pub.delay_schedule));
	TEST_ASSERT_ARRAY_EQ((uint8_t *)&sec, (uint8_t *)&DEFAULT_LEAF.sec,
			     sizeof(DEFAULT_LEAF.sec));
	TEST_ASSERT(pub->attempt_count.v == 16);
	TEST_ASSERT(pub->timestamp.boot_count == 7);
	TEST_ASSERT(pub->timestamp.timer_value == 99);
	return check_dcrypto_mutex_usage();
}

/******************************************************************************/
/* Main test function. Encapsulates the test cases..
 */

void run_test(void)
{
	test_reset();

	/* Test basic operations. */
	RUN_TEST(get_path_auxiliary_hash_count_test);
	RUN_TEST(compute_hash_test);

	/* Test header validation. */
	RUN_TEST(handle_request_version_mismatch);
	RUN_TEST(handle_request_invalid_type);

	/* Test reset tree. */
	RUN_TEST(handle_reset_tree_invalid_length);
	RUN_TEST(handle_reset_tree_bits_per_level_invalid);
	RUN_TEST(handle_reset_tree_height_invalid);
	RUN_TEST(handle_reset_tree_crypto_failure);
	RUN_TEST(handle_reset_tree_nv_fail);
	RUN_TEST(handle_reset_tree_success);

	/* Test insert leaf. */
	RUN_TEST(handle_insert_leaf_invalid_length);
	RUN_TEST(handle_insert_leaf_label_invalid);
	RUN_TEST(handle_insert_leaf_delay_schedule_invalid);
	RUN_TEST(handle_insert_leaf_path_auth_failed);
	RUN_TEST(handle_insert_leaf_crypto_failure);
	RUN_TEST(handle_insert_leaf_nv_fail);
	RUN_TEST(handle_insert_leaf_success);
	RUN_TEST(handle_insert_leaf_old_protocol_success);

	/* Test remove leaf. */
	RUN_TEST(handle_remove_leaf_invalid_length);
	RUN_TEST(handle_remove_leaf_label_invalid);
	RUN_TEST(handle_remove_leaf_path_auth_failed);
	RUN_TEST(handle_remove_leaf_nv_fail);
	RUN_TEST(handle_remove_leaf_success);

	/* Test try auth. */
	RUN_TEST(handle_try_auth_invalid_length);
	RUN_TEST(handle_try_auth_leaf_version_mismatch);
	RUN_TEST(handle_try_auth_label_invalid);
	RUN_TEST(handle_try_auth_path_auth_failed);
	RUN_TEST(handle_try_auth_hmac_auth_failed);
	RUN_TEST(handle_try_auth_crypto_failure);
	RUN_TEST(handle_try_auth_rate_limit_reached);
	RUN_TEST(handle_try_auth_nv_fail);
	RUN_TEST(handle_try_auth_lowent_auth_failed);
	RUN_TEST(handle_try_auth_pcr_mismatch);
	RUN_TEST(handle_try_auth_success);
	RUN_TEST(handle_try_auth_old_protocol_old_leaf_success);
	RUN_TEST(handle_try_auth_old_protocol_new_leaf_success);

	/* Test reset auth. */
	RUN_TEST(handle_reset_auth_invalid_length);
	RUN_TEST(handle_reset_auth_label_invalid);
	RUN_TEST(handle_reset_auth_path_auth_failed);
	RUN_TEST(handle_reset_auth_hmac_auth_failed);
	RUN_TEST(handle_reset_auth_crypto_failure);
	RUN_TEST(handle_reset_auth_reset_auth_failed);
	RUN_TEST(handle_reset_auth_nv_fail);
	RUN_TEST(handle_reset_auth_success);

	/* Test get log. */
	RUN_TEST(handle_get_log_invalid_length);
	RUN_TEST(handle_get_log_nv_fail);
	RUN_TEST(handle_get_log_success);

	/* Test log replay. */
	RUN_TEST(handle_log_replay_invalid_length);
	RUN_TEST(handle_log_replay_nv_fail);
	RUN_TEST(handle_log_replay_root_not_found);
	RUN_TEST(handle_log_replay_type_invalid);
	RUN_TEST(handle_log_replay_hmac_auth_failed);
	RUN_TEST(handle_log_replay_crypto_failure);
	RUN_TEST(handle_log_replay_label_invalid);
	RUN_TEST(handle_log_replay_path_auth_failed);
	RUN_TEST(handle_log_replay_success);

	test_print_result();
}

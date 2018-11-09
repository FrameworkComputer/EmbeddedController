/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Shared types between Cr50 and the AP side code. */

#ifndef __CROS_EC_INCLUDE_PINWEAVER_TYPES_H
#define __CROS_EC_INCLUDE_PINWEAVER_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PW_PACKED __packed

#define PW_PROTOCOL_VERSION 1
#define PW_LEAF_MAJOR_VERSION 0
/* The change from version zero is the addition of valid_pcr_value metadata */
#define PW_LEAF_MINOR_VERSION 1

#define PW_MAX_MESSAGE_SIZE (2048 - 12 /* sizeof(struct tpm_cmd_header) */)

/* The block size of encryption used for wrapped_leaf_data_t. */
#define PW_WRAP_BLOCK_SIZE 16

#define PW_ALIGN_TO_WRD __aligned(4)

#define PW_ALIGN_TO_BLK __aligned(PW_WRAP_BLOCK_SIZE)

enum pw_error_codes_enum {
	PW_ERR_VERSION_MISMATCH = 0x10000, /* EC_ERROR_INTERNAL_FIRST */
	PW_ERR_TREE_INVALID,
	PW_ERR_LENGTH_INVALID,
	PW_ERR_TYPE_INVALID,
	PW_ERR_BITS_PER_LEVEL_INVALID,
	PW_ERR_HEIGHT_INVALID,
	PW_ERR_LABEL_INVALID,
	PW_ERR_DELAY_SCHEDULE_INVALID,
	PW_ERR_PATH_AUTH_FAILED,
	PW_ERR_LEAF_VERSION_MISMATCH,
	PW_ERR_HMAC_AUTH_FAILED,
	PW_ERR_LOWENT_AUTH_FAILED,
	PW_ERR_RESET_AUTH_FAILED,
	PW_ERR_CRYPTO_FAILURE,
	PW_ERR_RATE_LIMIT_REACHED,
	PW_ERR_ROOT_NOT_FOUND,
	PW_ERR_NV_EMPTY,
	PW_ERR_NV_LENGTH_MISMATCH,
	PW_ERR_NV_VERSION_MISMATCH,
	PW_ERR_PCR_NOT_MATCH,
};

/* Represents the log2(fan out) of a tree. */
struct PW_PACKED bits_per_level_t {
	uint8_t v;
};

 /* Represent the height of a tree. */
struct PW_PACKED height_t {
	uint8_t v;
};

/* Represents a child index of a node in a tree. */
struct PW_PACKED index_t {
	uint8_t v;
};

/* Represents the child index for each level of a tree along a path to a leaf.
 * It is a Little-endian unsigned integer with the following value (MSB->LSB)
 * | Zero padding | 1st level index | ... | leaf index |,
 * where each index is represented by bits_per_level bits.
 */
struct PW_PACKED label_t {
	uint64_t v;
};

/* Represents a count of failed login attempts. This is capped at UINT32_MAX. */
struct PW_PACKED attempt_count_t {
	uint32_t v;
};

/* Represents a notion of time. */
struct PW_PACKED pw_timestamp_t {
	/* Number of boots. This is used to track if Cr50 has rebooted since
	 * timer_value was recorded.
	 */
	uint32_t boot_count;
	/* Seconds since boot. */
	uint64_t timer_value;
};

/* Represents a time interval in seconds.
 *
 * This only needs to be sufficiently large to represent the longest time
 * between allowed attempts.
 */
struct PW_PACKED time_diff_t {
	uint32_t v;
};
#define PW_BLOCK_ATTEMPTS UINT32_MAX

/* Number of bytes required for a hash or hmac value in the merkle tree. */
#define PW_HASH_SIZE 32

/* Represents a single entry in a delay schedule table. */
struct PW_PACKED delay_schedule_entry_t {
	struct attempt_count_t attempt_count;
	struct time_diff_t time_diff;
};

/* Represents a set of PCR values hashed into a single digest. This is a
 * criterion that can be added to a leaf. A leaf is valid only if at least one
 * of the valid_pcr_value_t criteria it contains is satisfied.
 */
struct PW_PACKED valid_pcr_value_t {
	/* The set of PCR indexes that have to pass the validation. */
	uint8_t bitmask[2];
	/* The hash digest of the PCR values contained in the bitmask */
	uint8_t digest[32];
};

/* Represents the number of entries in the delay schedule table which can be
 * used to determine the next time an authentication attempt can be made.
 */
#define PW_SCHED_COUNT 16

/* Represents the maximum number of criteria for valid PCR values.
 */
#define PW_MAX_PCR_CRITERIA_COUNT 2

/* Number of bytes required to store a secret.
 */
#define PW_SECRET_SIZE 32

struct PW_PACKED leaf_version_t {
	/* minor comes first so this struct will be compatibile with uint32_t
	 * comparisons for little endian to make version comparisons easier.
	 *
	 * Changes to minor versions are allowed to add new fields, but not
	 * remove existing fields, and they are allowed to be interpreted by
	 * previous versions---any extra fields are truncated.
	 *
	 * Leafs will reject future major versions assuming they are
	 * incompatible, so fields in struct leaf_public_data_t and
	 * struct leaf_sensitive_data_t may be removed for new major versions.
	 * Upgrades across major versions will require explicit logic to
	 * map the old struct to the new struct or vice versa.
	 */
	uint16_t minor;
	uint16_t major;
};

/* Do not change this within the same PW_LEAF_MAJOR_VERSION. */
struct PW_PACKED leaf_header_t {
	/* Always have leaf_version at the beginning of
	 * struct wrapped_leaf_data_t to maintain preditable behavior across
	 * versions.
	 */
	struct leaf_version_t leaf_version;
	uint16_t pub_len;
	uint16_t sec_len;
};

/* Do not remove fields within the same PW_LEAF_MAJOR_VERSION. */
/* Unencrypted part of the leaf data.
 */
struct PW_PACKED leaf_public_data_t {
	struct label_t label;
	struct delay_schedule_entry_t delay_schedule[PW_SCHED_COUNT];

	/* State used to rate limit. */
	struct pw_timestamp_t timestamp;
	struct attempt_count_t attempt_count;
	struct valid_pcr_value_t valid_pcr_criteria[PW_MAX_PCR_CRITERIA_COUNT];
};

/* Represents a struct of unknown length to be imported to process a request. */
struct PW_PACKED unimported_leaf_data_t {
	/* This is first so that head.leaf_version will be the first field
	 * in the struct to make handling different struct versions easier.
	 */
	struct leaf_header_t head;
	/* Covers .head, .iv, and .payload (excluding path_hashes) */
	uint8_t hmac[PW_HASH_SIZE];
	uint8_t iv[PW_WRAP_BLOCK_SIZE];
	/* This field is treated as having a zero size by the compiler so the
	 * actual size needs to be added to the size of this struct. This allows
	 * for forward compatibility using the pub_len and sec_len fields in the
	 * header.
	 *
	 * Has following layout:
	 * Required:
	 *  uint8_t pub_data[head.pub_len];
	 *  uint8_t ciphter_text[head.sec_len];
	 *
	 * For Requests only:
	 *  uint8_t path_hashes[get_path_auxiliary_hash_count(.)][PW_HASH_SIZE];
	 */
	uint8_t payload[];
};

/******************************************************************************/
/* Message structs
 *
 * The message format is a pw_request_header_t followed by the data
 */

enum pw_message_type_enum {
	PW_MT_INVALID = 0,

	/* Request / "Question" types. */
	PW_RESET_TREE = 1,
	PW_INSERT_LEAF,
	PW_REMOVE_LEAF,
	PW_TRY_AUTH,
	PW_RESET_AUTH,
	PW_GET_LOG,
	PW_LOG_REPLAY,
};

struct PW_PACKED pw_message_type_t {
	uint8_t v;
};

struct PW_PACKED pw_request_header_t {
	uint8_t version;
	struct pw_message_type_t type;
	uint16_t data_length;
};

struct PW_PACKED pw_response_header_t {
	uint8_t version;
	uint16_t data_length; /* Does not include the header. */
	uint32_t result_code;
	uint8_t root[PW_HASH_SIZE];
};

struct PW_PACKED pw_request_reset_tree_t {
	struct bits_per_level_t bits_per_level;
	struct height_t height;
};

/* This is only used for parsing incoming data of version 0:0 */
struct PW_PACKED pw_request_insert_leaf00_t {
	struct label_t label;
	struct delay_schedule_entry_t delay_schedule[PW_SCHED_COUNT];
	uint8_t low_entropy_secret[PW_SECRET_SIZE];
	uint8_t high_entropy_secret[PW_SECRET_SIZE];
	uint8_t reset_secret[PW_SECRET_SIZE];
	/* This is a variable length field because it size is determined at
	 * runtime based on the chosen tree parameters. Its size is treated as
	 * zero by the compiler so the computed size needs to be added to the
	 * size of this struct in order to determine the actual size. This field
	 * has the form:
	 * uint8_t path_hashes[get_path_auxiliary_hash_count(.)][PW_HASH_SIZE];
	 */
	uint8_t path_hashes[][PW_HASH_SIZE];
};

struct PW_PACKED pw_request_insert_leaf_t {
	struct label_t label;
	struct delay_schedule_entry_t delay_schedule[PW_SCHED_COUNT];
	uint8_t low_entropy_secret[PW_SECRET_SIZE];
	uint8_t high_entropy_secret[PW_SECRET_SIZE];
	uint8_t reset_secret[PW_SECRET_SIZE];
	struct valid_pcr_value_t valid_pcr_criteria[PW_MAX_PCR_CRITERIA_COUNT];
	/* This is a variable length field because it size is determined at
	 * runtime based on the chosen tree parameters. Its size is treated as
	 * zero by the compiler so the computed size needs to be added to the
	 * size of this struct in order to determine the actual size. This field
	 * has the form:
	 * uint8_t path_hashes[get_path_auxiliary_hash_count(.)][PW_HASH_SIZE];
	 */
	uint8_t path_hashes[][PW_HASH_SIZE];
};

struct PW_PACKED pw_response_insert_leaf_t {
	struct unimported_leaf_data_t unimported_leaf_data;
};

struct PW_PACKED pw_request_remove_leaf_t {
	struct label_t leaf_location;
	uint8_t leaf_hmac[PW_HASH_SIZE];
	/* See (struct pw_request_insert_leaf_t).path_hashes. */
	uint8_t path_hashes[][PW_HASH_SIZE];
};

struct PW_PACKED pw_request_try_auth_t {
	uint8_t low_entropy_secret[PW_SECRET_SIZE];
	struct unimported_leaf_data_t unimported_leaf_data;
};

/* This is only used to send response data of version 0:0 */
struct PW_PACKED pw_response_try_auth00_t {
	/* Valid for the PW_ERR_RATE_LIMIT_REACHED return code only. */
	struct time_diff_t seconds_to_wait;
	/* Valid for the EC_SUCCESS return code only. */
	uint8_t high_entropy_secret[PW_SECRET_SIZE];
	/* Valid for the PW_ERR_LOWENT_AUTH_FAILED and EC_SUCCESS return codes.
	 */
	struct unimported_leaf_data_t unimported_leaf_data;
};

struct PW_PACKED pw_response_try_auth_t {
	/* Valid for the PW_ERR_RATE_LIMIT_REACHED return code only. */
	struct time_diff_t seconds_to_wait;
	/* Valid for the EC_SUCCESS return code only. */
	uint8_t high_entropy_secret[PW_SECRET_SIZE];
	/* Valid for the EC_SUCCESS return code only. */
	uint8_t reset_secret[PW_SECRET_SIZE];
	/* Valid for the PW_ERR_LOWENT_AUTH_FAILED and EC_SUCCESS return codes.
	 */
	struct unimported_leaf_data_t unimported_leaf_data;
};

struct PW_PACKED pw_request_reset_auth_t {
	uint8_t reset_secret[PW_SECRET_SIZE];
	struct unimported_leaf_data_t unimported_leaf_data;
};

struct PW_PACKED pw_response_reset_auth_t {
	uint8_t high_entropy_secret[PW_SECRET_SIZE];
	struct unimported_leaf_data_t unimported_leaf_data;
};

struct PW_PACKED pw_request_get_log_t {
	/* The root on the CrOS side that needs to be brought back in sync with
	 * the root on Cr50. If this doesn't match a log entry, the entire log
	 * is returned.
	 */
	uint8_t root[PW_HASH_SIZE];
};

struct PW_PACKED pw_request_log_replay_t {
	/* The root hash after the desired log event.
	 * The log entry that matches this hash contains all the necessary
	 * data to update wrapped_leaf_data
	 */
	uint8_t log_root[PW_HASH_SIZE];
	struct unimported_leaf_data_t unimported_leaf_data;
};

struct PW_PACKED pw_response_log_replay_t {
	struct unimported_leaf_data_t unimported_leaf_data;
};

struct PW_PACKED pw_get_log_entry_t {
	/* The root hash after this operation. */
	uint8_t root[PW_HASH_SIZE];
	/* The label of the leaf that was operated on. */
	struct label_t label;
	/* The type of operation. This should be one of
	 * PW_INSERT_LEAF,
	 * PW_REMOVE_LEAF,
	 * PW_TRY_AUTH.
	 *
	 * Successful PW_RESET_AUTH events are included
	 */
	struct pw_message_type_t type;
	/* Type specific fields. */
	union {
		/* PW_INSERT_LEAF */
		uint8_t leaf_hmac[PW_HASH_SIZE];
		/* PW_REMOVE_LEAF */
		/* PW_TRY_AUTH */
		struct PW_PACKED {
			struct pw_timestamp_t timestamp;
			int32_t return_code;
		};
	};
};

struct PW_PACKED pw_request_t {
	struct pw_request_header_t header;
	union {
		struct pw_request_reset_tree_t reset_tree;
		struct pw_request_insert_leaf00_t insert_leaf00;
		struct pw_request_insert_leaf_t insert_leaf;
		struct pw_request_remove_leaf_t remove_leaf;
		struct pw_request_try_auth_t try_auth;
		struct pw_request_reset_auth_t reset_auth;
		struct pw_request_get_log_t get_log;
		struct pw_request_log_replay_t log_replay;
	} data;
};

struct PW_PACKED pw_response_t {
	struct pw_response_header_t header;
	union {

		struct pw_response_insert_leaf_t insert_leaf;
		struct pw_response_try_auth00_t try_auth00;
		struct pw_response_try_auth_t try_auth;
		struct pw_response_reset_auth_t reset_auth;
		/* An array with as many entries as are present in the log up to
		 * the present time or will fit in the message.
		 */
		uint8_t get_log[0];
		struct pw_response_log_replay_t log_replay;
	} data;
};

/* An explicit limit is set because struct unimported_leaf_data_t can have more
 * than one variable length field so the max length for these fields needs to be
 * defined so that meaningful parameter limits can be set to validate the tree
 * parameters.
 *
 * 1024 was chosen because it is 1/2 of 2048 and allows for a maximum tree
 * height of 10 for the default fan-out of 4.
 */
#define PW_MAX_PATH_SIZE 1024

#ifdef __cplusplus
}
#endif

#endif  /* __CROS_EC_INCLUDE_PINWEAVER_TYPES_H */

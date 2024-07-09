/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_UPDATE_FW_H
#define __CROS_EC_UPDATE_FW_H

#include "compile_time_macros.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This file contains structures used to facilitate EC firmware updates
 * over USB (and over TPM for cr50).
 *
 * The firmware update protocol consists of two phases: connection
 * establishment and actual image transfer.
 *
 * Image transfer is done in 1K blocks. The host supplying the image
 * encapsulates blocks in PDUs by prepending a header including the flash
 * offset where the block is destined and its digest.
 *
 * The EC device responds to each PDU with a confirmation which is 1 byte
 * response. Zero value means success, non zero value is the error code
 * reported by EC.
 *
 * To establish the connection, the host sends a different PDU, which
 * contains no data and is destined to offset 0. Receiving such a PDU
 * signals the EC that the host intends to transfer a new image.
 *
 * The connection establishment response is described by the
 * first_response_pdu structure below.
 */

#define UPDATE_PROTOCOL_VERSION 6

/*
 * This is the format of the update PDU header.
 *
 * block digest: the first four bytes of the sha256 digest of the rest of the
 *               structure (can be 0 on boards where digest is ignored).
 * block_base:   offset of this PDU into the flash SPI.
 */
struct update_command {
	uint32_t block_digest;
	uint32_t block_base;
	/* The actual payload goes here. */
} __packed;

/*
 * This is the frame format the host uses when sending update PDUs over USB.
 *
 * The PDUs are up to 1K bytes in size, they are fragmented into USB chunks of
 * 64 bytes each and reassembled on the receive side before being passed to
 * the flash update function.
 *
 * The flash update function receives the unframed PDU body (starting at the
 * cmd field below), and puts its reply into the same buffer the PDU was in.
 */
struct update_frame_header {
	uint32_t block_size; /* Total frame size, including this field. */
	struct update_command cmd;
};

/*
 * A convenience structure which allows to group together various revision
 * fields of the header created by the signer (cr50-specific).
 *
 * These fields are compared when deciding if versions of two images are the
 * same or when deciding which one of the available images to run.
 */
struct signed_header_version {
	uint32_t minor;
	uint32_t major;
	uint32_t epoch;
};

/*
 * Response to the connection establishment request.
 *
 * When responding to the very first packet of the update sequence, the
 * original USB update implementation was responding with a four byte value,
 * just as to any other block of the transfer sequence.
 *
 * It became clear that there is a need to be able to enhance the update
 * protocol, while staying backwards compatible.
 *
 * All newer protocol versions (starting with version 2) respond to the very
 * first packet with an 8 byte or larger response, where the first 4 bytes are
 * a version specific data, and the second 4 bytes - the protocol version
 * number.
 *
 * This way the host receiving of a four byte value in response to the first
 * packet is considered an indication of the target running the 'legacy'
 * protocol, version 1. Receiving of an 8 byte or longer response would
 * communicates the protocol version in the second 4 bytes.
 */
struct first_response_pdu {
	uint32_t return_value;

	/* The below fields are present in versions 2 and up. */

	/* Type of header following (one of first_response_pdu_header_type) */
	uint16_t header_type;

	/* Must be UPDATE_PROTOCOL_VERSION */
	uint16_t protocol_version;

	/* In version 6 and up, a board-specific header follows. */
	union {
		/* cr50 (header_type = UPDATE_HEADER_TYPE_CR50) */
		struct {
			/* The below fields are present in versions 3 and up. */
			uint32_t backup_ro_offset;
			uint32_t backup_rw_offset;

			/* The below fields are present in versions 4 and up. */
			/*
			 * Versions of the currently active RO and RW sections.
			 */
			struct signed_header_version shv[2];

			/* The below fields are present in versions 5 and up */
			/* keyids of the currently active RO and RW sections. */
			uint32_t keyid[2];
		} cr50;
		/* Common code (header_type = UPDATE_HEADER_TYPE_COMMON) */
		struct {
			/* Maximum PDU size */
			uint32_t maximum_pdu_size;

			/* Flash protection status */
			uint32_t flash_protection;

			/* Offset of the other region */
			uint32_t offset;

			/* Version string of the other region */
			char version[32];

			/* Minimum rollback version that RO will accept */
			int32_t min_rollback;

			/* RO public key version */
			uint32_t key_version;
		} common;
	};
};

enum first_response_pdu_header_type {
	UPDATE_HEADER_TYPE_CR50 = 0, /* Must be 0 for backwards compatibility */
	UPDATE_HEADER_TYPE_COMMON = 1,
};

/* TODO: Handle this in update_fw.c, not usb_update.c */
#define UPDATE_DONE 0xB007AB1E
#define UPDATE_EXTRA_CMD 0xB007AB1F

enum update_extra_command {
	UPDATE_EXTRA_CMD_IMMEDIATE_RESET = 0,
	UPDATE_EXTRA_CMD_JUMP_TO_RW = 1,
	UPDATE_EXTRA_CMD_STAY_IN_RO = 2,
	UPDATE_EXTRA_CMD_UNLOCK_RW = 3,
	UPDATE_EXTRA_CMD_UNLOCK_ROLLBACK = 4,
	UPDATE_EXTRA_CMD_INJECT_ENTROPY = 5,
	UPDATE_EXTRA_CMD_PAIR_CHALLENGE = 6,
	UPDATE_EXTRA_CMD_TOUCHPAD_INFO = 7,
	UPDATE_EXTRA_CMD_TOUCHPAD_DEBUG = 8,
	UPDATE_EXTRA_CMD_CONSOLE_READ_INIT = 9,
	UPDATE_EXTRA_CMD_CONSOLE_READ_NEXT = 10,
	UPDATE_EXTRA_CMD_GET_VERSION_STRING = 11,
};

/*
 * Pair challenge (from host), note that the packet, with header, must fit
 * in a single USB packet (64 bytes), so its maximum length is 50 bytes.
 */
struct pair_challenge {
	uint8_t host_public[32]; /* X22519 public key from host */
	uint8_t nonce[16]; /* nonce to be used for HMAC */
};

/*
 * Pair challenge response (from device).
 */
struct pair_challenge_response {
	uint8_t status; /* = EC_RES_SUCCESS */
	uint8_t device_public[32]; /* X22519 device public key of device */
	/*
	 * Truncated output of
	 * HMAC_SHA256(x25519(device_private, host_public), nonce)
	 */
	uint8_t authenticator[16];
} __packed;

struct touchpad_info {
	uint8_t status; /* = EC_RES_SUCCESS */
	uint8_t reserved; /* padding */
	uint16_t vendor; /* Vendor USB id */

	/*
	 * Virtual address to write to to update TP FW over USB update protocol,
	 * and FW size. Both are 0 if unsupported.
	 */
	uint32_t fw_address;
	uint32_t fw_size;

	/*
	 * SHA256 hash of the trackpad FW accepted by this EC image.
	 * This is used by the updater to make sure we do not attempt to flash
	 * a touchpad FW that does not match the one shipped by the EC.
	 */
	uint8_t allowed_fw_hash[32];

	/* Vendor specific data. */
	union {
		struct {
			uint16_t id;
			uint16_t fw_version;
			uint16_t fw_checksum;
		} elan __packed;
		struct {
			uint16_t id;
			uint16_t fw_version;
			uint16_t fw_checksum;
		} st __packed;
	} __packed;
} __packed;

/*
 * The response message must not exceed 64 bytes.
 * And our protocol has a 14 bytes header.
 * So the size of `struct touchpad_info` must be less
 * than or equal to 50 bytes
 */
BUILD_ASSERT(sizeof(struct touchpad_info) <= 50);

void fw_update_command_handler(void *body, size_t cmd_size,
			       size_t *response_size);

/* Used to tell fw update the update ran successfully and is finished */
void fw_update_complete(void);

/* Verify integrity of the PDU received. */
int update_pdu_valid(struct update_command *cmd_body, size_t cmd_size);

/* Various update command return values. */
enum {
	UPDATE_SUCCESS = 0,
	UPDATE_BAD_ADDR = 1,
	UPDATE_ERASE_FAILURE = 2,
	UPDATE_DATA_ERROR = 3,
	UPDATE_WRITE_FAILURE = 4,
	UPDATE_VERIFY_ERROR = 5,
	UPDATE_GEN_ERROR = 6,
	UPDATE_MALLOC_ERROR = 7,
	UPDATE_ROLLBACK_ERROR = 8,
	UPDATE_RATE_LIMIT_ERROR = 9,
	UPDATE_RWSIG_BUSY = 10,
};

/* Obtain touchpad information */
int touchpad_get_info(struct touchpad_info *tp);

/* Touchpad FW update: Write a FW block. */
int touchpad_update_write(int offset, int size, const uint8_t *data);

/**
 * Touchpad debugging interface, called whenever UPDATE_EXTRA_CMD_TOUCHPAD_DEBUG
 * is received. Behaviour is touchpad-vendor dependent, with the following
 * restrictions: data must be allocated statically, and must not be larger than
 * 64 bytes.
 *
 * @param param        Data passed as parameter to command.
 * @param param_size   Number of bytes passed as parameter.
 * @param data         Data to write back to host, needs to be allocated
 *                     statically by touchpad handler.
 * @param data_size    Amount of data to write back to host (up to 64 bytes).
 *
 * @return EC_RES_SUCCESS on success, any other EC_RES_* status on error.
 */
int touchpad_debug(const uint8_t *param, unsigned int param_size,
		   uint8_t **data, unsigned int *data_size);

/* SHA256 hash of the touchpad firmware expected by this image. */
extern const uint8_t touchpad_fw_full_hash[32];

#ifdef __cplusplus
}
#endif

#endif /* ! __CROS_EC_UPDATE_FW_H */

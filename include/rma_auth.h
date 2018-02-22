/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* RMA challenge-response */

#ifndef __CROS_EC_RMA_AUTH_H
#define __CROS_EC_RMA_AUTH_H

#include <stdint.h>

#include "common.h"  /* For __packed. */

/* Current challenge protocol version */
#define RMA_CHALLENGE_VERSION 0

/* Getters and setters for version_key_id byte */
#define RMA_CHALLENGE_VKID_BYTE(version, keyid) \
	(((version) << 6) | ((keyid) & 0x3f))
#define RMA_CHALLENGE_GET_VERSION(vkidbyte) ((vkidbyte) >> 6)
#define RMA_CHALLENGE_GET_KEY_ID(vkidbyte) ((vkidbyte) & 0x3f)

struct __packed rma_challenge {
	/* Top 2 bits are protocol version; bottom 6 are server KeyID */
	uint8_t version_key_id;

	/* Ephemeral public key from device */
	uint8_t device_pub_key[32];

	/* Board ID (.type) */
	uint8_t board_id[4];

	/* Device ID */
	uint8_t device_id[8];
};

/* Size of encoded challenge and response, and buffer sizes to hold them */
#define RMA_CHALLENGE_CHARS 80
#define RMA_CHALLENGE_BUF_SIZE (RMA_CHALLENGE_CHARS + 1)

#define RMA_AUTHCODE_CHARS 8
#define RMA_AUTHCODE_BUF_SIZE (RMA_AUTHCODE_CHARS + 1)

/**
 * Create a new RMA challenge/response
 *
 * @return EC_SUCCESS, EC_ERROR_TIMEOUT if too soon since the last challenge,
 * or other non-zero error code.
 */
int rma_create_challenge(void);

/**
 * Get the current challenge string
 *
 * @return a pointer to the challenge string.  String will be empty if there
 * is no active challenge.
 */
const char *rma_get_challenge(void);

/**
 * Try a RMA authorization code
 *
 * @param code		Authorization code to try
 * @return EC_SUCCESS if the response was correct, or non-zero error code.
 */
int rma_try_authcode(const char *code);

#endif

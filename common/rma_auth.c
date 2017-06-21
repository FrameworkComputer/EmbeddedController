/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* RMA authorization challenge-response */

#include "common.h"
#include "base32.h"
#include "chip/g/board_id.h"
#include "curve25519.h"
#include "rma_auth.h"
#include "sha256.h"
#include "system.h"
#include "timer.h"
#include "util.h"

/* Minimum time since system boot or last challenge before making a new one */
#define CHALLENGE_INTERVAL (10 * SECOND)

/* Number of tries to properly enter auth code */
#define MAX_AUTHCODE_TRIES 3

/* Server public key and key ID */
static const uint8_t server_pub_key[32] = CONFIG_RMA_AUTH_SERVER_PUBLIC_KEY;
static const uint8_t server_key_id = CONFIG_RMA_AUTH_SERVER_KEY_ID;

static char challenge[RMA_CHALLENGE_BUF_SIZE];
static char authcode[RMA_AUTHCODE_BUF_SIZE];
static int tries_left;
static uint64_t last_challenge_time;

/**
 * Create a new RMA challenge/response
 *
 * @return EC_SUCCESS, EC_ERROR_TIMEOUT if too soon since the last challenge,
 * or other non-zero error code.
 */
int rma_create_challenge(void)
{
	uint8_t temp[32];	/* Private key or HMAC */
	uint8_t secret[32];
	struct rma_challenge c;
	struct board_id bid;
	uint8_t *device_id;
	uint8_t *cptr = (uint8_t *)&c;
	uint64_t t;

	/* Clear the current challenge and authcode, if any */
	memset(challenge, 0, sizeof(challenge));
	memset(authcode, 0, sizeof(authcode));

	/* Rate limit challenges */
	t = get_time().val;
	if (t - last_challenge_time < CHALLENGE_INTERVAL)
		return EC_ERROR_TIMEOUT;
	last_challenge_time = t;

	memset(&c, 0, sizeof(c));
	c.version_key_id = RMA_CHALLENGE_VKID_BYTE(
	    RMA_CHALLENGE_VERSION, server_key_id);

	if (read_board_id(&bid))
		return EC_ERROR_UNKNOWN;
	memcpy(c.board_id, &bid.type, sizeof(c.board_id));

	if (system_get_chip_unique_id(&device_id) != sizeof(c.device_id))
		return EC_ERROR_UNKNOWN;
	memcpy(c.device_id, device_id, sizeof(c.device_id));

	/* Calculate a new ephemeral key pair */
	X25519_keypair(c.device_pub_key, temp);

	/* Encode the challenge */
	if (base32_encode(challenge, sizeof(challenge), cptr, 8 * sizeof(c), 9))
		return EC_ERROR_UNKNOWN;

	/* Calculate the shared secret */
	X25519(secret, temp, server_pub_key);

	/*
	 * Auth code is a truncated HMAC of the ephemeral public key, BoardID,
	 * and DeviceID.  Those are all in the right order in the challenge
	 * struct, after the version/key id byte.
	 */
	hmac_SHA256(temp, secret, sizeof(secret), cptr + 1, sizeof(c) - 1);
	if (base32_encode(authcode, sizeof(authcode), temp,
			  RMA_AUTHCODE_CHARS * 5, 0))
		return EC_ERROR_UNKNOWN;

	tries_left = MAX_AUTHCODE_TRIES;
	return EC_SUCCESS;
}

const char *rma_get_challenge(void)
{
	return challenge;
}

int rma_try_authcode(const char *code)
{
	int rv = EC_ERROR_INVAL;

	/* Fail if out of tries */
	if (!tries_left)
		return EC_ERROR_ACCESS_DENIED;

	if (safe_memcmp(authcode, code, RMA_AUTHCODE_CHARS)) {
		/* Mismatch */
		tries_left--;
	} else {
		rv = EC_SUCCESS;
		tries_left = 0;
	}

	/* Clear challenge and response if out of tries */
	if (!tries_left) {
		memset(challenge, 0, sizeof(challenge));
		memset(authcode, 0, sizeof(authcode));
	}

	return rv;
}

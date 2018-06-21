/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test RMA auth challenge/response
 */

#include <endian.h>
#include <stdio.h>
#include "common.h"
#include "chip/g/board_id.h"
#include "curve25519.h"
#include "base32.h"
#include "sha256.h"
#include "rma_auth.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

/* Dummy implementations for testing */
static uint8_t dummy_board_id[4] = {'Z', 'Z', 'C', 'R'};
static uint8_t dummy_device_id[8] = {'T', 'H', 'X', 1, 1, 3, 8, 0xfe};
static int server_protocol_version = RMA_CHALLENGE_VERSION;
static uint8_t server_private_key[32] = RMA_TEST_SERVER_PRIVATE_KEY;
static int server_key_id = RMA_TEST_SERVER_KEY_ID;

void rand_bytes(void *buffer, size_t len)
{
	FILE *f = fopen("/dev/urandom", "rb");

	assert(f);
	fread(buffer, 1, len, f);
	fclose(f);
}

int read_board_id(struct board_id *id)
{
	memcpy(&id->type, dummy_board_id, sizeof(id->type));
	id->type_inv = ~id->type;
	id->flags = 0xFF00;
	return EC_SUCCESS;
}

int system_get_chip_unique_id(uint8_t **id)
{
	*id = dummy_device_id;
	return sizeof(dummy_device_id);
}

/**
 * Simulate the server side of a RMA challenge-response.
 *
 * @param out_auth_code		Buffer for generated authorization code
 *				(must be >= CR50_AUTH_CODE_CHARS + 1 chars)
 * @param challenge		Challenge from device
 * @return 0 if success, non-zero if error.
 */
int rma_server_side(char *out_auth_code, const char *challenge)
{
	int version, key_id;
	uint32_t device_id[2];
	uint8_t secret[32];
	uint8_t hmac[32];
	struct rma_challenge c;
	uint8_t *cptr = (uint8_t *)&c;

	/* Convert the challenge back into binary */
	if (base32_decode(cptr, 8 * sizeof(c), challenge, 9) != 8 * sizeof(c)) {
		printf("Error decoding challenge\n");
		return -1;
	}

	version = RMA_CHALLENGE_GET_VERSION(c.version_key_id);
	if (version != server_protocol_version) {
		printf("Unsupported challenge version %d\n", version);
		return -1;
	}

	key_id = RMA_CHALLENGE_GET_KEY_ID(c.version_key_id);

	printf("\nChallenge: %s\n", challenge);
	printf("  Version:      %d\n", version);
	printf("  Server KeyID: %d\n", key_id);
	printf("  BoardID:      %c%c%c%c\n",
	       isprint(c.board_id[0]) ? c.board_id[0] : '?',
	       isprint(c.board_id[1]) ? c.board_id[1] : '?',
	       isprint(c.board_id[2]) ? c.board_id[2] : '?',
	       isprint(c.board_id[3]) ? c.board_id[3] : '?');

	memcpy(device_id, c.device_id, sizeof(device_id));
	printf("  DeviceID:     0x%08x 0x%08x\n", device_id[0], device_id[1]);

	if (key_id != server_key_id) {
		printf("Unsupported KeyID %d\n", key_id);
		return -1;
	}

	/*
	 * Make sure the current user is authorized to reset this board.
	 *
	 * Since this is just a test, here we'll just make sure the BoardID
	 * and DeviceID match what we expected.
	 */
	if (memcmp(c.board_id, dummy_board_id, sizeof(c.board_id))) {
		printf("BoardID mismatch\n");
		return -1;
	}
	if (memcmp(c.device_id, dummy_device_id, sizeof(c.device_id))) {
		printf("DeviceID mismatch\n");
		return -1;
	}

	/* Calculate the shared secret */
	X25519(secret, server_private_key, c.device_pub_key);

	/*
	 * Auth code is a truncated HMAC of the ephemeral public key, BoardID,
	 * and DeviceID.
	 */
	hmac_SHA256(hmac, secret, sizeof(secret), cptr + 1, sizeof(c) - 1);
	if (base32_encode(out_auth_code, RMA_AUTHCODE_BUF_SIZE,
			  hmac, RMA_AUTHCODE_CHARS * 5, 0)) {
		printf("Error encoding auth code\n");
		return -1;
	}
	printf("Authcode: %s\n", out_auth_code);

	return 0;
};

#define FORCE_TIME(t) { ts.val = (t); force_time(ts); }

/*
 * rma_try_authcode expects a buffer that is at least RMA_AUTHCODE_CHARS long,
 * so copy the input string to a buffer before calling the function.
 */
static int rma_try_authcode_pad(const char *code)
{
	char authcode[RMA_AUTHCODE_BUF_SIZE];

	memset(authcode, 0, sizeof(authcode));
	strncpy(authcode, code, sizeof(authcode));

	return rma_try_authcode(authcode);
}

static int test_rma_auth(void)
{
	const char *challenge;
	char authcode[RMA_AUTHCODE_BUF_SIZE];
	timestamp_t ts;

	/* Test rate limiting */
	FORCE_TIME(9 * SECOND);
	TEST_ASSERT(rma_create_challenge() == EC_ERROR_TIMEOUT);
	TEST_ASSERT(rma_try_authcode_pad("Bad") == EC_ERROR_ACCESS_DENIED);
	TEST_ASSERT(strlen(rma_get_challenge()) == 0);

	FORCE_TIME(10 * SECOND);
	TEST_ASSERT(rma_create_challenge() == 0);
	TEST_ASSERT(strlen(rma_get_challenge()) == RMA_CHALLENGE_CHARS);

	/* Test using up tries */
	TEST_ASSERT(rma_try_authcode_pad("Bad") == EC_ERROR_INVAL);
	TEST_ASSERT(strlen(rma_get_challenge()) == RMA_CHALLENGE_CHARS);
	TEST_ASSERT(rma_try_authcode_pad("BadCodeZ") == EC_ERROR_INVAL);
	TEST_ASSERT(strlen(rma_get_challenge()) == RMA_CHALLENGE_CHARS);
	TEST_ASSERT(rma_try_authcode_pad("BadLongCode") == EC_ERROR_INVAL);
	/* Out of tries now */
	TEST_ASSERT(strlen(rma_get_challenge()) == 0);
	TEST_ASSERT(rma_try_authcode_pad("Bad") == EC_ERROR_ACCESS_DENIED);

	FORCE_TIME(19 * SECOND);
	TEST_ASSERT(rma_create_challenge() == EC_ERROR_TIMEOUT);
	TEST_ASSERT(strlen(rma_get_challenge()) == 0);

	FORCE_TIME(21 * SECOND);
	TEST_ASSERT(rma_create_challenge() == 0);
	challenge = rma_get_challenge();
	TEST_ASSERT(strlen(challenge) == RMA_CHALLENGE_CHARS);
	TEST_ASSERT(rma_server_side(authcode, challenge) == 0);
	TEST_ASSERT(rma_try_authcode(authcode) == EC_SUCCESS);

	/*
	 * Make sure the server-side checks for fields work.  That is, test
	 * our ability to test those fields...
	 */
	server_protocol_version++;
	TEST_ASSERT(rma_server_side(authcode, challenge) == -1);
	server_protocol_version--;

	server_key_id++;
	TEST_ASSERT(rma_server_side(authcode, challenge) == -1);
	server_key_id--;

	dummy_board_id[0]++;
	TEST_ASSERT(rma_server_side(authcode, challenge) == -1);
	dummy_board_id[0]--;

	dummy_device_id[0]++;
	TEST_ASSERT(rma_server_side(authcode, challenge) == -1);
	dummy_device_id[0]--;

	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_rma_auth);

	test_print_result();
}

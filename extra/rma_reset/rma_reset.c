/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define __packed __attribute__((packed))
#include "rma_auth.h"
#include "curve25519.h"
#include "sha256.h"
#include "base32.h"

#define SERVER_ADDRESS	"https://www.google.com/chromeos/partner/console/cr50reset/request"

/* Server public key and key ID */
static uint8_t server_pri_key[32] = RMA_TEST_SERVER_PRIVATE_KEY;
static uint8_t server_pub_key[32] = RMA_TEST_SERVER_PUBLIC_KEY;
static uint8_t server_key_id = RMA_TEST_SERVER_KEY_ID;
static uint8_t board_id[4] = {'Z', 'Z', 'C', 'R'};
static uint8_t device_id[8] = {'T', 'H', 'X', 1, 1, 3, 8, 0xfe};

static char challenge[RMA_CHALLENGE_BUF_SIZE];
static char authcode[RMA_AUTHCODE_BUF_SIZE];

void panic_assert_fail(const char *fname, int linenum);
void rand_bytes(void *buffer, size_t len);
int safe_memcmp(const void *s1, const void *s2, size_t size);

void panic_assert_fail(const char *fname, int linenum)
{
	printf("\nASSERTION FAILURE at %s:%d\n", fname, linenum);
}

int safe_memcmp(const void *s1, const void *s2, size_t size)
{
	const uint8_t *us1 = s1;
	const uint8_t *us2 = s2;
	int result = 0;

	if (size == 0)
		return 0;

	while (size--)
		result |= *us1++ ^ *us2++;

	return result != 0;
}

void rand_bytes(void *buffer, size_t len)
{
	int random_togo = 0;
	uint32_t buffer_index = 0;
	uint32_t random_value;
	uint8_t *buf = (uint8_t *) buffer;

	while (buffer_index < len) {
		if (!random_togo) {
			random_value = rand();
			random_togo = sizeof(random_value);
		}
		buf[buffer_index++] = random_value >>
			((random_togo-- - 1) * 8);
	}
}

int rma_create_challenge(void)
{
	uint8_t temp[32];   /* Private key or HMAC */
	uint8_t secret[32];
	struct rma_challenge c;
	uint8_t *cptr = (uint8_t *)&c;

	/* Clear the current challenge and authcode, if any */
	memset(challenge, 0, sizeof(challenge));
	memset(authcode, 0, sizeof(authcode));

	memset(&c, 0, sizeof(c));
	c.version_key_id = RMA_CHALLENGE_VKID_BYTE(
		RMA_CHALLENGE_VERSION, server_key_id);

	memcpy(c.board_id, board_id, sizeof(c.board_id));
	memcpy(c.device_id, device_id, sizeof(c.device_id));

	/* Calculate a new ephemeral key pair */
	X25519_keypair(c.device_pub_key, temp);

	/* Encode the challenge */
	if (base32_encode(challenge, sizeof(challenge), cptr, 8 * sizeof(c), 9))
		return 1;

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
		return 1;

	return 0;
}

int rma_try_authcode(const char *code)
{
	return safe_memcmp(authcode, code, RMA_AUTHCODE_CHARS);
}

static void print_params(void)
{
	int i;

	printf("\nBoard Id:\n");
	for (i = 0; i < 4; i++)
		printf("%c ", board_id[i]);

	printf("\n\nDevice Id:\n");
	for (i = 0; i < 3; i++)
		printf("%c ", device_id[i]);
	for (i = 3; i < 8; i++)
		printf("%02x ", device_id[i]);

	printf("\n\nServer Key Id:\n");
	printf("%02x", server_key_id);

	printf("\n\nServer Private Key:\n");
	for (i = 0; i < 32; i++)
		printf("%02x%c", server_pri_key[i], ((i + 1) % 8) ? ' ':'\n');

	printf("\nServer Public Key:\n");
	for (i = 0; i < 32; i++)
		printf("%02x%c", server_pub_key[i], ((i + 1) % 8) ? ' ':'\n');

	printf("\nChallenge:\n");
	for (i = 0; i < RMA_CHALLENGE_CHARS; i++) {
		printf("%c", challenge[i]);
		if (((i + 1) % 5) == 0)
			printf(" ");
		if (((i + 1) % 40) == 0)
			printf("\n");
	}

	printf("\nAuthorization Code:\n");
	for (i = 0; i < RMA_AUTHCODE_BUF_SIZE; i++)
		printf("%c", authcode[i]);

	printf("\n\nChallenge String:\n");
	printf("%s?challenge=", SERVER_ADDRESS);
	for (i = 0; i < RMA_CHALLENGE_CHARS; i++)
		printf("%c", challenge[i]);
	printf("&hwid=HWIDTEST2082\n");

	printf("\n");
}

int main(int argc, char **argv)
{
	char code[25];
	int ret;

	rma_create_challenge();
	print_params();

	do {
		printf("Enter Authorization Code: ");
		fgets(code, 25, stdin);
		ret = rma_try_authcode(code);
		if (ret != 0)
			printf("\n\nCode is invalid\n\n");
	} while (ret != 0);

	printf("Code Accepted\n");

	return 0;
}

/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ctype.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rma_auth.h"
#include "curve25519.h"
#include "sha256.h"
#include "base32.h"

#define SERVER_ADDRESS \
	"https://www.google.com/chromeos/partner/console/cr50reset/request"

/* Test server public and private keys */
#define RMA_TEST_SERVER_PUBLIC_KEY {                                   \
		0x03, 0xae, 0x2d, 0x2c, 0x06, 0x23, 0xe0, 0x73,         \
		0x0d, 0xd3, 0xb7, 0x92, 0xac, 0x54, 0xc5, 0xfd,	\
		0x7e, 0x9c, 0xf0, 0xa8, 0xeb, 0x7e, 0x2a, 0xb5,	\
		0xdb, 0xf4, 0x79, 0x5f, 0x8a, 0x0f, 0x28, 0x3f}
#define RMA_TEST_SERVER_PRIVATE_KEY {					\
		0x47, 0x3b, 0xa5, 0xdb, 0xc4, 0xbb, 0xd6, 0x77,         \
		0x20, 0xbd, 0xd8, 0xbd, 0xc8, 0x7a, 0xbb, 0x07,	\
		0x03, 0x79, 0xba, 0x7b, 0x52, 0x8c, 0xec, 0xb3,	\
		0x4d, 0xaa, 0x69, 0xf5, 0x65, 0xb4, 0x31, 0xad}
#define RMA_TEST_SERVER_KEY_ID 0x10

/* Server public key and key ID */
static uint8_t server_pri_key[32] = RMA_TEST_SERVER_PRIVATE_KEY;
static uint8_t server_pub_key[32] = RMA_TEST_SERVER_PUBLIC_KEY;
static uint8_t server_key_id = RMA_TEST_SERVER_KEY_ID;
static uint8_t board_id[4] = {'Z', 'Z', 'C', 'R'};
static uint8_t device_id[8] = {'T', 'H', 'X', 1, 1, 3, 8, 0xfe};
static uint8_t hw_id[20] = "TESTSAMUS1234";

static char challenge[RMA_CHALLENGE_BUF_SIZE];
static char authcode[RMA_AUTHCODE_BUF_SIZE];

static char *progname;
static char *short_opts = "c:k:b:d:a:w:th";
static const struct option long_opts[] = {
	/* name    hasarg *flag val */
	{"challenge",  1,   NULL, 'c'},
	{"key_id",     1,   NULL, 'k'},
	{"board_id",   1,   NULL, 'b'},
	{"device_id",  1,   NULL, 'd'},
	{"auth_code",  1,   NULL, 'a'},
	{"hw_id",      1,   NULL, 'w'},
	{"test",       0,   NULL, 't'},
	{"help",       0,   NULL, 'h'},
	{},
};

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

static int rma_server_side(const char *generated_challenge)
{
	int key_id, version;
	uint8_t secret[32];
	uint8_t hmac[32];
	struct rma_challenge c;
	uint8_t *cptr = (uint8_t *)&c;

	/* Convert the challenge back into binary */
	if (base32_decode(cptr, 8 * sizeof(c), generated_challenge, 9) !=
		8 * sizeof(c)) {
		printf("Error decoding challenge\n");
		return -1;
	}

	version = RMA_CHALLENGE_GET_VERSION(c.version_key_id);
	key_id = RMA_CHALLENGE_GET_KEY_ID(c.version_key_id);
	printf("Challenge:    %s\n", generated_challenge);
	printf("Version:      %d\n", version);
	printf("Server KeyID: %d\n", key_id);

	if (version != RMA_CHALLENGE_VERSION)
		printf("Unsupported challenge version %d\n", version);

	if (key_id != RMA_TEST_SERVER_KEY_ID)
		printf("Unsupported KeyID %d\n", key_id);

	/* Calculate the shared secret */
	X25519(secret, server_pri_key, c.device_pub_key);

	/*
	 * Auth code is a truncated HMAC of the ephemeral public key, BoardID,
	 * and DeviceID.
	 */
	hmac_SHA256(hmac, secret, sizeof(secret), cptr + 1, sizeof(c) - 1);
	if (base32_encode(authcode, RMA_AUTHCODE_BUF_SIZE,
			  hmac, RMA_AUTHCODE_CHARS * 5, 0)) {
		printf("Error encoding auth code\n");
		return -1;
	}
	printf("Authcode:     %s\n", authcode);

	return 0;
};

int rma_create_challenge(void)
{
	uint8_t temp[32];   /* Private key or HMAC */
	uint8_t secret[32];
	struct rma_challenge c;
	uint8_t *cptr = (uint8_t *)&c;
	uint32_t bid;

	/* Clear the current challenge and authcode, if any */
	memset(challenge, 0, sizeof(challenge));
	memset(authcode, 0, sizeof(authcode));

	memset(&c, 0, sizeof(c));
	c.version_key_id = RMA_CHALLENGE_VKID_BYTE(
		RMA_CHALLENGE_VERSION, server_key_id);

	memcpy(&bid, board_id, sizeof(bid));
	bid = be32toh(bid);
	memcpy(c.board_id, &bid, sizeof(c.board_id));

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

	{ /* For Testing only */
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
			printf("%02x%c", server_pri_key[i], ((i + 1) % 8)
								? ' ':'\n');

		printf("\nServer Public Key:\n");
		for (i = 0; i < 32; i++)
			printf("%02x%c", server_pub_key[i], ((i + 1) % 8)
								? ' ':'\n');

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
	}

	printf("\n\nChallenge String:\n");
	printf("%s?challenge=", SERVER_ADDRESS);
	for (i = 0; i < RMA_CHALLENGE_CHARS; i++)
		printf("%c", challenge[i]);
	printf("&hwid=%s\n", hw_id);

	printf("\n");
}

static void usage(void)
{
	printf("\nUsage: %s --key_id <arg> --board_id <arg> --device_id <arg>"
					"--hw_id <arg> | --auth_code <arg> | "
					"--challenge <arg>\n"
		"\n"
		"This is used to generate the cr50 or server responses for rma "
		"open.\n"
		"The cr50 side can be used to generate a challenge response "
		"and sends authoriztion code to reset device.\n"
		"The server side can generate an authcode from cr50's "
		"rma challenge.\n"
		"\n"
		"  -c,--challenge    The challenge generated by cr50\n"
		"  -k,--key_id       Index of the server private key\n"
		"  -b,--board_id     BoardID type field\n"
		"  -d,--device_id    Device-unique identifier\n"
		"  -a,--auth_code    Reset authorization code\n"
		"  -w,--hw_id        Hardware id\n"
		"  -h,--help         Show this message\n"
		"\n", progname);
}

static int atoh(char *v)
{
	char hn;
	char ln;

	hn = toupper(*v);
	ln = toupper(*(v + 1));

	hn -= (isdigit(hn) ? '0' : '7');
	ln -= (isdigit(ln) ? '0' : '7');

	if ((hn < 0 || hn > 0xf) || (ln < 0 || ln > 0xf))
		return 0;

	return (hn << 4) | ln;
}

static int set_server_key_id(char *id)
{
	/* verify length */
	if (strlen(id) != 2)
		return 1;

	/* verify digits */
	if (!isxdigit(*id) || !isxdigit(*(id+1)))
		return 1;

	server_key_id = atoh(id);

	return 0;
}

static int set_board_id(char *id)
{
	int i;

	/* verify length */
	if (strlen(id) != 8)
		return 1;

	/* verify digits */
	for (i = 0; i < 8; i++)
		if (!isxdigit(*(id + i)))
			return 1;

	for (i = 0; i < 4; i++)
		board_id[i] = atoh((id + (i*2)));

	return 0;
}

static int set_device_id(char *id)
{
	int i;

	/* verify length */
	if (strlen(id) != 16)
		return 1;

	for (i = 0; i < 16; i++)
		if (!isxdigit(*(id + i)))
			return 1;

	for (i = 0; i < 8; i++)
		device_id[i] = atoh((id + (i*2)));

	return 0;
}

static int set_hw_id(char *id)
{
	int i;
	int len;

	len = strlen(id);
	if (len > 20)
		len = 20;

	for (i = 0; i < 20; i++)
		hw_id[i] = *(id + i);

	return 0;
}

static int set_auth_code(char *code)
{
	int i;

	if (strlen(code) != 8)
		return 1;

	for (i = 0; i < 8; i++)
		authcode[i] = *(code + i);
	authcode[i] = 0;

	return 0;
}

int main(int argc, char **argv)
{
	int a_flag = 0;
	int k_flag = 0;
	int b_flag = 0;
	int d_flag = 0;
	int w_flag = 0;
	int t_flag = 0;
	int i;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	opterr = 0;
	while ((i = getopt_long(argc, argv, short_opts, long_opts, 0)) != -1) {
		switch (i) {
		case 't':
			t_flag = 1;
			break;
		case 'c':
			return rma_server_side(optarg);
		case 'k':
			if (set_server_key_id(optarg)) {
				printf("Malformed key id\n");
				return 1;
			}
			k_flag = 1;
			break;
		case 'b':
			if (set_board_id(optarg)) {
				printf("Malformed board id\n");
				return 1;
			}
			b_flag = 1;
			break;
		case 'd':
			if (set_device_id(optarg)) {
				printf("Malformed device id\n");
				return 1;
			}
			d_flag = 1;
			break;
		case 'a':
			if (set_auth_code(optarg)) {
				printf("Malformed authorization code\n");
				return 1;
			}
			a_flag = 1;
			break;
		case 'w':
			if (set_hw_id(optarg)) {
				printf("Malformed hardware id\n");
				return 1;
			}
			w_flag = 1;
			break;
		case 'h':
			usage();
			return 0;
		case 0:                         /* auto-handled option */
			break;
		case '?':
			if (optopt)
				printf("Unrecognized option: -%c\n", optopt);
			else
				printf("Unrecognized option: %s\n",
							argv[optind - 1]);
			break;
		case ':':
			printf("Missing argument to %s\n", argv[optind - 1]);
			break;
		default:
			printf("Internal error at %s:%d\n", __FILE__, __LINE__);
			return 1;
		}
	}

	if (a_flag) {
		FILE *acode;
		char verify_authcode[RMA_AUTHCODE_BUF_SIZE];
		int rv;

		acode = fopen("/tmp/authcode", "r");
		if (acode == NULL) {
			printf("Please generate challenge\n");
			return 1;
		}

		rv = fread(verify_authcode, 1, RMA_AUTHCODE_BUF_SIZE, acode);
		if (rv != RMA_AUTHCODE_BUF_SIZE) {
			printf("Error reading saved authcode\n");
			return 1;
		}
		if (strcmp(verify_authcode, authcode) == 0)
			printf("Code Accepted\n");
		else
			printf("Invalid Code\n");

	} else {
		if (!t_flag) { /* Use default values */
			if (!k_flag || !b_flag || !d_flag || !w_flag) {
				printf("server-side: Flag -c is mandatory\n");
				printf("cr50-side: Flags -k, -b, -d, and -w "
					"are mandatory\n");
				return 1;
			}
		}

		rma_create_challenge();

		{
			FILE *acode;

			acode = fopen("/tmp/authcode", "w");
			if (acode < 0)
				return 1;
			fwrite(authcode, 1, RMA_AUTHCODE_BUF_SIZE, acode);
			fclose(acode);
		}

		print_params();
	}

	return 0;
}

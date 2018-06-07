/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ctype.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rma_auth.h"
#include "curve25519.h"
#include "sha256.h"
#include "base32.h"

#define EC_COORDINATE_SZ 32
#define EC_PRIV_KEY_SZ 32
#define EC_P256_UNCOMPRESSED_PUB_KEY_SZ (EC_COORDINATE_SZ * 2 + 1)
#define EC_P256_COMPRESSED_PUB_KEY_SZ (EC_COORDINATE_SZ  + 1)

#define SERVER_ADDRESS \
	"https://www.google.com/chromeos/partner/console/cr50reset/request"

/* Test server keys for x25519 and p256 curves. */
static const uint8_t rma_test_server_x25519_public_key[] = {
	0x03, 0xae, 0x2d, 0x2c, 0x06, 0x23, 0xe0, 0x73,
	0x0d, 0xd3, 0xb7, 0x92, 0xac, 0x54, 0xc5, 0xfd,
	0x7e, 0x9c, 0xf0, 0xa8, 0xeb, 0x7e, 0x2a, 0xb5,
	0xdb, 0xf4, 0x79, 0x5f, 0x8a, 0x0f, 0x28, 0x3f
};

static const uint8_t rma_test_server_x25519_private_key[] = {
	0x47, 0x3b, 0xa5, 0xdb, 0xc4, 0xbb, 0xd6, 0x77,
	0x20, 0xbd, 0xd8, 0xbd, 0xc8, 0x7a, 0xbb, 0x07,
	0x03, 0x79, 0xba, 0x7b, 0x52, 0x8c, 0xec, 0xb3,
	0x4d, 0xaa, 0x69, 0xf5, 0x65, 0xb4, 0x31, 0xad
};

#define RMA_TEST_SERVER_X25519_KEY_ID 0x10
#define RMA_PROD_SERVER_X25519_KEY_ID 0

/*
 * P256 curve keys, generated using openssl as follows:
 *
 * openssl ecparam -name prime256v1 -genkey -out key.pem
 * openssl ec -in key.pem -text -noout
 */
static const uint8_t rma_test_server_p256_private_key[] = {
	0x54, 0xb0, 0x82, 0x92, 0x54, 0x92, 0xfc, 0x4a,
	0xa7, 0x6b, 0xea, 0x8f, 0x30, 0xcc, 0xf7, 0x3d,
	0xa2, 0xf6, 0xa7, 0xad, 0xf0, 0xec, 0x7d, 0xe9,
	0x26, 0x75, 0xd1, 0xec, 0xde, 0x20, 0x8f, 0x81
};

/*
 * P256 public key in full form, x and y coordinates with a single byte
 * prefix, 65 bytes total.
 */
static const uint8_t rma_test_server_p256_public_key[] = {
	0x04, 0xe7, 0xbe, 0x37, 0xaa, 0x68, 0xca, 0xcc,
	0x68, 0xf4, 0x8c, 0x56, 0x65, 0x5a, 0xcb, 0xf8,
	0xf4, 0x65, 0x3c, 0xd3, 0xc6, 0x1b, 0xae, 0xd6,
	0x51, 0x7a, 0xcc, 0x00, 0x8d, 0x59, 0x6d, 0x1b,
	0x0a, 0x66, 0xe8, 0x68, 0x5e, 0x6a, 0x82, 0x19,
	0x81, 0x76, 0x84, 0x92, 0x7f, 0x8d, 0xb2, 0xbe,
	0xf5, 0x39, 0x50, 0xd5, 0xfe, 0xee, 0x00, 0x67,
	0xcf, 0x40, 0x5f, 0x68, 0x12, 0x83, 0x4f, 0xa4,
	0x35
};

#define RMA_TEST_SERVER_P256_KEY_ID 0x20
#define RMA_PROD_SERVER_P256_KEY_ID 0x01

/* Default values which can change based on command line arguments. */
static uint8_t server_key_id = RMA_TEST_SERVER_X25519_KEY_ID;
static uint8_t board_id[4] = {'Z', 'Z', 'C', 'R'};
static uint8_t device_id[8] = {'T', 'H', 'X', 1, 1, 3, 8, 0xfe};
static uint8_t hw_id[20] = "TESTSAMUS1234";

static char challenge[RMA_CHALLENGE_BUF_SIZE];
static char authcode[RMA_AUTHCODE_BUF_SIZE];

static char *progname;
static char *short_opts = "a:b:c:d:hpk:tw:";
static const struct option long_opts[] = {
	/* name    hasarg *flag val */
	{"auth_code",  1,   NULL, 'a'},
	{"board_id",   1,   NULL, 'b'},
	{"challenge",  1,   NULL, 'c'},
	{"device_id",  1,   NULL, 'd'},
	{"help",       0,   NULL, 'h'},
	{"hw_id",      1,   NULL, 'w'},
	{"key_id",     1,   NULL, 'k'},
	{"p256",       0,   NULL, 'p'},
	{"test",       0,   NULL, 't'},
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
	RAND_bytes(buffer, len);
}

/*
 * Generate a p256 key pair and calculate the shared secret based on our
 * private key and the server public key.
 *
 * Return the X coordinate of the generated public key and the shared secret.
 *
 * @pub_key - the compressed public key without the prefix; by convention
 *	      between RMA client and server the generated pubic key would
 *	      always have prefix of 0x03, (the Y coordinate value is odd), so
 *	      it is omitted from the key blob, which allows to keep the blob
 *	      size at 32 bytes.
 * @secret_seed - the product of multiplying of the server point by our
 *	      private key, only the 32 bytes of X coordinate are returned.
 */
static void p256_key_and_secret_seed(uint8_t pub_key[32],
				     uint8_t secret_seed[32])
{
	const EC_GROUP *group;
	EC_KEY *key;
	EC_POINT *pub;
	EC_POINT *secret_point;
	uint8_t buf[EC_P256_UNCOMPRESSED_PUB_KEY_SZ];

	/* Prepare structures to operate on. */
	key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	group = EC_KEY_get0_group(key);
	pub = EC_POINT_new(group);

	/*
	 * We might have to try multiple times, until the Y coordinate is an
	 * odd value as required by convention.
	 */
	do {
		EC_KEY_generate_key(key);

		/* Extract public key into an octal array. */
		EC_POINT_point2oct(group, EC_KEY_get0_public_key(key),
				   POINT_CONVERSION_UNCOMPRESSED,
				   buf, sizeof(buf), NULL);

		/* If Y coordinate is an odd value, we are done. */
	} while (!(buf[sizeof(buf) - 1] & 1));

	/* Copy X coordinate out. */
	memcpy(pub_key, buf + 1, 32);

	/*
	 * We have our private key and the server's point coordinates (aka
	 * server public key). Let's multiply the coordinates by our private
	 * key to get the shared secret.
	 */

	/* Load raw public key into the point structure. */
	EC_POINT_oct2point(group, pub, rma_test_server_p256_public_key,
			   sizeof(rma_test_server_p256_public_key), NULL);

	secret_point = EC_POINT_new(group);

	/* Multiply server public key by our private key. */
	EC_POINT_mul(group, secret_point, 0, pub,
		     EC_KEY_get0_private_key(key), 0);

	/* Pull the result back into the octal buffer. */
	EC_POINT_point2oct(group, secret_point, POINT_CONVERSION_UNCOMPRESSED,
			   buf, sizeof(buf), NULL);

	/*
	 * Copy X coordinate into the output to use as the shared secret
	 * seed.
	 */
	memcpy(secret_seed, buf + 1, 32);

	/* release resources */
	EC_KEY_free(key);
	EC_POINT_free(pub);
	EC_POINT_free(secret_point);
}

/*
 * When imitating server side, calculate the secret value given the client's
 * compressed public key (X coordinate only with 0x03 prefix implied) and
 * knowing our (server) private key.
 *
 * @secret - array to return the X coordinate of the calculated point.
 * @raw_pub_key - X coordinate of the point calculated by the client, 0x03
 *		  prefix implied.
 */
static void p256_calculate_secret(uint8_t secret[32],
				  const uint8_t raw_pub_key[32])
{
	uint8_t raw_pub_key_x[EC_P256_COMPRESSED_PUB_KEY_SZ];
	EC_KEY *key;
	const uint8_t *kp = raw_pub_key_x;
	EC_POINT *secret_point;
	const EC_GROUP *group;
	BIGNUM *priv;
	uint8_t buf[EC_P256_UNCOMPRESSED_PUB_KEY_SZ];

	/* Express server private key as a BN. */
	priv = BN_new();
	BN_bin2bn(rma_test_server_p256_private_key, EC_PRIV_KEY_SZ, priv);

	/*
	 * Populate a p256 key structure based on the compressed
	 * representation of the client's public key.
	 */
	raw_pub_key_x[0] = 3; /* Implied by convention. */
	memcpy(raw_pub_key_x + 1, raw_pub_key, sizeof(raw_pub_key_x) - 1);
	key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	group = EC_KEY_get0_group(key);
	key = o2i_ECPublicKey(&key, &kp, sizeof(raw_pub_key_x));

	/* This is where the multiplication result will go. */
	secret_point = EC_POINT_new(group);

	/* Multiply client's point by our private key. */
	EC_POINT_mul(group, secret_point, 0,
		     EC_KEY_get0_public_key(key),
		     priv, 0);

	/* Pull the result back into the octal buffer. */
	EC_POINT_point2oct(group, secret_point, POINT_CONVERSION_UNCOMPRESSED,
			   buf, sizeof(buf), NULL);

	/* Copy X coordinate into the output to use as the shared secret. */
	memcpy(secret, buf + 1, 32);
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

	/* Calculate the shared secret, use curve based on the key ID. */
	switch (key_id) {
	case RMA_PROD_SERVER_X25519_KEY_ID:
		printf("Unsupported Prod KeyID %d\n", key_id);
	case RMA_TEST_SERVER_X25519_KEY_ID:
		X25519(secret, rma_test_server_x25519_private_key,
		       c.device_pub_key);
		break;
	case RMA_PROD_SERVER_P256_KEY_ID:
		printf("Unsupported Prod KeyID %d\n", key_id);
	case RMA_TEST_SERVER_P256_KEY_ID:
		p256_calculate_secret(secret, c.device_pub_key);
		break;
	default:
		printf("Unknown KeyID %d\n", key_id);
		return 1;
	}

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

static int rma_create_test_challenge(int p256_mode)
{
	uint8_t temp[32];   /* Private key or HMAC */
	uint8_t secret_seed[32];
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

	if (p256_mode) {
		p256_key_and_secret_seed(c.device_pub_key, secret_seed);
	} else {
		/* Calculate a new ephemeral key pair */
		X25519_keypair(c.device_pub_key, temp);
		/* Calculate the shared secret seed. */
		X25519(secret_seed, temp, rma_test_server_x25519_public_key);
	}

	/* Encode the challenge */
	if (base32_encode(challenge, sizeof(challenge), cptr, 8 * sizeof(c), 9))
		return 1;

	/*
	 * Auth code is a truncated HMAC of the ephemeral public key, BoardID,
	 * and DeviceID.  Those are all in the right order in the challenge
	 * struct, after the version/key id byte.
	 */
	hmac_SHA256(temp, secret_seed, sizeof(secret_seed),
		    cptr + 1, sizeof(c) - 1);
	if (base32_encode(authcode, sizeof(authcode), temp,
			  RMA_AUTHCODE_CHARS * 5, 0))
		return 1;

	return 0;
}

int rma_try_authcode(const char *code)
{
	return safe_memcmp(authcode, code, RMA_AUTHCODE_CHARS);
}

static void dump_key(const char *title, const uint8_t *key, size_t key_size)
{
	size_t i;
	const int bytes_per_line = 8;

	printf("\n\n\%s\n", title);
	for (i = 0; i < key_size; i++)
		printf("%02x%c", key[i], ((i + 1) % bytes_per_line) ? ' ':'\n');

	if (i % bytes_per_line)
		printf("\n");
}

static void print_params(int p_flag)
{
	int i;
	const uint8_t *priv_key;
	const uint8_t *pub_key;
	int key_id;
	size_t pub_key_size;

	printf("\nBoard Id:\n");
	for (i = 0; i < 4; i++)
		printf("%c ", board_id[i]);

	printf("\n\nDevice Id:\n");
	for (i = 0; i < 3; i++)
		printf("%c ", device_id[i]);
	for (i = 3; i < 8; i++)
		printf("%02x ", device_id[i]);

	if (p_flag) {
		priv_key = rma_test_server_p256_private_key;
		pub_key = rma_test_server_p256_public_key;
		pub_key_size = sizeof(rma_test_server_p256_public_key);
		key_id = RMA_TEST_SERVER_P256_KEY_ID;
	} else {
		priv_key = rma_test_server_x25519_private_key;
		pub_key = rma_test_server_x25519_public_key;
		pub_key_size = sizeof(rma_test_server_x25519_public_key);
		key_id = RMA_TEST_SERVER_X25519_KEY_ID;
	}

	printf("\n\nServer Key Id:\n");
	printf("%02x", key_id);

	/* Both private keys are of the same size */
	dump_key("Server Private Key:", priv_key, EC_PRIV_KEY_SZ);
	dump_key("Server Public Key:", pub_key, pub_key_size);

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
	printf("&hwid=%s\n", hw_id);

	printf("\n");
}

static void usage(void)
{
	printf("\nUsage: %s  [--p256] --key_id <arg> --board_id <arg> "
	       "--device_id <arg> --hw_id <arg> |\n"
	       "                           --auth_code <arg> |\n"
	       "                           --challenge <arg>\n"
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
		"  -p,--p256         Use prime256v1 curve instead of x25519\n"
		"  -t,--test         "
			"Generate challenge using default test inputs\n"
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
	int b_flag = 0;
	int d_flag = 0;
	int k_flag = 0;
	int p_flag = 0;
	int t_flag = 0;
	int w_flag = 0;
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
		case 'p':
			p_flag = 1;
			server_key_id = RMA_TEST_SERVER_P256_KEY_ID;
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

		rma_create_test_challenge(p_flag);

		{
			FILE *acode;

			acode = fopen("/tmp/authcode", "w");
			if (acode < 0)
				return 1;
			fwrite(authcode, 1, RMA_AUTHCODE_BUF_SIZE, acode);
			fclose(acode);
		}

		print_params(p_flag);
	}

	return 0;
}

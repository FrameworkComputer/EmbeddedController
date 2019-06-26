/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* RMA authorization challenge-response */

#include "common.h"
#include "base32.h"
#include "byteorder.h"
#include "ccd_config.h"
#include "chip/g/board_id.h"
#include "console.h"
#ifdef CONFIG_CURVE25519
#include "curve25519.h"
#endif
#include "extension.h"
#include "hooks.h"
#include "rma_auth.h"
#include "shared_mem.h"
#include "system.h"
#include "timer.h"
#include "tpm_registers.h"
#include "tpm_vendor_cmds.h"
#ifdef CONFIG_RMA_AUTH_USE_P256
#include "trng.h"
#endif
#include "util.h"

#ifndef TEST_BUILD
#include "cryptoc/util.h"
#include "rma_key_from_blob.h"
#else
/* Cryptoc library is not available to the test layer. */
#define always_memset memset
#endif

#ifdef CONFIG_DCRYPTO
#include "dcrypto.h"
#else
#include "sha256.h"
#endif

#define CPRINTF(format, args...) cprintf(CC_EXTENSION, format, ## args)

/* Minimum time since system boot or last challenge before making a new one */
#define CHALLENGE_INTERVAL (10 * SECOND)

/* Number of tries to properly enter auth code */
#define MAX_AUTHCODE_TRIES 3

#ifdef CONFIG_RMA_AUTH_USE_P256
#define RMA_SERVER_PUB_KEY_SZ 65
#else
#define RMA_SERVER_PUB_KEY_SZ 32
#endif

/* Server public key and key ID */
static const struct  {
	union {
		uint8_t raw_blob[RMA_SERVER_PUB_KEY_SZ + 1];
		struct {
			uint8_t server_pub_key[RMA_SERVER_PUB_KEY_SZ];
			volatile uint8_t server_key_id;
		};
	};
} __packed rma_key_blob = {
	.raw_blob = RMA_KEY_BLOB
};

BUILD_ASSERT(sizeof(rma_key_blob) == (RMA_SERVER_PUB_KEY_SZ + 1));

static char challenge[RMA_CHALLENGE_BUF_SIZE];
static char authcode[RMA_AUTHCODE_BUF_SIZE];
static int tries_left;
static uint64_t last_challenge_time;

static void get_hmac_sha256(void *hmac_out, const uint8_t *secret,
			    size_t secret_size, const void *ch_ptr,
			    size_t ch_size)
{
#ifdef CONFIG_DCRYPTO
	LITE_HMAC_CTX hmac;

	DCRYPTO_HMAC_SHA256_init(&hmac, secret, secret_size);
	HASH_update(&hmac.hash, ch_ptr, ch_size);
	memcpy(hmac_out, DCRYPTO_HMAC_final(&hmac), 32);
#else
	hmac_SHA256(hmac_out, secret, secret_size, ch_ptr, ch_size);
#endif
}

static void hash_buffer(void *dest, size_t dest_size,
			const void *buffer, size_t buf_size)
{
	/* We know that the destination is no larger than 32 bytes. */
	uint8_t temp[32];

	get_hmac_sha256(temp, buffer, buf_size, buffer, buf_size);

	/* Or should we do XOR of the temp modulo dest size? */
	memcpy(dest, temp, dest_size);
}

#ifdef CONFIG_RMA_AUTH_USE_P256
/*
 * Generate a p256 key pair, such that Y coordinate component of the public
 * key is an odd value. Use the X component value as the compressed public key
 * to be sent to the server. Multiply server public key by our private key to
 * generate the shared secret.
 *
 * @pub_key - array to return 32 bytes of the X coordinate public key
 *	      component.
 * @secet - array to return the X coordinate of the product of the server
 *            public key multiplied by our private key.
 */
static void p256_get_pub_key_and_secret(uint8_t pub_key[P256_NBYTES],
					uint8_t secret[P256_NBYTES])
{
	uint8_t buf[SHA256_DIGEST_SIZE];
	p256_int d;
	p256_int pk_x;
	p256_int pk_y;

	/* Get some noise for private key. */
	rand_bytes(buf, sizeof(buf));

	/*
	 * By convention with the RMA server the Y coordinate of the Cr50
	 * public key component is required to be an odd value. Keep trying
	 * until the genreated bublic key has the compliant Y coordinate.
	 */
	while (1) {
		HASH_CTX sha;

		if (DCRYPTO_p256_key_from_bytes(&pk_x, &pk_y, &d, buf)) {

			/* Is Y coordinate an odd value? */
			if (p256_is_odd(&pk_y))
				break; /* Yes it is, got a good key. */
		}

		/* Did not succeed, rehash the private key and try again. */
		DCRYPTO_SHA256_init(&sha, 0);
		HASH_update(&sha, buf, sizeof(buf));
		memcpy(buf, HASH_final(&sha), sizeof(buf));
	}

	/* X coordinate is passed to the server as the public key. */
	p256_to_bin(&pk_x, pub_key);

	/*
	 * Now let's calculate the secret as a the server pub key multiplied
	 * by our private key.
	 */
	p256_from_bin(rma_key_blob.raw_blob + 1, &pk_x);
	p256_from_bin(rma_key_blob.raw_blob + 1 + P256_NBYTES, &pk_y);

	/* Use input space for storing multiplication results. */
	DCRYPTO_p256_point_mul(&pk_x, &pk_y, &d, &pk_x, &pk_y);

	/* X value is the seed for the shared secret. */
	p256_to_bin(&pk_x, secret);

	/* Wipe out the private key just in case. */
	always_memset(&d, 0, sizeof(d));
}
#endif

void get_rma_device_id(uint8_t rma_device_id[RMA_DEVICE_ID_SIZE])
{
	uint8_t *chip_unique_id;
	int chip_unique_id_size = system_get_chip_unique_id(&chip_unique_id);

	if (chip_unique_id_size < 0)
		chip_unique_id_size = 0;
	/* Smaller unique chip IDs will fill rma_device_id only partially. */
	if (chip_unique_id_size <= RMA_DEVICE_ID_SIZE) {
		/* The size matches, let's just copy it as is. */
		memcpy(rma_device_id, chip_unique_id, chip_unique_id_size);
		if (chip_unique_id_size < RMA_DEVICE_ID_SIZE) {
			memset(rma_device_id + chip_unique_id_size, 0,
				RMA_DEVICE_ID_SIZE - chip_unique_id_size);
		}
	} else {
		/*
		 * The unique chip ID size exceeds space allotted in
		 * rma_challenge:device_id, let's use first few bytes of
		 * its hash.
		 */
		hash_buffer(rma_device_id, RMA_DEVICE_ID_SIZE,
			    chip_unique_id, chip_unique_id_size);
	}
}

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
	    RMA_CHALLENGE_VERSION, rma_key_blob.server_key_id);

	if (read_board_id(&bid))
		return EC_ERROR_UNKNOWN;

	memcpy(c.board_id, &bid.type, sizeof(c.board_id));
	get_rma_device_id(c.device_id);

	/* Calculate a new ephemeral key pair and the shared secret. */
#ifdef CONFIG_RMA_AUTH_USE_P256
	p256_get_pub_key_and_secret(c.device_pub_key, secret);
#endif
#ifdef CONFIG_CURVE25519
	X25519_keypair(c.device_pub_key, temp);
	X25519(secret, temp, rma_key_blob.server_pub_key);
#endif
	/* Encode the challenge */
	if (base32_encode(challenge, sizeof(challenge), cptr, 8 * sizeof(c), 9))
		return EC_ERROR_UNKNOWN;


	/*
	 * Auth code is a truncated HMAC of the ephemeral public key, BoardID,
	 * and DeviceID.  Those are all in the right order in the challenge
	 * struct, after the version/key id byte.
	 */
	get_hmac_sha256(temp, secret, sizeof(secret), cptr + 1, sizeof(c) - 1);
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

	/* Fail if auth code has not been calculated yet. */
	if (!*authcode)
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

#ifndef TEST_BUILD
/*
 * Trigger generating of the new challenge/authcode pair. If successful, store
 * the challenge in the vendor command response buffer and send it to the
 * sender. If not successful - return the error value to the sender.
 */
static enum vendor_cmd_rc get_challenge(uint8_t *buf, size_t *buf_size)
{
	int rv;
	size_t i;

	if (*buf_size < sizeof(challenge)) {
		*buf_size = 1;
		buf[0] = VENDOR_RC_RESPONSE_TOO_BIG;
		return buf[0];
	}

	rv = rma_create_challenge();
	if (rv != EC_SUCCESS) {
		*buf_size = 1;
		buf[0] = rv;
		return buf[0];
	}

	*buf_size = sizeof(challenge) - 1;
	memcpy(buf, rma_get_challenge(), *buf_size);


	CPRINTF("generated challenge:\n\n");
	for (i = 0; i < *buf_size; i++)
		CPRINTF("%c", ((uint8_t *)buf)[i]);
	CPRINTF("\n\n");

#ifdef CR50_DEV

	CPRINTF("expected authcode: ");
	for (i = 0; i < RMA_AUTHCODE_CHARS; i++)
		CPRINTF("%c", authcode[i]);
	CPRINTF("\n");
#endif
	return VENDOR_RC_SUCCESS;
}
/*
 * Compare response sent by the operator with the pre-compiled auth code.
 * Return error code or success depending on the comparison results.
 */
static enum vendor_cmd_rc process_response(uint8_t *buf,
					   size_t input_size,
					   size_t *response_size)
{
	int rv;

	*response_size = 1; /* Just in case there is an error. */

	if (input_size != RMA_AUTHCODE_CHARS) {
		CPRINTF("%s: authcode size %d\n",
			__func__, input_size);
		buf[0] = VENDOR_RC_BOGUS_ARGS;
		return buf[0];
	}

	rv = rma_try_authcode(buf);

	if (rv == EC_SUCCESS) {
		CPRINTF("%s: success!\n", __func__);
		*response_size = 0;
		enable_ccd_factory_mode(0);
		return VENDOR_RC_SUCCESS;
	}

	CPRINTF("%s: authcode mismatch\n", __func__);
	buf[0] = VENDOR_RC_INTERNAL_ERROR;
	return buf[0];
}

/*
 * Handle the VENDOR_CC_RMA_CHALLENGE_RESPONSE command. When received with
 * empty payload - this is a request to generate a new challenge, when
 * received with a payload, this is a request to check if the payload matches
 * the previously calculated auth code.
 */
static enum vendor_cmd_rc rma_challenge_response(enum vendor_cmd_cc code,
						 void *buf,
						 size_t input_size,
						 size_t *response_size)
{
	if (!input_size)
		/*
		 * This is a request for the challenge, get it and send it
		 * back.
		 */
		return get_challenge(buf, response_size);

	return process_response(buf, input_size, response_size);
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_RMA_CHALLENGE_RESPONSE,
		       rma_challenge_response);


#define RMA_CMD_BUF_SIZE (sizeof(struct tpm_cmd_header) + \
			  RMA_CHALLENGE_BUF_SIZE)
static int rma_auth_cmd(int argc, char **argv)
{
	struct tpm_cmd_header *tpmh;
	int rv;

	if (argc > 2) {
		ccprintf("Error: the only accepted parameter is"
			 " the auth code to check\n");
		return EC_ERROR_PARAM_COUNT;
	}

	rv = shared_mem_acquire(RMA_CMD_BUF_SIZE, (char **)&tpmh);
	if (rv != EC_SUCCESS)
		return rv;

	/* Common fields of the RMA AUTH challenge/response vendor command. */
	tpmh->tag = htobe16(0x8001); /* TPM_ST_NO_SESSIONS */
	tpmh->command_code = htobe32(TPM_CC_VENDOR_BIT_MASK);
	tpmh->subcommand_code = htobe16(VENDOR_CC_RMA_CHALLENGE_RESPONSE);

	if (argc == 2) {
		/*
		 * The user entered a value, must be the auth code, build and
		 * send vendor command to check it.
		 */
		const char *authcode = argv[1];

		if (strlen(authcode) != RMA_AUTHCODE_CHARS) {
			ccprintf("Wrong auth code size.\n");
			return EC_ERROR_PARAM1;
		}

		tpmh->size = htobe32(sizeof(struct tpm_cmd_header) +
				     RMA_AUTHCODE_CHARS);

		memcpy(tpmh + 1, authcode, RMA_AUTHCODE_CHARS);

		tpm_alt_extension(tpmh, RMA_CMD_BUF_SIZE);

		if (tpmh->command_code) {
			ccprintf("Auth code does not match.\n");
			return EC_ERROR_PARAM1;
		}
		ccprintf("Auth code match, reboot might be coming!\n");
		return EC_SUCCESS;
	}

	/* Prepare and send the request to get RMA auth challenge. */
	tpmh->size = htobe32(sizeof(struct tpm_cmd_header));
	tpm_alt_extension(tpmh, RMA_CMD_BUF_SIZE);

	/* Return status in the command code field now. */
	if (tpmh->command_code) {
		ccprintf("RMA Auth error 0x%x\n", be32toh(tpmh->command_code));
		rv = EC_ERROR_UNKNOWN;
	}

	shared_mem_release(tpmh);
	return EC_SUCCESS;
}

DECLARE_SAFE_CONSOLE_COMMAND(rma_auth, rma_auth_cmd, NULL,
			     "rma_auth [auth code] - "
			"Generate RMA challenge or check auth code match\n");
#endif

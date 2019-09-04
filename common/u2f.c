/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* APDU dispatcher and U2F command handlers. */

#include "console.h"
#include "cryptoc/p256.h"
#include "cryptoc/sha256.h"
#include "dcrypto.h"
#include "extension.h"
#include "system.h"
#include "u2f_impl.h"
#include "u2f.h"
#include "util.h"

#define G2F_CERT_NAME "CrOS"

#define CPRINTF(format, args...) cprintf(CC_EXTENSION, format, ##args)

/* Crypto parameters */
#define AES_BLOCK_LEN 16
#define KH_LEN 64

/* De-interleave 64 bytes into two 32 arrays. */
static void deinterleave64(const uint8_t *in, uint8_t *a, uint8_t *b)
{
	size_t i;

	for (i = 0; i < 32; ++i) {
		a[i] = in[2 * i + 0];
		b[i] = in[2 * i + 1];
	}
}

/* (un)wrap w/ the origin dependent KEK. */
static int wrap_kh(const uint8_t *origin, const uint8_t *in,
		   uint8_t *out, enum encrypt_mode mode)
{
	uint8_t kek[SHA256_DIGEST_SIZE];
	uint8_t iv[AES_BLOCK_LEN] = {0};
	int i;

	/* KEK derivation */
	if (u2f_gen_kek(origin, kek, sizeof(kek)))
		return EC_ERROR_UNKNOWN;

	DCRYPTO_aes_init(kek, 256, iv, CIPHER_MODE_CBC, mode);

	for (i = 0; i < 4; i++)
		DCRYPTO_aes_block(in + i * AES_BLOCK_LEN,
				  out + i * AES_BLOCK_LEN);

	return EC_SUCCESS;
}

static int individual_cert(const p256_int *d, const p256_int *pk_x,
			   const p256_int *pk_y,  uint8_t *cert, const int n)
{
	p256_int *serial;

	if (system_get_chip_unique_id((uint8_t **)&serial) != P256_NBYTES)
		return 0;

	return DCRYPTO_x509_gen_u2f_cert_name(d, pk_x, pk_y, serial,
					      G2F_CERT_NAME, cert, n);
}

int g2f_attestation_cert(uint8_t *buf)
{
	p256_int d, pk_x, pk_y;

	if (!use_g2f())
		return 0;

	if (g2f_individual_keypair(&d, &pk_x, &pk_y))
		return 0;

	/* Note that max length is not currently respected here. */
	return individual_cert(&d, &pk_x, &pk_y,
			       buf, G2F_ATTESTATION_CERT_MAX_LEN);
}

/* U2F GENERATE command  */
static enum vendor_cmd_rc u2f_generate(enum vendor_cmd_cc code,
				       void *buf,
				       size_t input_size,
				       size_t *response_size)
{
	U2F_GENERATE_REQ *req = buf;
	U2F_GENERATE_RESP *resp;

	/* Origin keypair */
	uint8_t od_seed[P256_NBYTES];
	p256_int od, opk_x, opk_y;

	/* Key handle */
	uint8_t kh[U2F_FIXED_KH_SIZE];

	/* Whether keypair generation succeeded */
	int generate_keypair_rc;

	size_t response_buf_size = *response_size;

	*response_size = 0;

	if (input_size != sizeof(U2F_GENERATE_REQ) ||
	    response_buf_size < sizeof(U2F_GENERATE_RESP))
		return VENDOR_RC_BOGUS_ARGS;

	/* Maybe enforce user presence, w/ optional consume */
	if (pop_check_presence(req->flags & G2F_CONSUME) != POP_TOUCH_YES &&
	    (req->flags & U2F_AUTH_FLAG_TUP) != 0)
		return VENDOR_RC_NOT_ALLOWED;

	/* Generate origin-specific keypair */
	do {
		if (!DCRYPTO_ladder_random(&od_seed))
			return VENDOR_RC_INTERNAL_ERROR;

		if (u2f_origin_user_keyhandle(req->appId, req->userSecret,
					      od_seed, kh) != EC_SUCCESS)
			return VENDOR_RC_INTERNAL_ERROR;

		generate_keypair_rc =
			u2f_origin_user_keypair(kh, &od, &opk_x, &opk_y);
	} while (generate_keypair_rc == EC_ERROR_TRY_AGAIN);

	if (generate_keypair_rc != EC_SUCCESS)
		return VENDOR_RC_INTERNAL_ERROR;

	/*
	 * From this point: the request 'req' content is invalid as it is
	 * overridden by the response we are building in the same buffer.
	 */
	resp = buf;

	*response_size = sizeof(*resp);

	/* Insert origin-specific public keys into the response */
	p256_to_bin(&opk_x, resp->pubKey.x); /* endianness */
	p256_to_bin(&opk_y, resp->pubKey.y); /* endianness */

	resp->pubKey.pointFormat = U2F_POINT_UNCOMPRESSED;

	/* Copy key handle to response. */
	memcpy(resp->keyHandle, kh, sizeof(kh));

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_U2F_GENERATE, u2f_generate);

static int verify_kh_owned(const uint8_t *user_secret, const uint8_t *app_id,
			   const uint8_t *key_handle, int *owned)
{
	int rc;
	/* Re-created key handle. */
	uint8_t recreated_kh[KH_LEN];

	/*
	 * Re-create the key handle and compare against that which
	 * was provided. This allows us to verify that the key handle
	 * is owned by this combination of device, current user and app_id.
	 */

	rc = u2f_origin_user_keyhandle(app_id, user_secret, key_handle,
				       recreated_kh);

	if (rc == EC_SUCCESS)
		*owned = safe_memcmp(recreated_kh, key_handle, KH_LEN) == 0;

	return rc;
}

static int verify_legacy_kh_owned(const uint8_t *app_id,
				  const uint8_t *key_handle,
				  uint8_t *origin_seed)
{
	uint8_t unwrapped_kh[KH_LEN];
	uint8_t kh_app_id[U2F_APPID_SIZE];

	p256_int app_id_p256;
	p256_int kh_app_id_p256;

	/* Unwrap key handle */
	if (wrap_kh(app_id, key_handle, unwrapped_kh, DECRYPT_MODE))
		return 0;
	deinterleave64(unwrapped_kh, kh_app_id, origin_seed);

	/* Return whether appId (i.e. origin) matches. */
	p256_from_bin(app_id, &app_id_p256);
	p256_from_bin(kh_app_id, &kh_app_id_p256);
	return p256_cmp(&app_id_p256, &kh_app_id_p256) == 0;
}

/* Below, we depend on the response not being larger than than the request. */
BUILD_ASSERT(sizeof(U2F_SIGN_RESP) <= sizeof(U2F_SIGN_REQ));

/* U2F SIGN command */
static enum vendor_cmd_rc u2f_sign(enum vendor_cmd_cc code,
				   void *buf,
				   size_t input_size,
				   size_t *response_size)
{
	const U2F_SIGN_REQ *req = buf;
	U2F_SIGN_RESP *resp;

	struct drbg_ctx ctx;

	/* Whether the key handle is owned by this device. */
	int kh_owned;

	/* Origin private key. */
	uint8_t legacy_origin_seed[SHA256_DIGEST_SIZE];
	p256_int origin_d;

	/* Hash, and corresponding signature. */
	p256_int h, r, s;

	/* Whether the key handle uses the legacy key derivation scheme. */
	int legacy_kh = 0;

	/* Response is smaller than request, so no need to check this. */
	*response_size = 0;

	if (input_size != sizeof(U2F_SIGN_REQ))
		return VENDOR_RC_BOGUS_ARGS;

	if (verify_kh_owned(req->userSecret, req->appId, req->keyHandle,
			    &kh_owned) != EC_SUCCESS)
		return VENDOR_RC_INTERNAL_ERROR;

	if (!kh_owned) {
		if ((req->flags & SIGN_LEGACY_KH) == 0)
			return VENDOR_RC_PASSWORD_REQUIRED;

		/*
		 * We have a key handle which is not valid for the new scheme,
		 * but may be a valid legacy key handle, and we have been asked
		 * to sign legacy key handles.
		 */
		if (verify_legacy_kh_owned(req->appId, req->keyHandle,
					   legacy_origin_seed))
			legacy_kh = 1;
		else
			return VENDOR_RC_PASSWORD_REQUIRED;
	}

	/* We might not actually need to sign anything. */
	if (req->flags == U2F_AUTH_CHECK_ONLY)
		return VENDOR_RC_SUCCESS;

	/* Always enforce user presence, with optional consume. */
	if (pop_check_presence(req->flags & G2F_CONSUME) != POP_TOUCH_YES)
		return VENDOR_RC_NOT_ALLOWED;

	/* Re-create origin-specific key. */
	if (legacy_kh) {
		if (u2f_origin_key(legacy_origin_seed, &origin_d) != EC_SUCCESS)
			return VENDOR_RC_INTERNAL_ERROR;
	} else {
		if (u2f_origin_user_keypair(req->keyHandle, &origin_d, NULL,
					    NULL) != EC_SUCCESS)
			return VENDOR_RC_INTERNAL_ERROR;
	}

	/* Prepare hash to sign. */
	p256_from_bin(req->hash, &h);

	/* Sign. */
	hmac_drbg_init_rfc6979(&ctx, &origin_d, &h);
	if (!dcrypto_p256_ecdsa_sign(&ctx, &origin_d, &h, &r, &s)) {
		p256_clear(&origin_d);
		return VENDOR_RC_INTERNAL_ERROR;
	}
	p256_clear(&origin_d);

	/*
	 * From this point: the request 'req' content is invalid as it is
	 * overridden by the response we are building in the same buffer.
	 * The response is smaller than the request, so we have the space.
	 */
	resp = buf;

	*response_size = sizeof(*resp);

	p256_to_bin(&r, resp->sig_r);
	p256_to_bin(&s, resp->sig_s);

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_U2F_SIGN, u2f_sign);

struct G2F_REGISTER_MSG {
	uint8_t reserved;
	uint8_t app_id[U2F_APPID_SIZE];
	uint8_t challenge[U2F_CHAL_SIZE];
	uint8_t key_handle[U2F_APPID_SIZE + sizeof(p256_int)];
	U2F_EC_POINT public_key;
};

static inline int u2f_attest_verify_reg_resp(const uint8_t *user_secret,
					     uint8_t data_size,
					     const uint8_t *data)
{
	struct G2F_REGISTER_MSG *msg = (void *) data;
	int kh_owned;

	if (data_size != sizeof(struct G2F_REGISTER_MSG))
		return VENDOR_RC_NOT_ALLOWED;

	if (verify_kh_owned(user_secret, msg->app_id, msg->key_handle,
			    &kh_owned) != EC_SUCCESS)
		return VENDOR_RC_INTERNAL_ERROR;

	if (!kh_owned)
		return VENDOR_RC_NOT_ALLOWED;

	return VENDOR_RC_SUCCESS;
}

static int u2f_attest_verify(const uint8_t *user_secret,
			     uint8_t format,
			     uint8_t data_size,
			     const uint8_t *data)
{
	switch (format) {
	case U2F_ATTEST_FORMAT_REG_RESP:
		return u2f_attest_verify_reg_resp(user_secret, data_size, data);
	default:
		return VENDOR_RC_NOT_ALLOWED;
	}
}

static inline size_t u2f_attest_format_size(uint8_t format)
{
	switch (format) {
	case U2F_ATTEST_FORMAT_REG_RESP:
		return sizeof(struct G2F_REGISTER_MSG);
	default:
		return 0;
	}
}

/* U2F ATTEST command */
static enum vendor_cmd_rc u2f_attest(enum vendor_cmd_cc code,
				     void *buf,
				     size_t input_size,
				     size_t *response_size)
{
	const U2F_ATTEST_REQ *req = buf;
	U2F_ATTEST_RESP *resp;

	int verify_ret;

	HASH_CTX h_ctx;
	struct drbg_ctx dr_ctx;

	/* Data hash, and corresponding signature. */
	p256_int h, r, s;

	/* Attestation key */
	p256_int d, pk_x, pk_y;

	size_t response_buf_size = *response_size;

	*response_size = 0;

	if (input_size < 2 ||
	    input_size < (2 + req->dataLen) ||
	    input_size > sizeof(U2F_ATTEST_REQ) ||
	    response_buf_size < sizeof(*resp))
		return VENDOR_RC_BOGUS_ARGS;

	verify_ret = u2f_attest_verify(req->userSecret,
				       req->format,
				       req->dataLen,
				       req->data);

	if (verify_ret != VENDOR_RC_SUCCESS)
		return verify_ret;

	/* Message signature */
	DCRYPTO_SHA256_init(&h_ctx, 0);
	HASH_update(&h_ctx, req->data, u2f_attest_format_size(req->format));
	p256_from_bin(HASH_final(&h_ctx), &h);

	/* Derive G2F Attestation Key */
	if (g2f_individual_keypair(&d, &pk_x, &pk_y)) {
		CPRINTF("G2F Attestation key generation failed");
		return VENDOR_RC_INTERNAL_ERROR;
	}

	/* Sign over the response w/ the attestation key */
	hmac_drbg_init_rfc6979(&dr_ctx, &d, &h);
	if (!dcrypto_p256_ecdsa_sign(&dr_ctx, &d, &h, &r, &s)) {
		CPRINTF("Signing error");
		return VENDOR_RC_INTERNAL_ERROR;
	}
	p256_clear(&d);

	/*
	 * From this point: the request 'req' content is invalid as it is
	 * overridden by the response we are building in the same buffer.
	 * The response is smaller than the request, so we have the space.
	 */
	resp = buf;

	*response_size = sizeof(*resp);

	p256_to_bin(&r, resp->sig_r);
	p256_to_bin(&s, resp->sig_s);

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_U2F_ATTEST, u2f_attest);

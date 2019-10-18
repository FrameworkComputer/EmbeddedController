/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helpers to emulate a U2F HID dongle over the TPM transport */

#include "console.h"
#include "dcrypto.h"
#include "extension.h"
#include "nvmem_vars.h"
#include "rbox.h"
#include "registers.h"
#include "signed_header.h"
#include "system.h"
#include "tpm_nvmem_ops.h"
#include "tpm_vendor_cmds.h"
#include "u2f.h"
#include "u2f_impl.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_EXTENSION, format, ## args)

/* ---- physical presence (using the laptop power button) ---- */

static timestamp_t last_press;

/* how long do we keep the last button press as valid presence */
#define PRESENCE_TIMEOUT (10 * SECOND)

void power_button_record(void)
{
	if (ap_is_on() && rbox_powerbtn_is_pressed()) {
		last_press = get_time();
#ifdef CR50_DEV
		CPRINTS("record pp");
#endif
	}
}

enum touch_state pop_check_presence(int consume)
{
	int recent = ((last_press.val  > 0) &&
		((get_time().val - last_press.val) < PRESENCE_TIMEOUT));

#ifdef CR50_DEV
	if (recent)
		CPRINTS("User presence: consumed %d", consume);
#endif
	if (consume)
		last_press.val = 0;

	/* user physical presence on the power button */
	return recent ? POP_TOUCH_YES : POP_TOUCH_NO;
}

/* ---- non-volatile U2F state ---- */

struct u2f_state {
	uint32_t salt[8];
	uint32_t salt_kek[8];
	uint32_t salt_kh[8];
};

static const uint8_t k_salt = NVMEM_VAR_G2F_SALT;
static const uint8_t k_salt_deprecated = NVMEM_VAR_U2F_SALT;

static int load_state(struct u2f_state *state)
{
	const struct tuple *t_salt = getvar(&k_salt, sizeof(k_salt));

	if (!t_salt) {
		/* Delete the old salt if present, no-op if not. */
		if (setvar(&k_salt_deprecated, sizeof(k_salt_deprecated),
			   NULL, 0))
			return 0;

		/* create random salt */
		if (!DCRYPTO_ladder_random(state->salt))
			return 0;
		if (setvar(&k_salt, sizeof(k_salt),
			   (const uint8_t *)state->salt, sizeof(state->salt)))
			return 0;
	} else {
		memcpy(state->salt, tuple_val(t_salt), sizeof(state->salt));
		freevar(t_salt);
	}

	if (read_tpm_nvmem_hidden(TPM_HIDDEN_U2F_KEK, sizeof(state->salt_kek),
				  state->salt_kek) == tpm_read_not_found) {
		/*
		 * Not found means that we have not used u2f before,
		 * or not used it with updated fw that resets kek seed
		 * on TPM clear.
		 */
		if (t_salt) { /* Note that memory has been freed already!. */
			/*
			 * We have previously used u2f, and may have
			 * existing registrations; we don't want to
			 * invalidate these, so preserve the existing
			 * seed as a one-off. It will be changed on
			 * next TPM clear.
			 */
			memcpy(state->salt_kek, state->salt,
			       sizeof(state->salt_kek));
		} else {
			/*
			 * We have never used u2f before - generate
			 * new seed.
			 */
			if (!DCRYPTO_ladder_random(state->salt_kek))
				return 0;
		}
		if (write_tpm_nvmem_hidden(TPM_HIDDEN_U2F_KEK,
					   sizeof(state->salt_kek),
					   state->salt_kek,
					   1 /* commit */) != tpm_write_created)
			return 0;
	}

	if (read_tpm_nvmem_hidden(TPM_HIDDEN_U2F_KH_SALT,
				  sizeof(state->salt_kh),
				  state->salt_kh) == tpm_read_not_found) {
		/*
		 * We have never used u2f before - generate
		 * new seed.
		 */
		if (!DCRYPTO_ladder_random(state->salt_kh))
			return 0;

		if (write_tpm_nvmem_hidden(TPM_HIDDEN_U2F_KH_SALT,
					   sizeof(state->salt_kh),
					   state->salt_kh,
					   1 /* commit */) != tpm_write_created)
			return 0;
	}

	return 1;
}

static struct u2f_state *get_state(void)
{
	static int state_loaded;
	static struct u2f_state state;

	if (!state_loaded)
		state_loaded = load_state(&state);

	return state_loaded ? &state : NULL;
}

/* ---- chip-specific U2F crypto ---- */

static int _derive_key(enum dcrypto_appid appid, const uint32_t input[8],
		       uint32_t output[8])
{
	struct APPKEY_CTX ctx;
	int result;

	/* Setup USR-based application key. */
	if (!DCRYPTO_appkey_init(appid, &ctx))
		return 0;
	result = DCRYPTO_appkey_derive(appid, input, output);

	DCRYPTO_appkey_finish(&ctx);
	return result;
}

int u2f_origin_key(const uint8_t *seed, p256_int *d)
{
	uint32_t tmp[P256_NDIGITS];

	memcpy(tmp, seed, sizeof(tmp));
	if (!_derive_key(U2F_ORIGIN, tmp, tmp))
		return EC_ERROR_UNKNOWN;
	return DCRYPTO_p256_key_from_bytes(NULL, NULL, d,
					   (const uint8_t *)tmp) == 0;
}

int u2f_origin_user_keyhandle(const uint8_t *origin,
			      const uint8_t *user,
			      const uint8_t *origin_seed,
			      uint8_t *key_handle)
{
	LITE_HMAC_CTX ctx;
	struct u2f_state *state = get_state();

	if (!state)
		return EC_ERROR_UNKNOWN;

	memcpy(key_handle, origin_seed, P256_NBYTES);

	DCRYPTO_HMAC_SHA256_init(&ctx, state->salt_kek, SHA256_DIGEST_SIZE);
	HASH_update(&ctx.hash, origin, P256_NBYTES);
	HASH_update(&ctx.hash, user, P256_NBYTES);
	HASH_update(&ctx.hash, origin_seed, P256_NBYTES);

	memcpy(key_handle + P256_NBYTES,
	       DCRYPTO_HMAC_final(&ctx), SHA256_DIGEST_SIZE);

	return EC_SUCCESS;
}

int u2f_origin_user_keypair(const uint8_t *key_handle,
			    p256_int *d,
			    p256_int *pk_x,
			    p256_int *pk_y)
{
	uint32_t dev_salt[P256_NDIGITS];
	uint8_t key_seed[P256_NBYTES];

	struct drbg_ctx drbg;
	struct u2f_state *state = get_state();

	if (!state)
		return EC_ERROR_UNKNOWN;

	if (!_derive_key(U2F_ORIGIN, state->salt_kek, dev_salt))
		return EC_ERROR_UNKNOWN;

	hmac_drbg_init(&drbg, state->salt_kh, P256_NBYTES, dev_salt,
		       P256_NBYTES, NULL, 0);

	hmac_drbg_generate(&drbg,
			   key_seed, sizeof(key_seed),
			   key_handle, P256_NBYTES * 2);

	if (!DCRYPTO_p256_key_from_bytes(pk_x, pk_y, d, key_seed))
		return EC_ERROR_TRY_AGAIN;

	return EC_SUCCESS;
}

int u2f_gen_kek(const uint8_t *origin, uint8_t *kek, size_t key_len)
{
	uint32_t buf[P256_NDIGITS];

	struct u2f_state *state = get_state();

	if (!state)
		return EC_ERROR_UNKNOWN;

	if (key_len != sizeof(buf))
		return EC_ERROR_UNKNOWN;
	if (!_derive_key(U2F_WRAP, state->salt_kek, buf))
		return EC_ERROR_UNKNOWN;
	memcpy(kek, buf, key_len);

	return EC_SUCCESS;
}

int g2f_individual_keypair(p256_int *d, p256_int *pk_x, p256_int *pk_y)
{
	uint8_t buf[SHA256_DIGEST_SIZE];

	struct u2f_state *state = get_state();

	if (!state)
		return EC_ERROR_UNKNOWN;

	/* Incorporate HIK & diversification constant */
	if (!_derive_key(U2F_ATTEST, state->salt, (uint32_t *)buf))
		return EC_ERROR_UNKNOWN;

	/* Generate unbiased private key */
	while (!DCRYPTO_p256_key_from_bytes(pk_x, pk_y, d, buf)) {
		HASH_CTX sha;

		DCRYPTO_SHA256_init(&sha, 0);
		HASH_update(&sha, buf, sizeof(buf));
		memcpy(buf, HASH_final(&sha), sizeof(buf));
	}

	return EC_SUCCESS;
}

int u2f_gen_kek_seed(int commit)
{
	struct u2f_state *state = get_state();

	if (!state)
		return EC_ERROR_UNKNOWN;

	if (!DCRYPTO_ladder_random(state->salt_kek))
		return EC_ERROR_HW_INTERNAL;

	if (write_tpm_nvmem_hidden(TPM_HIDDEN_U2F_KEK, sizeof(state->salt_kek),
				   state->salt_kek, commit) == tpm_write_fail)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/*
 * We need to keep a dummy version of this function around, as u2fd on M77 will
 * call it and not start up or send commands unless it receives a success
 * response. cr50 has been updated to no longer require the commands being sent,
 * so we don't need to do anything other than return a valid success response.
 */
static enum vendor_cmd_rc vc_u2f_apdu_dummy(enum vendor_cmd_cc code, void *body,
					    size_t cmd_size,
					    size_t *response_size)
{
	uint8_t *cmd = body;

	if (cmd_size < 3)
		return VENDOR_RC_BOGUS_ARGS;

	/*
	 * The incoming APDUs are in the following format:
	 *
	 *   CLA INS   P1  P2  Le
	 *   00  <ins> ??  ??  ??
	 */

	if (cmd[1] == 0xbf /* U2F_VENDOR_MODE */) {
		/*
		 * The u2fd code that call this command expects confirmation
		 * that the mode was correctly set in the return message.
		 *
		 * The incoming APDU is in the following format:
		 *
		 *   CLA INS P1  P2      Le
		 *   00  bf  01  <mode>  00
		 */
		cmd[0] = cmd[3];
	} else if (cmd[1] == 0x03 /* U2F_VERSION */) {
		/*
		 * The returned value for U2F_VERSION is not checked; return
		 * a known string just to be safe.
		 */
		cmd[0] = '2';
	} else {
		/* We're not expecting any other commands. */
		*response_size = 0;
		return VENDOR_RC_NO_SUCH_SUBCOMMAND;
	}

	/*
	 * Return U2F_SW_NO_ERROR status.
	 */
	cmd[1] = 0x90;
	cmd[2] = 0x00;
	*response_size = 3;

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_U2F_APDU, vc_u2f_apdu_dummy);

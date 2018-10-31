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
	if (ap_is_on() && rbox_powerbtn_is_pressed())
		last_press = get_time();
}

enum touch_state pop_check_presence(int consume)
{
	int recent = (get_time().val - PRESENCE_TIMEOUT) < last_press.val;

	CPRINTS("Presence:%d", recent);
	if (consume)
		last_press.val = 0;

	/* user physical presence on the power button */
	return recent ? POP_TOUCH_YES : POP_TOUCH_NO;
}

/* ---- non-volatile U2F parameters ---- */

/*
 * Current mode defining the behavior of the U2F feature.
 * Identical to the one defined on the host side by the enum U2fMode
 * in the chrome_device_policy.proto protobuf.
 */
enum u2f_mode {
	MODE_UNSET = 0,
	/* Feature disabled */
	MODE_DISABLED = 1,
	/* U2F as defined by the FIDO Alliance specification */
	MODE_U2F = 2,
	/* U2F plus extensions for individual attestation certificate */
	MODE_U2F_EXTENDED = 3,
};

static uint32_t salt[8];
static uint32_t salt_kek[8];
static uint8_t u2f_mode = MODE_UNSET;
static const uint8_t k_salt = NVMEM_VAR_U2F_SALT;

static int load_state(void)
{
	const struct tuple *t_salt = getvar(&k_salt, sizeof(k_salt));

	if (!t_salt) {
		/* create random salt */
		if (!DCRYPTO_ladder_random(salt))
			return 0;
		if (setvar(&k_salt, sizeof(k_salt),
			   (const uint8_t *)salt, sizeof(salt)))
			return 0;
		/* really save the new variable to flash */
		writevars();
	} else {
		memcpy(salt, tuple_val(t_salt), sizeof(salt));
	}

	if (read_tpm_nvmem_hidden(
		TPM_HIDDEN_U2F_KEK,
		sizeof(salt_kek), salt_kek) ==
	    tpm_read_not_found) {
		/*
		 * Not found means that we have not used u2f before,
		 * or not used it with updated fw that resets kek seed
		 * on TPM clear.
		 */
		if (t_salt) {
			/*
			 * We have previously used u2f, and may have
			 * existing registrations; we don't want to
			 * invalidate these, so preserve the existing
			 * seed as a one-off. It will be changed on
			 * next TPM clear.
			 */
			memcpy(salt_kek, salt, sizeof(salt_kek));
		} else {
			/*
			 * We have never used u2f before - generate
			 * new seed.
			 */
			if (!DCRYPTO_ladder_random(salt_kek))
				return 0;
		}
		if (write_tpm_nvmem_hidden(
			TPM_HIDDEN_U2F_KEK,
			sizeof(salt_kek), salt_kek, 1 /* commit */) !=
		    tpm_write_created)
			return 0;
	}

	return 1;
}

static int use_u2f(void)
{
	/*
	 * TODO(b/62294740): Put board ID check here if needed
	 * if (!board_id_we_want)
	 *	return 0;
	 */

	if (u2f_mode == MODE_UNSET) {
		if (load_state())
			/* Start without extension enabled, host will set it */
			u2f_mode = MODE_U2F;
	}

	return u2f_mode >= MODE_U2F;
}

int use_g2f(void)
{
	return use_u2f() && u2f_mode == MODE_U2F_EXTENDED;
}

unsigned u2f_custom_dispatch(uint8_t ins, struct apdu apdu,
			     uint8_t *buf, unsigned *ret_len)
{
	if (ins == U2F_VENDOR_MODE) {
		if (apdu.p1) { /* Set mode */
			u2f_mode = apdu.p2;
		}
		/* return the current mode */
		buf[0] = use_u2f() ? u2f_mode : 0;
		*ret_len = 1;
		return U2F_SW_NO_ERROR;
	}
	return U2F_SW_INS_NOT_SUPPORTED;
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

int u2f_origin_keypair(uint8_t *seed, p256_int *d,
		       p256_int *pk_x, p256_int *pk_y)
{
	uint32_t tmp[P256_NDIGITS];

	do {
		if (!DCRYPTO_ladder_random(seed))
			return EC_ERROR_UNKNOWN;
		memcpy(tmp, seed, sizeof(tmp));
		if (!_derive_key(U2F_ORIGIN, tmp, tmp))
			return EC_ERROR_UNKNOWN;
	} while (
	    !DCRYPTO_p256_key_from_bytes(pk_x, pk_y, d, (const uint8_t *)tmp));

	return EC_SUCCESS;
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

int u2f_gen_kek(const uint8_t *origin, uint8_t *kek, size_t key_len)
{
	uint32_t buf[P256_NDIGITS];

	if (key_len != sizeof(buf))
		return EC_ERROR_UNKNOWN;
	if (!_derive_key(U2F_WRAP, salt_kek, buf))
		return EC_ERROR_UNKNOWN;
	memcpy(kek, buf, key_len);

	return EC_SUCCESS;
}

int g2f_individual_keypair(p256_int *d, p256_int *pk_x, p256_int *pk_y)
{
	uint8_t buf[SHA256_DIGEST_SIZE];

	/* Incorporate HIK & diversification constant */
	if (!_derive_key(U2F_ATTEST, salt, (uint32_t *)buf))
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
	if (!DCRYPTO_ladder_random(salt_kek))
		return EC_ERROR_HW_INTERNAL;

	if (write_tpm_nvmem_hidden(
		TPM_HIDDEN_U2F_KEK, sizeof(salt_kek), salt_kek, commit) ==
	    tpm_write_fail)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/* ---- Send/receive U2F APDU over TPM vendor commands ---- */

enum vendor_cmd_rc vc_u2f_apdu(enum vendor_cmd_cc code, void *body,
			       size_t cmd_size, size_t *response_size)
{
	unsigned retlen;

	if (!use_u2f()) { /* the feature is disabled */
		uint8_t *cmd = body;
		/* process it only if the host tries to enable the feature */
		if (cmd_size < 2 || cmd[1] != U2F_VENDOR_MODE) {
			*response_size = 0;
			return VENDOR_RC_NO_SUCH_COMMAND;
		}
	}

	/* Process U2F APDU */
	retlen = u2f_apdu_rcv(body, cmd_size, *response_size);

	*response_size = retlen;
	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_U2F_APDU, vc_u2f_apdu);

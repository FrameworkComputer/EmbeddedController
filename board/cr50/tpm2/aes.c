/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "CryptoEngine.h"
#include "dcrypto.h"

#include <assert.h>
#include <string.h>

static CRYPT_RESULT _cpri__AESBlock(
	uint8_t *out, uint32_t len, uint8_t *in);


CRYPT_RESULT _cpri__AESDecryptCBC(
	uint8_t *out, uint32_t num_bits, uint8_t *key, uint8_t *iv,
	uint32_t len, uint8_t *in)
{
	CRYPT_RESULT result;

	if (len == 0)
		return CRYPT_SUCCESS;
	assert(key != NULL && iv != NULL && in != NULL && out != NULL);
	assert(len <= INT32_MAX);
	if (!DCRYPTO_aes_init(key, num_bits, iv, CIPHER_MODE_CBC, DECRYPT_MODE))
		return CRYPT_PARAMETER;

	result = _cpri__AESBlock(out, len, in);
	if (result != CRYPT_SUCCESS)
		return result;

	DCRYPTO_aes_read_iv(iv);
	return CRYPT_SUCCESS;
}

CRYPT_RESULT _cpri__AESDecryptCFB(uint8_t *out, uint32_t num_bits,
				uint8_t *key, uint8_t *iv, uint32_t len,
				uint8_t *in)
{
	uint8_t *ivp = NULL;
	int i;
	int32_t slen;

	if (len == 0)
		return CRYPT_SUCCESS;

	assert(key != NULL && iv != NULL && out != NULL && in != NULL);
	assert(len <= INT32_MAX);
	slen = (int32_t) len;
	/* Initialize AES hardware. */
	if (!DCRYPTO_aes_init(key, num_bits, iv, CIPHER_MODE_CTR, ENCRYPT_MODE))
		return CRYPT_PARAMETER;

	for (; slen > 0; slen -= 16) {
		uint8_t tmpin[16];
		uint8_t tmpout[16];
		const uint8_t *inp;
		uint8_t *outp;

		if (slen < 16) {
			memcpy(tmpin, in, slen);
			inp = tmpin;
			outp = tmpout;
		} else {
			inp = in;
			outp = out;
		}
		DCRYPTO_aes_block(inp, outp);
		if (outp != out)
			memcpy(out, outp, slen);

		ivp = iv;
		for (i = (slen < 16) ? slen : 16; i > 0; i--) {
			*ivp++ = *in++;
			out++;
		}
		DCRYPTO_aes_write_iv(iv);
	}
	/* If the inner loop (i loop) was smaller than 16, then slen
	 * would have been smaller than 16 and it is now negative
	 * If it is negative, then it indicates how may fill bytes
	 * are needed to pad out the IV for the next round. */
	for (; slen < 0; slen++)
		*ivp++ = 0;

	return CRYPT_SUCCESS;
}

CRYPT_RESULT _cpri__AESDecryptECB(
	uint8_t *out, uint32_t num_bits, uint8_t *key, uint32_t len,
	uint8_t *in)
{
	assert(key != NULL);
	/* Initialize AES hardware. */
	if (!DCRYPTO_aes_init(key, num_bits, NULL,
				CIPHER_MODE_ECB, DECRYPT_MODE))
		return CRYPT_PARAMETER;
	return _cpri__AESBlock(out, len, in);
}

static CRYPT_RESULT _cpri__AESBlock(
	uint8_t *out, uint32_t len, uint8_t *in)
{
	int32_t slen;

	assert(out != NULL && in != NULL && len > 0 && len <= INT32_MAX);
	slen = (int32_t) len;
	if ((slen % 16) != 0)
		return CRYPT_PARAMETER;

	for (; slen > 0; slen -= 16) {
		DCRYPTO_aes_block(in, out);
		in = &in[16];
		out = &out[16];
	}
	return CRYPT_SUCCESS;
}

CRYPT_RESULT _cpri__AESEncryptCBC(
	uint8_t *out, uint32_t num_bits, uint8_t *key, uint8_t *iv,
	uint32_t len, uint8_t *in)
{
	CRYPT_RESULT result;

	assert(key != NULL && iv != NULL);
	if (!DCRYPTO_aes_init(key, num_bits, iv, CIPHER_MODE_CBC, ENCRYPT_MODE))
		return CRYPT_PARAMETER;

	result = _cpri__AESBlock(out, len, in);
	if (result != CRYPT_SUCCESS)
		return result;

	DCRYPTO_aes_read_iv(iv);
	return CRYPT_SUCCESS;
}

CRYPT_RESULT _cpri__AESEncryptCFB(
	uint8_t *out, uint32_t num_bits, uint8_t *key, uint8_t *iv,
	uint32_t len, uint8_t *in)
{
	uint8_t *ivp = NULL;
	int32_t slen;
	int i;

	if (len == 0)
		return CRYPT_SUCCESS;

	assert(out != NULL && key != NULL && iv != NULL && in != NULL);
	assert(len <= INT32_MAX);
	slen = (int32_t) len;
	if (!DCRYPTO_aes_init(key, num_bits, iv, CIPHER_MODE_CTR, ENCRYPT_MODE))
		return CRYPT_PARAMETER;

	for (; slen > 0; slen -= 16) {
		DCRYPTO_aes_block(in, out);
		ivp = iv;
		for (i = slen < 16 ? slen : 16; i > 0; i--) {
			*ivp++ = *out++;
			in++;
		}
		DCRYPTO_aes_write_iv(iv);
	}
	/* If the inner loop (i loop) was smaller than 16, then slen
	 * would have been smaller than 16 and it is now negative. If
	 * it is negative, then it indicates how many bytes are needed
	 * to pad out the IV for the next round. */
	for (; slen < 0; slen++)
		*ivp++ = 0;
	return CRYPT_SUCCESS;
}

CRYPT_RESULT _cpri__AESEncryptCTR(
	uint8_t *out, uint32_t num_bits, uint8_t *key, uint8_t *iv,
	uint32_t len, uint8_t *in)
{
	int32_t slen;

	if (len == 0)
		return CRYPT_SUCCESS;

	assert(out != NULL && key != NULL && iv != NULL && in != NULL);
	assert(len <= INT32_MAX);
	slen = (int32_t) len;
	/* Initialize AES hardware. */
	if (!DCRYPTO_aes_init(key, num_bits, iv,
				CIPHER_MODE_CTR, ENCRYPT_MODE))
		return CRYPT_PARAMETER;

	for (; slen > 0; slen -= 16) {
		uint8_t tmpin[16];
		uint8_t tmpout[16];
		uint8_t *inp;
		uint8_t *outp;

		if (slen < 16) {
			memcpy(tmpin, in, slen);
			inp = tmpin;
			outp = tmpout;
		} else {
			inp = in;
			outp = out;
		}
		DCRYPTO_aes_block(inp, outp);
		if (outp != out)
			memcpy(out, outp, (slen < 16) ? slen : 16);

		in += 16;
		out += 16;
	}
	return CRYPT_SUCCESS;
}

CRYPT_RESULT _cpri__AESEncryptECB(
	uint8_t *out, uint32_t num_bits, uint8_t *key, uint32_t len,
	uint8_t *in)
{
	assert(key != NULL);
	/* Initialize AES hardware. */
	if (!DCRYPTO_aes_init(key, num_bits, NULL,
				CIPHER_MODE_ECB, ENCRYPT_MODE))
		return CRYPT_PARAMETER;
	return _cpri__AESBlock(out, len, in);
}

CRYPT_RESULT _cpri__AESEncryptOFB(
	uint8_t *out, uint32_t num_bits, uint8_t *key, uint8_t *iv,
	uint32_t len, uint8_t *in)
{
	uint8_t *ivp;
	int32_t slen;
	int i;

	if (len == 0)
		return CRYPT_SUCCESS;

	assert(out != NULL && key != NULL && iv != NULL && in != NULL);
	assert(len <= INT32_MAX);
	slen = (int32_t) len;
	/* Initialize AES hardware. */
	if (!DCRYPTO_aes_init(key, num_bits, NULL,
				CIPHER_MODE_ECB, ENCRYPT_MODE))
		return CRYPT_PARAMETER;

	for (; slen > 0; slen -= 16) {
		DCRYPTO_aes_block(iv, iv);
		ivp = iv;
		for (i = (slen < 16) ? slen : 16; i > 0; i--)
			*out++ = (*ivp++ ^ *in++);
	}
	return CRYPT_SUCCESS;
}

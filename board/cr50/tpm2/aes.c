/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "CryptoEngine.h"
#include "dcrypto.h"

#include <assert.h>

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
	if (len == 0)
		return CRYPT_SUCCESS;
	assert(key != NULL && iv != NULL && out != NULL && in != NULL);

	/* Initialize AES hardware. */
	if (!DCRYPTO_aes_init(key, num_bits, NULL,
				CIPHER_MODE_ECB, ENCRYPT_MODE))
		return CRYPT_PARAMETER;

	while (len > 0) {
		int i;
		size_t chunk_len;
		uint8_t mask[16];

		chunk_len = MIN(len, 16);

		DCRYPTO_aes_block(iv, mask);

		memcpy(iv, in, chunk_len);
		if (chunk_len != 16)
			memset(iv + chunk_len, 0, 16 - chunk_len);

		for (i = 0; i < chunk_len; i++)
			*out++ = *in++ ^ mask[i];
		len -= chunk_len;
	}

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
	if (len == 0)
		return CRYPT_SUCCESS;

	assert(out != NULL && key != NULL && iv != NULL && in != NULL);
	assert(len <= INT32_MAX);
	if (!DCRYPTO_aes_init(key, num_bits, iv, CIPHER_MODE_CTR, ENCRYPT_MODE))
		return CRYPT_PARAMETER;

	for (; len >= 16; len -= 16, in += 16, out += 16) {
		DCRYPTO_aes_block(in, out);
		DCRYPTO_aes_write_iv(out);
	}
	if (len > 0) {
		uint8_t buf[16];

		memcpy(buf, in, len);
		memset(buf+len, 0, 16-len);
		DCRYPTO_aes_block(buf, buf);
		memcpy(out, buf, len);
		memcpy(iv, buf, len);
		memset(iv+len, 0, 16-len);
	} else {
		memcpy(iv, out-16, 16);
	}
	return CRYPT_SUCCESS;
}

CRYPT_RESULT _cpri__AESEncryptCTR(
	uint8_t *out, uint32_t num_bits, uint8_t *key, uint8_t *iv,
	uint32_t len, uint8_t *in)
{
	if (len == 0)
		return CRYPT_SUCCESS;

	assert(out != NULL && key != NULL && iv != NULL && in != NULL);
	assert(len <= INT32_MAX);

	if (!DCRYPTO_aes_ctr(out, key, num_bits, iv, in, len))
		return CRYPT_PARAMETER;
	else
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

#ifdef CRYPTO_TEST_SETUP

#include "console.h"
#include "extension.h"
#include "hooks.h"
#include "uart.h"

enum aes_test_cipher_mode {
	TEST_MODE_ECB = 0,
	TEST_MODE_CTR = 1,
	TEST_MODE_CBC = 2,
	TEST_MODE_GCM = 3,
	TEST_MODE_OFB = 4,
	TEST_MODE_CFB = 5,
};

#define CPRINTF(format, args...) cprintf(CC_EXTENSION, format, ## args)

static void aes_command_handler(void *cmd_body,
				size_t cmd_size,
				size_t *response_size)
{
	uint8_t *key;
	uint16_t key_len;
	uint8_t iv_len;
	uint8_t *iv;
	uint8_t aad_len;
	const uint8_t *aad;
	enum aes_test_cipher_mode c_mode;
	enum encrypt_mode e_mode;
	uint8_t *cmd = (uint8_t *)cmd_body;
	int16_t data_len;
	unsigned max_data_len = *response_size;
	unsigned actual_cmd_size;

	/* Copy inputs into a local unaligned buffer, so as to ensure
	 * that api's are memory-alignment agnostic.
	 */
	struct unaligned_buf {
		uint8_t unused;
		uint8_t b[255];
	} __packed;

	struct unaligned_buf out_local;
	struct unaligned_buf iv_local;
	struct unaligned_buf key_local;
	struct unaligned_buf data_local;

	*response_size = 0;

	/*
	 * Command structure, shared out of band with the test driver running
	 * on the host:
	 *
	 * field       |    size  |              note
	 * ================================================================
	 * mode        |    1     | 0 - decrypt, 1 - encrypt
	 * cipher_mode |    1     | as per aes_test_cipher_mode
	 * key_len     |    1     | key size in bytes (16, 24 or 32)
	 * key         | key len  | key to use
	 * iv_len      |    1     | either 0 or 16
	 * iv          | 0 or 16  | as defined by iv_len
	 * aad_len     |  <= 127  | additional authentication data length
	 * aad         |  aad_len | additional authentication data
	 * text_len    |    2     | size of the text to process, big endian
	 * text        | text_len | text to encrypt/decrypt
	 */
	e_mode = *cmd++;
	c_mode = *cmd++;
	key_len = *cmd++;

	if ((key_len != 16) && (key_len != 24) && (key_len != 32)) {
		CPRINTF("Invalid key len %d\n", key_len * 8);
		return;
	}
	key = cmd;
	cmd += key_len;
	key_len *= 8;
	iv_len = *cmd++;
	if ((c_mode == TEST_MODE_GCM && iv_len == 0) ||
		(c_mode != TEST_MODE_GCM && iv_len && iv_len != 16)) {
		CPRINTF("Invalid vector len %d\n", iv_len);
		return;
	}
	iv = cmd;
	cmd += iv_len;
	aad_len = *cmd++;
	aad = cmd;
	cmd += aad_len;
	data_len = *cmd++;
	data_len = data_len * 256 + *cmd++;

	/*
	 * We know that the receive buffer is at least this big, i.e. all the
	 * preceding fields are guaranteed to fit.
	 *
	 * Now is a good time to verify overall sanity of the received
	 * payload: does the actual size match the added up sizes of the
	 * pieces.
	 */
	actual_cmd_size = cmd - (const uint8_t *)cmd_body + data_len;
	if (actual_cmd_size != cmd_size) {
		CPRINTF("Command size mismatch: %d != %d (data len %d)\n",
			actual_cmd_size, cmd_size, data_len);
		return;
	}

	if (((data_len + 15) & ~15) > max_data_len) {
		CPRINTF("Response buffer too small\n");
		return;
	}

	if (data_len > sizeof(out_local.b)) {
		CPRINTF("Response buffer too small\n");
		return;
	}

	memset(out_local.b, 'A', sizeof(out_local.b));
	memcpy(iv_local.b, iv, iv_len);
	memcpy(key_local.b, key, key_len / 8);
	memcpy(data_local.b, cmd, data_len);

	switch (c_mode) {
	case TEST_MODE_ECB:
		if (e_mode == 0) {
			if (_cpri__AESDecryptECB(
					out_local.b, key_len, key_local.b,
					data_len, data_local.b) ==
			    CRYPT_SUCCESS) {
				*response_size = data_len;
			}
			CPRINTF("%s:%d response size %d\n",
				__func__, __LINE__, *response_size);
		} else if (e_mode == 1) {
			/* pad input data to integer block size. */
			while (data_len & 15)
				data_local.b[data_len++] = 0;
			if (_cpri__AESEncryptECB(
					out_local.b, key_len, key_local.b,
					data_len, data_local.b) ==
			    CRYPT_SUCCESS) {
				*response_size = data_len;
			}
			CPRINTF("%s:%d response size %d\n",
				__func__, __LINE__, *response_size);
		}
		break;
	case TEST_MODE_CTR:
		if (e_mode == 0) {
			if (_cpri__AESDecryptCTR(
					out_local.b, key_len, key_local.b,
					iv_local.b, data_len, data_local.b) ==
			    CRYPT_SUCCESS) {
				*response_size = data_len;
			}
			CPRINTF("%s:%d response size %d\n",
				__func__, __LINE__, *response_size);
		} else if (e_mode == 1) {
			/* pad input data to integer block size. */
			while (data_len & 15)
				data_local.b[data_len++] = 0;
			if (_cpri__AESEncryptCTR(
					out_local.b, key_len, key_local.b,
					iv_local.b, data_len, data_local.b) ==
			    CRYPT_SUCCESS) {
				*response_size = data_len;
			}
			CPRINTF("%s:%d response size %d\n",
				__func__, __LINE__, *response_size);
		}
		break;
	case TEST_MODE_CBC:
	{
		if (e_mode == 0) {
			if (_cpri__AESDecryptCBC(
					out_local.b, key_len, key_local.b,
					iv_local.b, data_len, data_local.b) ==
				CRYPT_SUCCESS) {
				*response_size = data_len;
			}
			CPRINTF("%s:%d response size %d\n",
				__func__, __LINE__, *response_size);
		} else if (e_mode == 1) {
			if (_cpri__AESEncryptCBC(
					out_local.b, key_len, key_local.b,
					iv_local.b, data_len, data_local.b) ==
			    CRYPT_SUCCESS) {
				*response_size = data_len;
			}
			CPRINTF("%s:%d response size %d\n",
				__func__, __LINE__, *response_size);
		}
		break;
	}
	case TEST_MODE_GCM:
	{
		if (e_mode == 0) {
			size_t total;
			size_t count;
			struct GCM_CTX ctx;

			DCRYPTO_gcm_init(&ctx, key_local.b, iv_local.b, iv_len);
			DCRYPTO_gcm_aad(&ctx, aad, aad_len);
			count = DCRYPTO_gcm_decrypt(
				&ctx, out_local.b, sizeof(out_local.b),
				data_local.b, data_len);
			if (count < 0) {
				CPRINTF(
					"%s: gcm decrypt failed\n", __func__);
				break;
			}
			total = count;
			count = DCRYPTO_gcm_decrypt_final(
				&ctx, out_local.b + total,
				sizeof(out_local.b) - total);
			if (count < 0) {
				CPRINTF(
					"%s: gcm decrypt_final failed\n",
					__func__);
				break;
			}
			total += count;
			count = DCRYPTO_gcm_tag(&ctx, out_local.b + total,
						sizeof(out_local.b) - total);
			if (count == 0) {
				CPRINTF("%s: gcm tag failed\n", __func__);
				break;
			}
			total += count;
			*response_size = total;
		} else if (e_mode == 1) {
			size_t total;
			size_t count;
			struct GCM_CTX ctx;

			DCRYPTO_gcm_init(&ctx, key_local.b, iv_local.b, iv_len);
			DCRYPTO_gcm_aad(&ctx, aad, aad_len);
			count = DCRYPTO_gcm_encrypt(
				&ctx, out_local.b, sizeof(out_local.b),
				data_local.b, data_len);
			if (count < 0) {
				CPRINTF(
					"%s: gcm encrypt failed\n");
				break;
			}
			total = count;
			count = DCRYPTO_gcm_encrypt_final(
				&ctx, out_local.b + total,
				sizeof(out_local.b) - total);
			if (count < 0) {
				CPRINTF(
					"%s: gcm encrypt_final failed\n",
					__func__);
				break;
			}
			total += count;
			count = DCRYPTO_gcm_tag(
				&ctx, out_local.b + total,
				sizeof(out_local.b) - total);
			if (count == 0) {
				CPRINTF("%s: gcm tag failed\n", __func__);
				break;
			}
			total += count;
			*response_size = total;
		}
		break;
	}
	case TEST_MODE_OFB:
		if (e_mode == 0) {
			if (_cpri__AESDecryptOFB(
					out_local.b, key_len, key_local.b,
					iv_local.b, data_len, data_local.b) ==
			    CRYPT_SUCCESS) {
				*response_size = data_len;
			}
			CPRINTF("%s:%d response size %d\n",
				__func__, __LINE__, *response_size);
		} else if (e_mode == 1) {
			if (_cpri__AESEncryptOFB(
					out_local.b, key_len, key_local.b,
					iv_local.b, data_len, data_local.b) ==
			    CRYPT_SUCCESS) {
				*response_size = data_len;
			}
			CPRINTF("%s:%d response size %d\n",
				__func__, __LINE__, *response_size);
		}
		break;
	case TEST_MODE_CFB:
	{
		if (e_mode == 0) {
			if (_cpri__AESDecryptCFB(
					out_local.b, key_len, key_local.b,
					iv_local.b, data_len, data_local.b) ==
			    CRYPT_SUCCESS) {
				*response_size = data_len;
			}
			CPRINTF("%s:%d response size %d\n",
				__func__, __LINE__, *response_size);
		} else if (e_mode == 1) {
			if (_cpri__AESEncryptCFB(
					out_local.b, key_len, key_local.b,
					iv_local.b, data_len, data_local.b) ==
			    CRYPT_SUCCESS) {
				*response_size = data_len;
			}
			CPRINTF("%s:%d response size %d\n",
				__func__, __LINE__, *response_size);
		}
		break;
	}
	default:
		break;
	}

	if (*response_size > 0) {
		int i;

		for (i = *response_size; i < sizeof(out_local.b); i++) {
			if (out_local.b[i] != 'A') {
				CPRINTF(
					"%s:%d output overwrite at offset %d\n",
					__func__, __LINE__, i);
				*response_size = 0;
			}
		}

		memcpy(cmd_body, out_local.b, *response_size);
	}
}

DECLARE_EXTENSION_COMMAND(EXTENSION_AES, aes_command_handler);

#endif   /* CRYPTO_TEST_SETUP */

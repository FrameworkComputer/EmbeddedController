/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "CryptoEngine.h"

#include "util.h"
#include "dcrypto.h"

static const HASH_INFO *lookup_hash_info(TPM_ALG_ID alg)
{
	int i;
	const int num_algs = ARRAY_SIZE(g_hashData);

	for (i = 0; i < num_algs - 1; i++) {
		if (g_hashData[i].alg == alg)
			return &g_hashData[i];
	}
	return &g_hashData[num_algs - 1];
}

TPM_ALG_ID _cpri__GetContextAlg(CPRI_HASH_STATE *hash_state)
{
	return hash_state->hashAlg;
}

TPM_ALG_ID _cpri__GetHashAlgByIndex(uint32_t index)
{
	if (index >= HASH_COUNT)
		return TPM_ALG_NULL;
	return g_hashData[index].alg;
}

uint16_t _cpri__GetDigestSize(TPM_ALG_ID alg)
{
	return lookup_hash_info(alg)->digestSize;
}

uint16_t _cpri__GetHashBlockSize(TPM_ALG_ID alg)
{
	return lookup_hash_info(alg)->blockSize;
}

BUILD_ASSERT(sizeof(LITE_SHA256_CTX) == USER_MIN_HASH_STATE_SIZE);
BUILD_ASSERT(sizeof(CPRI_HASH_STATE) == sizeof(EXPORT_HASH_STATE));
void _cpri__ImportExportHashState(CPRI_HASH_STATE *osslFmt,
				EXPORT_HASH_STATE *externalFmt,
				IMPORT_EXPORT direction)
{
	if (direction == IMPORT_STATE)
		memcpy(osslFmt, externalFmt, sizeof(CPRI_HASH_STATE));
	else
		memcpy(externalFmt, osslFmt, sizeof(CPRI_HASH_STATE));
}

uint16_t _cpri__HashBlock(TPM_ALG_ID alg, uint32_t in_len, uint8_t *in,
			uint32_t out_len, uint8_t *out)
{
	uint8_t digest[SHA_DIGEST_MAX_BYTES];
	const uint16_t digest_len = _cpri__GetDigestSize(alg);

	if (digest_len == 0)
		return 0;

	switch (alg) {
	case TPM_ALG_SHA1:
		DCRYPTO_SHA1_hash(in, in_len, digest);
		break;

	case TPM_ALG_SHA256:
		DCRYPTO_SHA256_hash(in, in_len, digest);
		break;
	case TPM_ALG_SHA384:
		DCRYPTO_SHA384_hash(in, in_len, digest);
		break;
	case TPM_ALG_SHA512:
		DCRYPTO_SHA512_hash(in, in_len, digest);
		break;
	default:
		FAIL(FATAL_ERROR_INTERNAL);
		break;
	}

	out_len = MIN(out_len, digest_len);
	memcpy(out, digest, out_len);
	return out_len;
}

BUILD_ASSERT(sizeof(struct HASH_CTX) <=
	     sizeof(((CPRI_HASH_STATE *)0)->state));
uint16_t _cpri__StartHash(TPM_ALG_ID alg, BOOL sequence,
			  CPRI_HASH_STATE *state)
{
	struct HASH_CTX *ctx = (struct HASH_CTX *) state->state;
	uint16_t result;

	/* NOTE: as per bug http://crosbug.com/p/55331#26 (NVMEM
	 * encryption), always use the software hash implementation
	 * for TPM related calculations, since we have no guarantee
	 * that the key-ladder will not be used between SHA_init() and
	 * final().
	 */
	switch (alg) {
	case TPM_ALG_SHA1:
		DCRYPTO_SHA1_init(ctx, 1);
		result = HASH_size(ctx);
		break;
	case TPM_ALG_SHA256:
		DCRYPTO_SHA256_init(ctx, 1);
		result = HASH_size(ctx);
		break;

	case TPM_ALG_SHA384:
		DCRYPTO_SHA384_init(ctx);
		result = HASH_size(ctx);
		break;
	case TPM_ALG_SHA512:
		DCRYPTO_SHA512_init(ctx);
		result = HASH_size(ctx);
		break;
	default:
		result = 0;
		break;
	}

	if (result > 0)
		state->hashAlg = alg;

	return result;
}

void _cpri__UpdateHash(CPRI_HASH_STATE *state, uint32_t in_len,
		BYTE *in)
{
	struct HASH_CTX *ctx = (struct HASH_CTX *) state->state;

	HASH_update(ctx, in, in_len);
}

uint16_t _cpri__CompleteHash(CPRI_HASH_STATE *state,
			uint32_t out_len, uint8_t *out)
{
	struct HASH_CTX *ctx = (struct HASH_CTX *) state->state;

	out_len = MIN(HASH_size(ctx), out_len);
	memcpy(out, HASH_final(ctx), out_len);
	return out_len;
}

#ifdef CRYPTO_TEST_SETUP

#include "console.h"
#include "extension.h"
#include "shared_mem.h"

#define CPRINTF(format, args...) cprintf(CC_EXTENSION, format, ## args)

struct test_context {
	int context_handle;
	CPRI_HASH_STATE hstate;
};

static struct {
	int current_context_count;
	int max_contexts;
	struct test_context *contexts;
} hash_test_db;

struct test_context *find_context(int handle)
{
	int i;

	for (i = 0; i < hash_test_db.current_context_count; i++)
		if (hash_test_db.contexts[i].context_handle == handle)
			return hash_test_db.contexts + i;
	return NULL;
}

static void process_start(TPM_ALG_ID alg, int handle, void *response_body,
			  size_t *response_size)
{
	uint8_t *response = response_body;
	struct test_context *new_context;

	if (find_context(handle)) {
		*response = EXC_HASH_DUPLICATED_HANDLE;
		*response_size = 1;
		return;
	}

	if (!hash_test_db.max_contexts) {
		size_t buffer_size;

		/* Check how many contexts could possible fit. */
		hash_test_db.max_contexts = shared_mem_size() /
			sizeof(struct test_context);

		buffer_size = sizeof(struct test_context) *
			hash_test_db.max_contexts;

		if (shared_mem_acquire(buffer_size,
				       (char **)&hash_test_db.contexts) !=
		    EC_SUCCESS) {
			/* Must be out of memory. */
			hash_test_db.max_contexts = 0;
			*response = EXC_HASH_TOO_MANY_HANDLES;
			*response_size = 1;
			return;
		}
		memset(hash_test_db.contexts, 0, buffer_size);
	}

	if (hash_test_db.current_context_count == hash_test_db.max_contexts) {
		*response = EXC_HASH_TOO_MANY_HANDLES;
		*response_size = 1;
		return;
	}

	new_context = hash_test_db.contexts +
		hash_test_db.current_context_count++;
	new_context->context_handle = handle;
	_cpri__StartHash(alg, 0, &new_context->hstate);
}

static void process_continue(int handle, void *cmd_body, uint16_t text_len,
			     void *response_body, size_t *response_size)
{
	struct test_context *context = find_context(handle);

	if (!context) {
		*((uint8_t *)response_body) = EXC_HASH_UNKNOWN_CONTEXT;
		*response_size = 1;
		return;
	}

	_cpri__UpdateHash(&context->hstate, text_len, cmd_body);
}

static void process_finish(int handle, void *response_body,
			   size_t *response_size)
{
	struct test_context *context = find_context(handle);

	if (!context) {
		*((uint8_t *)response_body) = EXC_HASH_UNKNOWN_CONTEXT;
		*response_size = 1;
		return;
	}

	/* There for sure is enough room in the TPM buffer. */
	*response_size = _cpri__CompleteHash(&context->hstate,
					     SHA_DIGEST_MAX_BYTES,
					     response_body);

	/* drop this context from the database. */
	hash_test_db.current_context_count--;
	if (!hash_test_db.current_context_count) {
		shared_mem_release(hash_test_db.contexts);
		hash_test_db.max_contexts = 0;
		return;
	}

	/* Nothing to do, if the deleted context is the last one in memory. */
	if (context == (hash_test_db.contexts +
			hash_test_db.current_context_count))
		return;

	memcpy(context,
	       hash_test_db.contexts + hash_test_db.current_context_count,
	       sizeof(*context));
}

static uint16_t do_software_hmac(TPM_ALG_ID alg, uint32_t in_len, uint8_t *in,
				 uint32_t out_len, uint8_t *out)
{
	CPRI_HASH_STATE hstate;
	TPM2B_MAX_HASH_BLOCK hmacKey;
	uint8_t *key;
	uint32_t key_len;
	const uint16_t digest_len = _cpri__GetDigestSize(alg);

	if (digest_len == 0)
		return 0;
	key = in + in_len;
	key_len = *key++;
	key_len = key_len * 256 + *key++;
	_cpri__StartHMAC(alg, 0, &hstate, key_len, key, &hmacKey.b);
	_cpri__UpdateHash(&hstate, in_len, in);
	out_len = _cpri__CompleteHMAC(&hstate, &hmacKey.b, out_len, out);
	return out_len;
}

static uint16_t do_dcrypto_hmac(TPM_ALG_ID alg, uint32_t in_len,
				uint8_t *in, int32_t out_len, uint8_t *out)
{
	LITE_HMAC_CTX ctx;
	uint8_t *key;
	uint32_t key_len;

	/* Dcrypto only support SHA-256 */
	if (alg != TPM_ALG_SHA256)
		return 0;
	key = in + in_len;
	key_len = *key++;
	key_len = key_len * 256 + *key++;
	DCRYPTO_HMAC_SHA256_init(&ctx, key, key_len);
	HASH_update(&ctx.hash, in, in_len);
	out_len = MIN(out_len, SHA256_DIGEST_SIZE);
	memcpy(out, DCRYPTO_HMAC_final(&ctx), out_len);
	return out_len;
}

enum hash_cmd {
	CMD_HASH_START = 0,
	CMD_HASH_CONTINUE = 1,
	CMD_HASH_FINISH = 2,
	CMD_HASH = 3,
	CMD_SW_HMAC = 4,
	CMD_HW_HMAC = 5
};

enum hash_alg {
	HASH_ALG_SHA1 = 0,
	HASH_ALG_SHA256 = 1,
	HASH_ALG_SHA384 = 2,
	HASH_ALG_SHA512 = 3
};

static void hash_command_handler(void *cmd_body,
				size_t cmd_size,
				size_t *response_size)
{
	enum hash_cmd hash_cmd;
	enum hash_alg hash_alg;
	int handle;
	uint16_t text_len;
	uint8_t *cmd;
	size_t response_room = *response_size;
	TPM_ALG_ID alg;

	cmd = cmd_body;

	/*
	 * Empty response is sent as a success indication when the digest is
	 * not yet expected (i.e. in response to 'start' and 'cont' commands,
	 * as defined below).
	 *
	 * Single byte responses indicate errors, test successes are
	 * communicated as responses of the size of the appropriate digests.
	 */
	*response_size = 0;

	/*
	 * Command structure, shared out of band with the test driver running
	 * on the host:
	 *
	 * field     |    size  |                  note
	 * ===================================================================
	 * hash_cmd  |    1     | 0 - start, 1 - cont., 2 - finish, 3 - single
	 *           |          | 4 - SW HMAC single shot (TPM code)
	 *           |          | 5 - HW HMAC SHA256 single shot (dcrypto code)
	 * hash_alg  |    1     | 0 - sha1, 1 - sha256, 2 - sha384, 3 - sha512
	 * handle    |    1     | session handle, ignored in 'single' mode
	 * text_len  |    2     | size of the text to process, big endian
	 * text      | text_len | text to hash
	 * for HMAC single shot only:
	 * key_len   |    2     | size of the key for HMAC, big endian
	 * key       | key_len  | key for HMAC single shot
	 */

	hash_cmd = *cmd++;
	hash_alg = *cmd++;
	handle = *cmd++;
	text_len = *cmd++;
	text_len = text_len * 256 + *cmd++;

	switch (hash_alg) {
	case HASH_ALG_SHA1:
		alg = TPM_ALG_SHA1;
		break;
	case HASH_ALG_SHA256:
		alg = TPM_ALG_SHA256;
		break;
#ifdef SHA512_SUPPORT
	case HASH_ALG_SHA384:
		alg = TPM_ALG_SHA384;
		break;
	case HASH_ALG_SHA512:
		alg = TPM_ALG_SHA512;
		break;
#endif
	default:
		return;
	}

	switch (hash_cmd) {
	case CMD_HASH_START: /* Start a new hash context. */
		process_start(alg, handle, cmd_body, response_size);
		if (*response_size)
			break; /* Something went wrong. */
		process_continue(handle, cmd, text_len,
				 cmd_body, response_size);
		break;

	case CMD_HASH_CONTINUE:
		process_continue(handle, cmd, text_len,
				 cmd_body, response_size);
		break;

	case CMD_HASH_FINISH:
		process_continue(handle, cmd, text_len,
				 cmd_body, response_size);
		if (*response_size)
			break;  /* Something went wrong. */

		process_finish(handle, cmd_body, response_size);
		CPRINTF("%s:%d response size %d\n", __func__, __LINE__,
			*response_size);
		break;

	case CMD_HASH: /* Process a buffer in a single shot. */
		/*
		 * Error responses are just 1 byte in size, valid responses
		 * are of various hash sizes.
		 */
		*response_size = _cpri__HashBlock(alg, text_len,
						  cmd, response_room, cmd_body);
		CPRINTF("%s:%d response size %d\n", __func__,
			__LINE__, *response_size);
		break;
	case CMD_SW_HMAC: /* SW HMAC SHA-256 (key, value) in a single shot. */
		/*
		 * Error responses are just 1 byte in size, valid responses
		 * are of various hash sizes.
		 */
		*response_size = do_software_hmac(alg, text_len, cmd,
						  response_room, cmd_body);
		CPRINTF("%s:%d hmac response size %d\n", __func__, __LINE__,
			*response_size);
		break;
	case CMD_HW_HMAC: /* HW HMAC SHA-256 (key, value) in a single shot */
		/*
		 * Error responses are just 1 byte in size, valid responses
		 * are of various hash sizes.
		 */
		*response_size = do_dcrypto_hmac(alg, text_len, cmd,
						 response_room, cmd_body);
		CPRINTF("%s:%d hmac response size %d\n", __func__, __LINE__,
			*response_size);
		break;

	default:
		break;
	}
}

DECLARE_EXTENSION_COMMAND(EXTENSION_HASH, hash_command_handler);

#endif   /* CRYPTO_TEST_SETUP */

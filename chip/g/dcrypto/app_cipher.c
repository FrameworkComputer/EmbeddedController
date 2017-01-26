/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "dcrypto.h"
#include "registers.h"

/* The default build options compile for size (-Os); instruct the
 * compiler to optimize for speed here.  Incidentally -O produces
 * faster code than -O2!
 */
static int __attribute__((optimize("O")))
inner_loop(uint32_t **out, const uint32_t **in, size_t len)
{
	uint32_t *outw = *out;
	const uint32_t *inw = *in;

	while (len >= 16) {
		uint32_t w0, w1, w2, w3;

		w0 = inw[0];
		w1 = inw[1];
		w2 = inw[2];
		w3 = inw[3];
		GREG32(KEYMGR, AES_WFIFO_DATA) = w0;
		GREG32(KEYMGR, AES_WFIFO_DATA) = w1;
		GREG32(KEYMGR, AES_WFIFO_DATA) = w2;
		GREG32(KEYMGR, AES_WFIFO_DATA) = w3;

		while (GREG32(KEYMGR, AES_RFIFO_EMPTY))
			;

		w0 = GREG32(KEYMGR, AES_RFIFO_DATA);
		w1 = GREG32(KEYMGR, AES_RFIFO_DATA);
		w2 = GREG32(KEYMGR, AES_RFIFO_DATA);
		w3 = GREG32(KEYMGR, AES_RFIFO_DATA);
		outw[0] = w0;
		outw[1] = w1;
		outw[2] = w2;
		outw[3] = w3;

		inw += 4;
		outw += 4;
		len -= 16;
	}

	*in = inw;
	*out = outw;
	return len;
}

static int outer_loop(uint32_t **out, const uint32_t **in, size_t len)
{
	uint32_t *outw = *out;
	const uint32_t *inw = *in;

	if (len >= 16) {
		GREG32(KEYMGR, AES_WFIFO_DATA) = inw[0];
		GREG32(KEYMGR, AES_WFIFO_DATA) = inw[1];
		GREG32(KEYMGR, AES_WFIFO_DATA) = inw[2];
		GREG32(KEYMGR, AES_WFIFO_DATA) = inw[3];
		inw += 4;
		len -= 16;

		len = inner_loop(&outw, &inw, len);

		while (GREG32(KEYMGR, AES_RFIFO_EMPTY))
			;

		outw[0] = GREG32(KEYMGR, AES_RFIFO_DATA);
		outw[1] = GREG32(KEYMGR, AES_RFIFO_DATA);
		outw[2] = GREG32(KEYMGR, AES_RFIFO_DATA);
		outw[3] = GREG32(KEYMGR, AES_RFIFO_DATA);
		outw += 4;
	}

	*in =  inw;
	*out = outw;
	return len;
}

static int aes_init(struct APPKEY_CTX *ctx, enum dcrypto_appid appid,
		const uint32_t iv[4])
{
	/* Setup USR-based application key. */
	if (!DCRYPTO_appkey_init(appid, ctx))
		return 0;

	/* Configure AES engine. */
	GWRITE_FIELD(KEYMGR, AES_CTRL, RESET, CTRL_NO_SOFT_RESET);
	GWRITE_FIELD(KEYMGR, AES_CTRL, KEYSIZE, 2 /* AES-256 */);
	GWRITE_FIELD(KEYMGR, AES_CTRL, CIPHER_MODE, CIPHER_MODE_CTR);
	GWRITE_FIELD(KEYMGR, AES_CTRL, ENC_MODE, ENCRYPT_MODE);
	GWRITE_FIELD(KEYMGR, AES_CTRL, CTR_ENDIAN, CTRL_CTR_BIG_ENDIAN);

	/* Enable hidden key usage, each appid gets its own
	 * USR, with USR0 starting at 0x2a0.
	 */
	GWRITE_FIELD(KEYMGR, AES_USE_HIDDEN_KEY, INDEX,
		0x2a0 + (appid * 2));
	GWRITE_FIELD(KEYMGR, AES_USE_HIDDEN_KEY, ENABLE, 1);
	GWRITE_FIELD(KEYMGR, AES_CTRL, ENABLE, CTRL_ENABLE);

	/* Wait for key-expansion. */
	GREG32(KEYMGR, AES_KEY_START) = 1;
	while (GREG32(KEYMGR, AES_KEY_START))
		;

	/* Check for errors (e.g. USR not correctly setup. */
	if (GREG32(KEYMGR, HKEY_ERR_FLAGS))
		return 0;

	/* Set IV. */
	GR_KEYMGR_AES_CTR(0) = iv[0];
	GR_KEYMGR_AES_CTR(1) = iv[1];
	GR_KEYMGR_AES_CTR(2) = iv[2];
	GR_KEYMGR_AES_CTR(3) = iv[3];

	return 1;
}

int DCRYPTO_app_cipher(enum dcrypto_appid appid, const void *salt,
		void *out, const void *in, size_t len)
{
	struct APPKEY_CTX ctx;
	const uint32_t *inw = in;
	uint32_t *outw = out;

	/* Test pointers for word alignment. */
	if (((uintptr_t) in & 0x03) || ((uintptr_t) out & 0x03))
		return 0;

	{
		/* Initialize key, and AES engine. */
		uint32_t iv[4];

		memcpy(iv, salt, sizeof(iv));
		if (!aes_init(&ctx, appid, iv))
			return 0;
	}

	len = outer_loop(&outw, &inw, len);

	if (len) {
		/* Cipher the final partial block */
		uint32_t tmpin[4];
		uint32_t tmpout[4];
		const uint32_t *tmpinw;
		uint32_t *tmpoutw;

		tmpinw = tmpin;
		tmpoutw = tmpout;

		memcpy(tmpin, inw, len);
		outer_loop(&tmpoutw, &tmpinw, 16);
		memcpy(outw, tmpout, len);
	}

	DCRYPTO_appkey_finish(&ctx);
	return 1;
}

#ifdef CRYPTO_TEST_SETUP

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "shared_mem.h"
#include "task.h"
#include "timer.h"

/*
 * Let's use some odd size to make sure unaligned buffers are handled
 * properly.
 */
#define TEST_BLOB_SIZE 16387

static uint8_t result;
static void run_cipher_cmd(void)
{
	int rv;
	char *p;
	uint8_t sha[SHA_DIGEST_SIZE];
	uint8_t sha_after[SHA_DIGEST_SIZE];
	int match;
	uint32_t tstamp;

	rv = shared_mem_acquire(TEST_BLOB_SIZE, (char **)&p);

	if (rv != EC_SUCCESS) {
		result = rv;
		return;
	}

	ccprintf("original data           %.16h\n", p);

	DCRYPTO_SHA1_hash((uint8_t *)p, TEST_BLOB_SIZE, sha);

	tstamp = get_time().val;
	rv = DCRYPTO_app_cipher(NVMEM, &sha, p, p, TEST_BLOB_SIZE);
	tstamp = get_time().val - tstamp;
	ccprintf("rv 0x%02x, out data       %.16h, time %d us\n",
		 rv, p, tstamp);

	if (rv == 1) {
		tstamp = get_time().val;
		rv = DCRYPTO_app_cipher(NVMEM, &sha, p, p, TEST_BLOB_SIZE);
		tstamp = get_time().val - tstamp;
		ccprintf("rv 0x%02x, orig. data     %.16h, time %d us\n",
			 rv, p, tstamp);
	}

	DCRYPTO_SHA1_hash((uint8_t *)p, TEST_BLOB_SIZE, sha_after);

	match = !memcmp(sha, sha_after, sizeof(sha));
	ccprintf("sha1 before and after %smatch!\n",
		 match ? "" : "MIS");

	shared_mem_release(p);

	if ((rv == 1) && match)
		result = EC_SUCCESS;
	else
		result = EC_ERROR_UNKNOWN;

	task_set_event(TASK_ID_CONSOLE, TASK_EVENT_CUSTOM(1), 0);
}
DECLARE_DEFERRED(run_cipher_cmd);

static int cmd_cipher(int argc, char **argv)
{
	uint32_t events;

	hook_call_deferred(&run_cipher_cmd_data, 0);

	/* Should be done much sooner than in 1 second. */
	events = task_wait_event_mask(TASK_EVENT_CUSTOM(1), 1 * SECOND);
	if (!(events & TASK_EVENT_CUSTOM(1))) {
		ccprintf("Timed out, you might want to reboot...\n");
		return EC_ERROR_TIMEOUT;
	}

	return result;
}
DECLARE_SAFE_CONSOLE_COMMAND(cipher, cmd_cipher, NULL, NULL);
#endif

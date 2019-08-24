/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* AES-CMAC-128 implementation according to NIST SP 800-38B, RFC4493 */
#include "console.h"
#include "dcrypto.h"

#define BSIZE 16 /* 16 bytes per 128-bit block */

/* Given a 128-bit number in 32-bit chunks, shift to the left by one */
static void shiftl_1(const uint8_t *in, uint8_t *out)
{
	int i;
	uint8_t carry = 0;

	for (i = 15; i >= 0; i--) {
		out[i] = in[i] << 1;
		out[i] |= carry;
		carry = (in[i] & 0x80) ? 1 : 0;
	}
}

static void xor128(const uint32_t in1[4], const uint32_t in2[4],
		uint32_t out[4])
{
	int i;

	for (i = 0; i < 4; i++)
		out[i] = in1[i] ^ in2[i];
}

static void get_and_xor(const uint8_t *arr, const uint32_t nBytes, int i,
			const uint8_t *xor_term, uint8_t *out)
{
	int j;
	int k;

	for (j = 0; j < 16; j++) {
		k = i*16 + j; /* index in arr */
		if (k < nBytes)
			out[j] = arr[k];
		else if (k == nBytes)
			out[j] = 0x80;
		else
			out[j] = 0;
		out[j] = out[j] ^ xor_term[j];
	}
}

/* Wrapper for initializing and calling AES-128 */
static int aes128(const uint8_t *K, const uint32_t in[4], uint32_t out[4])
{
	const uint32_t zero[4] = {0, 0, 0, 0};

	if (!DCRYPTO_aes_init((const uint8_t *)K, 128, (const uint8_t *) zero,
				CIPHER_MODE_ECB, ENCRYPT_MODE))
		return 0;
	if (!DCRYPTO_aes_block((const uint8_t *) in, (uint8_t *) out))
		return 0;
	return 1;
}

static int gen_subkey(const uint8_t *K, uint32_t k1[4], uint32_t k2[4])
{
	uint32_t L[4];
	uint32_t tmp[4];
	const uint32_t *xor_term;
	static const uint32_t zero[4] = {0, 0, 0, 0};
	static const uint32_t Rb[4] = {0, 0, 0, 0x87000000};

	if (!aes128(K, zero, L))
		return 0;

	xor_term = (L[0] & 0x00000080) ? Rb : zero;
	shiftl_1((const uint8_t *)L, (uint8_t *)tmp);
	xor128(tmp, xor_term, k1);

	xor_term = (k1[0] & 0x00000080) ? Rb : zero;
	shiftl_1((const uint8_t *) k1, (uint8_t *) tmp);
	xor128(tmp, xor_term, k2);

	return 1;
}

int DCRYPTO_aes_cmac(const uint8_t *K, const uint8_t *M, const uint32_t len,
		uint32_t T[4])
{
	uint32_t n;
	int i;
	int flag;
	uint32_t k1[4];
	uint32_t k2[4];
	uint32_t M_last[4];
	uint32_t Y[4];
	uint32_t X[4] = {0, 0, 0, 0};

	/* Generate the subkeys K1 and K2 */
	if (!gen_subkey(K, k1, k2))
		return 0;

	/* Set n and flag.
	 * flag = 1 if the last block has a full 128 bits; 0 otherwise
	 * n = number of 128-bit blocks in input = ceil (len / BSIZE)
	 *
	 * Special case: if len = 0, then n = 1 and flag = 0.
	 */
	flag = (len % BSIZE == 0) ? 1 : 0;
	n = len / BSIZE + (flag ? 0 : 1); // ceil (len / BSIZE)
	if (len == 0) {
		n = 1;
		flag = 0;
	}

	/* M_last = padded(last 128-bit block of M) ^ (flag ? k1 : k2) */
	get_and_xor(M, len, n-1, (uint8_t *) (flag ? k1 : k2),
			(uint8_t *) M_last);

	for (i = 0; i < n - 1; i++) {
		/* Y = padded(nth 128-bit block of M) ^ (flag ? k1 : k2) */
		get_and_xor(M, len, i, (uint8_t *)X, (uint8_t *)Y);
		if (!aes128(K, Y, X))
			return 0;
	}

	/* TODO: This block is separate from the main loop in the RFC. However,
	 * if we set M[n-1] = M_last, then it is equivalent to running the loop
	 * for one more step, which might be a nicer way to write it.
	 */
	xor128(X, M_last, Y);
	if (!aes128(K, Y, T))
		return 0;
	return 1;
}

int DCRYPTO_aes_cmac_verify(const uint8_t *key, const uint8_t *M, const int len,
		const uint32_t T[4])
{
	int i;
	uint32_t T2[4];
	int match = 1;

	if (!DCRYPTO_aes_cmac(key, M, len, T2))
		return -EC_ERROR_UNKNOWN;

	for (i = 0; i < 4; i++) {
		if (T[i] != T2[i])
			match = 0;
	}
	return match;
}

#ifdef CRYPTO_TEST_SETUP
static int check_answer(const uint32_t expected[4], uint32_t actual[4])
{
	int i;
	int success = 1;

	for (i = 0; i < 4; i++) {
		if (actual[i] != expected[i])
			success = 0;
	}
	if (success) {
		ccprintf("SUCCESS\n");
	} else {
		ccprintf("FAILURE:\n");
		ccprintf("actual   = 0x%08x 0x%08x 0x%08x 0x%08x\n", actual[0],
			 actual[1], actual[2], actual[3]);
		ccprintf("expected = 0x%08x 0x%08x 0x%08x 0x%08x\n",
			 expected[0], expected[1], expected[2], expected[3]);
	}
	return success;
}

static int command_test_aes_block(int argc, char **argv)
{
	uint32_t actual[4];
	const uint32_t zero[4] = {0, 0, 0, 0};
	const uint32_t K[4] = {0x16157e2b, 0xa6d2ae28, 0x8815f7ab, 0x3c4fcf09};
	const uint32_t expected[4] = {0x0c6bf77d, 0xb399b81a, 0x47f0423e,
				      0x6f541bb9};

	aes128((const uint8_t *) K, zero, actual);
	check_answer(expected, actual);

	return 0;
}

DECLARE_SAFE_CONSOLE_COMMAND(test_aesbk, command_test_aes_block, NULL,
			     "Test AES block in AES-CMAC subkey generation");

static int command_test_subkey_gen(int argc, char **argv)
{
	uint32_t k1[4];
	uint32_t k2[4];
	/* K:  2b7e1516 28aed2a6 abf71588 09cf4f3c
	 * k1: fbeed618 35713366 7c85e08f 7236a8de
	 * k2: f7ddac30 6ae266cc f90bc11e e46d513b
	 */
	const uint32_t K[4] = {0x16157e2b, 0xa6d2ae28, 0x8815f7ab, 0x3c4fcf09};
	const uint32_t k1e[4] = {0x18d6eefb, 0x66337135, 0x8fe0857c,
				 0xdea83672};
	const uint32_t k2e[4] = {0x30acddf7, 0xcc66e26a, 0x1ec10bf9,
				 0x3b516de4};

	gen_subkey((const uint8_t *) K, k1, k2);

	ccprintf("Checking K1: ");
	check_answer(k1e, k1);

	ccprintf("Checking K2: ");
	check_answer(k2e, k2);

	return 0;
}

DECLARE_SAFE_CONSOLE_COMMAND(test_skgen, command_test_subkey_gen, NULL,
			     "Test AES-CMAC subkey generation");

struct cmac_test_param {
	uint32_t len;
	uint8_t *M;
	uint32_t Te[4];
};

/* N.B. The order of bytes in each 32-bit block is reversed from the form in
 * which they are written in the RFC.
 */
const struct cmac_test_param rfctests[4] = {
	/*  --------------------------------------------------
	 *  Example 1: len = 0
	 *  M	      <empty string>
	 *  AES-CMAC       bb1d6929 e9593728 7fa37d12 9b756746
	 *  --------------------------------------------------
	 */
	{ .len = 0,
	  .M   = (uint8_t *) "",
	  .Te  = {0x29691dbb, 0x283759e9, 0x127da37f, 0x4667759b},
	  },
	/*  --------------------------------------------------
	 *  Example 2: len = 16
	 *  M	      6bc1bee2 2e409f96 e93d7e11 7393172a
	 *  AES-CMAC       070a16b4 6b4d4144 f79bdd9d d04a287c
	 *  --------------------------------------------------
	 */
	{ .len = 16,
	  .M   = (uint8_t *) (uint32_t[]) {
		0xe2bec16b, 0x969f402e, 0x117e3de9, 0x2a179373
		},
	  .Te  = {0xb4160a07, 0x44414d6b, 0x9ddd9bf7, 0x7c284ad0},
	  },
	/*  --------------------------------------------------
	 *  Example 3: len = 40
	 *  M	      6bc1bee2 2e409f96 e93d7e11 7393172a
	 *	      ae2d8a57 1e03ac9c 9eb76fac 45af8e51
	 *	      30c81c46 a35ce411
	 *  AES-CMAC       dfa66747 de9ae630 30ca3261 1497c827
	 *  --------------------------------------------------
	 */
	{ .len = 40,
	  .M   = (uint8_t *) (uint32_t[]) {
		0xe2bec16b, 0x969f402e, 0x117e3de9, 0x2a179373,
		0x578a2dae, 0x9cac031e, 0xac6fb79e, 0x518eaf45,
		0x461cc830, 0x11e45ca3
		},
	  .Te  = {0x4767a6df, 0x30e69ade, 0x6132ca30, 0x27c89714},
	  },
	/*  --------------------------------------------------
	 *  Example 4: len = 64
	 *  M	      6bc1bee2 2e409f96 e93d7e11 7393172a
	 *	      ae2d8a57 1e03ac9c 9eb76fac 45af8e51
	 *	      30c81c46 a35ce411 e5fbc119 1a0a52ef
	 *	      f69f2445 df4f9b17 ad2b417b e66c3710
	 *  AES-CMAC       51f0bebf 7e3b9d92 fc497417 79363cfe
	 *  --------------------------------------------------
	 */
	{ .len = 64,
	  .M   = (uint8_t *) (uint32_t[]) {
		0xe2bec16b, 0x969f402e, 0x117e3de9, 0x2a179373,
		0x578a2dae, 0x9cac031e, 0xac6fb79e, 0x518eaf45,
		0x461cc830, 0x11e45ca3, 0x19c1fbe5, 0xef520a1a,
		0x45249ff6, 0x179b4fdf, 0x7b412bad, 0x10376ce6
		},
	  .Te  = {0xbfbef051, 0x929d3b7e, 0x177449fc, 0xfe3c3679},
	  },
};

static int command_test_aes_cmac(int argc, char **argv)
{
	int i;
	uint32_t T[4];
	int testN;
	struct cmac_test_param param;
	const uint32_t K[4] = {0x16157e2b, 0xa6d2ae28, 0x8815f7ab, 0x3c4fcf09};

	for (i = 1; i < argc; i++) {
		testN = strtoi(argv[i], NULL, 10);
		param = rfctests[testN - 1];

		ccprintf("Testing RFC Example #%d (%d-byte message)...", testN,
			 param.len);

		DCRYPTO_aes_cmac((const uint8_t *)K, param.M, param.len, T);
		check_answer(param.Te, T);
	}

	return 0;
}

DECLARE_SAFE_CONSOLE_COMMAND(test_cmac, command_test_aes_cmac,
		"[test cases (1-4)]",
		"Test AES-CMAC with RFC examples");

static int command_test_verify(int argc, char **argv)
{
	int i;
	int testN;
	int result;
	struct cmac_test_param param;
	const uint32_t K[4] = {0x16157e2b, 0xa6d2ae28, 0x8815f7ab, 0x3c4fcf09};

	for (i = 1; i < argc; i++) {
		testN = strtoi(argv[i], NULL, 10);
		param = rfctests[testN-1];

		ccprintf("Testing RFC Example #%d (%d-byte message)...", testN,
			 param.len);

		result = DCRYPTO_aes_cmac_verify((const uint8_t *)K, param.M,
				param.len, param.Te);
		if (result == 1)
			ccprintf("SUCCESS\n");
		else if (result == 0)
			ccprintf("FAILURE: verify returned INVALID\n");
		else if (result == -EC_ERROR_UNKNOWN)
			ccprintf("FAILURE: verify returned ERROR\n");
	}

	return 0;
}

DECLARE_SAFE_CONSOLE_COMMAND(test_cmac_ver, command_test_verify,
		"[test cases (1-4)]",
		"Test AES-CMAC-verify with RFC examples");
#endif /* CRYPTO_TEST_SETUP */

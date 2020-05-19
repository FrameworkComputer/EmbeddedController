/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test Base-32 encoding/decoding
 */

#include <stdio.h>
#include "common.h"
#include "base32.h"
#include "test_util.h"
#include "util.h"

static int test_crc5(void)
{
	uint32_t seen;
	int i, j, c;
	int errors = 0;

	/*
	 * For every current CRC value and symbol, new CRC value is unique.
	 * This guarantees a single-character typo will be detected.
	 */
	for (i = 0; i < 32; i++) {
		seen = 0;
		for (j = 0; j < 32; j++)
			seen |= 1 << crc5_sym(j, i);
		TEST_ASSERT(seen == 0xffffffff);
	}

	/*
	 * Do the same in the opposite order, to make sure a subsequent
	 * character doesn't obscure a previous typo.
	 */
	for (i = 0; i < 32; i++) {
		seen = 0;
		for (j = 0; j < 32; j++)
			seen |= 1 << crc5_sym(i, j);
		TEST_ASSERT(seen == 0xffffffff);
	}

	/* Transposing different symbols generates distinct CRCs */
	for (c = 0; c < 32; c++) {
		for (i = 0; i < 32; i++) {
			for (j = i + 1; j < 32; j++) {
				if (crc5_sym(j, crc5_sym(i, c)) ==
				    crc5_sym(i, crc5_sym(j, c)))
					errors++;
			}
		}
	}
	TEST_ASSERT(errors == 0);

	return EC_SUCCESS;
}

static int enctest(const void *src, int srcbits, int crc_every,
		   const char *enc)
{
	char dest[32];

	if (base32_encode(dest, sizeof(dest), src, srcbits, crc_every))
		return -1;
	if (strlen(dest) != strlen(enc) || strncmp(dest, enc, strlen(dest))) {
		fprintf(stderr, "expected encode: \"%s\"\n", enc);
		fprintf(stderr, "got encode:      \"%s\"\n", dest);
		return -2;
	}
	return 0;
}

#define ENCTEST(a, b, c, d) TEST_ASSERT(enctest(a, b, c, d) == 0)

static int test_encode(void)
{
	const uint8_t src1[5] = {0xff, 0x00, 0xff, 0x00, 0xff};
	char enc[32];

	/* Test for enough space; error produces null string */
	*enc = 1;
	TEST_ASSERT(base32_encode(enc, 3, src1, 15, 0) == EC_ERROR_INVAL);
	TEST_ASSERT(*enc == 0);

	/* Empty source */
	ENCTEST("\x00", 0, 0, "");

	/* Single symbol uses top 5 bits */
	ENCTEST("\x07", 5, 0, "A");
	ENCTEST("\xb8", 5, 0, "Z");
	ENCTEST("\xc0", 5, 0, "2");
	ENCTEST("\xf8", 5, 0, "9");

	/* Multiples of 5 bits use top bits */
	ENCTEST("\x08\x86", 10, 0, "BC");
	ENCTEST("\x08\x86", 15, 0, "BCD");

	/* Multiples of 8 bits pad with 0 bits */
	ENCTEST("\xff", 8, 0, "96");
	ENCTEST("\x08\x87", 16, 0, "BCDS");

	/* Multiples of 40 bits use all the bits */
	ENCTEST("\xff\x00\xff\x00\xff", 40, 0, "96AR8AH9");

	/* CRC requires exact multiple of symbol count */
	ENCTEST("\xff\x00\xff\x00\xff", 40, 4, "96ARU8AH9D");
	ENCTEST("\xff\x00\xff\x00\xff", 40, 8, "96AR8AH9L");
	TEST_ASSERT(
	    base32_encode(enc, 16, (uint8_t *)"\xff\x00\xff\x00\xff", 40, 6)
	    == EC_ERROR_INVAL);
	/* But what matters is symbol count, not bit count */
	ENCTEST("\xff\x00\xff\x00\xfe", 39, 4, "96ARU8AH8P");

	return EC_SUCCESS;
}

static int cmpbytes(const uint8_t *expect, const uint8_t *got, int len,
		    const char *desc)
{
	int i;

	if (!memcmp(expect, got, len))
		return 0;

	fprintf(stderr, "expected %s:", desc);
	for (i = 0; i < len; i++)
		fprintf(stderr, " %02x", expect[i]);
	fprintf(stderr, "\ngot %s:     ", desc);
	for (i = 0; i < len; i++)
		fprintf(stderr, " %02x", got[i]);
	fprintf(stderr, "\n");

	return -2;
}

static int dectest(const void *dec, int decbits, int crc_every, const char *enc)
{
	uint8_t dest[32];
	int destbits = decbits > 0 ? decbits : sizeof(dest) * 8;
	int wantbits = decbits > 0 ? decbits : 5 * strlen(enc);
	int gotbits = base32_decode(dest, destbits, enc, crc_every);

	TEST_ASSERT(gotbits == wantbits);
	if (gotbits != wantbits)
		return -1;
	return cmpbytes(dec, dest, (decbits + 7) / 8, "decode");
}

#define DECTEST(a, b, c, d) TEST_ASSERT(dectest(a, b, c, d) == 0)

static int test_decode(void)
{
	uint8_t dec[32];

	/* Decode tests, dest-limited */
	DECTEST("\xf8", 5, 0, "97");
	DECTEST("\x08", 5, 0, "BCDS");
	DECTEST("\x08\x80", 10, 0, "BCDS");
	DECTEST("\x08\x86", 15, 0, "BCDS");
	DECTEST("\xff", 8, 0, "96");
	DECTEST("\x08\x87", 16, 0, "BCDS");
	DECTEST("\xff\x00\xff\x00\xff", 40, 0, "96AR8AH9");
	DECTEST("\xff\x00\xff\x00\xfe", 39, 4, "96ARU8AH8P");

	/* Decode ignores whitespace and dashes */
	DECTEST("\xff\x00\xff\x00\xff", 40, 0, " 96\tA-R\r8A H9\n");

	/* Invalid symbol fails */
	TEST_ASSERT(base32_decode(dec, 16, "AI", 0) == -1);

	/* If dest buffer is big, use all the source bits */
	DECTEST("", 0, 0, "");
	DECTEST("\xf8", 0, 0, "9");
	DECTEST("\x07\xc0", 0, 0, "A9");
	DECTEST("\x00\x3e", 0, 0, "AA9");
	DECTEST("\x00\x01\xf0", 0, 0, "AAA9");
	DECTEST("\xff\x00\xff\x00\xff", 0, 0, "96AR8AH9");

	/* Decode always overwrites destination */
	memset(dec, 0xff, sizeof(dec));
	DECTEST("\x00\x00\x00\x00\x00", 0, 0, "AAAAAAAA");
	memset(dec, 0x00, sizeof(dec));
	DECTEST("\xff\xff\xff\xff\xff", 0, 0, "99999999");

	/* Good CRCs */
	DECTEST("\xff\x00\xff\x00\xff", 40, 4, "96ARU8AH9D");
	DECTEST("\xff\x00\xff\x00\xff", 40, 8, "96AR8AH9L");

	/* CRC requires exact multiple of symbol count */
	TEST_ASSERT(base32_decode(dec, 40, "96ARL8AH9", 4) == -1);
	/* But what matters is symbol count, not bit count */
	DECTEST("\xff\x00\xff\x00\xfe", 39, 4, "96ARU8AH8P");

	/* Detect errors in data, CRC, and transposition */
	TEST_ASSERT(base32_decode(dec, 40, "96AQL", 4) == -1);
	TEST_ASSERT(base32_decode(dec, 40, "96ARM", 4) == -1);
	TEST_ASSERT(base32_decode(dec, 40, "96RAL", 4) == -1);

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_crc5);
	RUN_TEST(test_encode);
	RUN_TEST(test_decode);

	test_print_result();
}

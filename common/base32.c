/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Base-32 encoding/decoding */

#include "common.h"
#include "base32.h"
#include "util.h"

static const unsigned char crc5_table1[] = {
	0x00, 0x0E, 0x1C, 0x12, 0x11, 0x1F, 0x0D, 0x03,
	0x0B, 0x05, 0x17, 0x19, 0x1A, 0x14, 0x06, 0x08
};

static const unsigned char crc5_table0[] = {
	0x00, 0x16, 0x05, 0x13, 0x0A, 0x1C, 0x0F, 0x19,
	0x14, 0x02, 0x11, 0x07, 0x1E, 0x08, 0x1B, 0x0D
};

uint8_t crc5_sym(uint8_t sym, uint8_t previous_crc)
{
	uint8_t tmp = sym ^ previous_crc;
	return crc5_table1[tmp & 0x0F] ^ crc5_table0[(tmp >> 4) & 0x0F];
}

/* A-Z0-9 with I,O,0,1 removed */
const char base32_map[33] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

/**
 * Decode a base32 symbol.
 *
 * @param sym Input symbol
 * @return The symbol value or -1 if error.
 */
static int decode_sym(int sym)
{
	int i = 0;

	for (i = 0; i < 32; i++) {
		if (sym == base32_map[i])
			return i;
	}

	return -1;
}

int base32_encode(char *dest, int destlen_chars,
		  const void *srcbits, int srclen_bits,
		  int add_crc_every)
{
	const uint8_t *src = srcbits;
	int destlen_needed;
	int crc = 0, crc_count = 0;
	int didx = 0;
	int i;

	*dest = 0;

	/* Make sure destination is big enough */
	destlen_needed = (srclen_bits + 4) / 5;  /* Symbols before adding CRC */
	if (add_crc_every) {
		/* Must be an exact number of groups to add CRC */
		if (destlen_needed % add_crc_every)
			return EC_ERROR_INVAL;
		destlen_needed += destlen_needed / add_crc_every;
	}
	destlen_needed++;  /* For terminating null */
	if (destlen_chars < destlen_needed)
		return EC_ERROR_INVAL;

	for (i = 0; i < srclen_bits; i += 5) {
		int sym;
		int sidx = i / 8;
		int bit_offs = i % 8;

		if (bit_offs <= 3) {
			/* Entire symbol fits in that byte */
			sym = src[sidx] >> (3 - bit_offs);
		} else {
			/* Use the bits we have left */
			sym = src[sidx] << (bit_offs - 3);

			/* Use the bits from the next byte, if any */
			if (i + 1 < srclen_bits)
				sym |= src[sidx + 1] >> (11 - bit_offs);
		}

		sym &= 0x1f;

		/* Pad incomplete symbol with 0 bits */
		if (srclen_bits - i < 5)
			sym &= 0x1f << (5 + i - srclen_bits);

		dest[didx++] = base32_map[sym];

		/* Add CRC if needed */
		if (add_crc_every) {
			crc = crc5_sym(sym, crc);
			if (++crc_count == add_crc_every) {
				dest[didx++] = base32_map[crc];
				crc_count = crc = 0;
			}
		}
	}

	/* Terminate string and return */
	dest[didx] = 0;
	return EC_SUCCESS;
}

int base32_decode(uint8_t *dest, int destlen_bits, const char *src,
		  int crc_after_every)
{
	int crc = 0, crc_count = 0;
	int out_bits = 0;

	for (; *src; src++) {
		int sym, sbits, dbits, b;

		if (isspace(*src) || *src == '-')
			continue;

		sym = decode_sym(*src);
		if (sym < 0)
			return -1;  /* Bad input symbol */

		/* Check CRC if needed */
		if (crc_after_every) {
			if (crc_count == crc_after_every) {
				if (crc != sym)
					return -1;
				crc_count = crc = 0;
				continue;
			} else {
				crc = crc5_sym(sym, crc);
				crc_count++;
			}
		}

		/*
		 * Stop if we're out of space.  Have to do this after checking
		 * the CRC, or we might not check the last CRC.
		 */
		if (out_bits >= destlen_bits)
			break;

		/* See how many bits we get to use from this symbol */
		sbits = MIN(5, destlen_bits - out_bits);
		if (sbits < 5)
			sym >>= (5 - sbits);

		/* Fill up the rest of the current byte */
		dbits = 8 - (out_bits & 7);
		b = MIN(dbits, sbits);
		if (dbits == 8)
			dest[out_bits / 8] = 0;  /* Starting a new byte */
		dest[out_bits / 8] |= (sym << (dbits - b)) >> (sbits - b);
		out_bits += b;
		sbits -= b;

		/* Start the next byte if there's space */
		if (sbits > 0) {
			dest[out_bits / 8] = sym << (8 - sbits);
			out_bits += sbits;
		}
	}

	/* If we have CRCs, should have a full group */
	if (crc_after_every && crc_count)
		return -1;

	return out_bits;
}

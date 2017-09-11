/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Base-32 encoding/decoding, designed for manual operator entry. */

#ifndef __CROS_EC_BASE32_H
#define __CROS_EC_BASE32_H

/* Symbol map for base32 encoding */
extern const char base32_map[33];

/**
 * CRC-5-USB Initially created for USB Token Packets. It uses
 * the generator polynomial X^5 + X^2 + X^0 and is 5-bits.
 *
 * @param sym		New symbol to update CRC with
 * @param previous_crc	Existing CRC value
 * @return The updated CRC.
 */
uint8_t crc5_sym(uint8_t sym, uint8_t previous_crc);

/**
 * base32-encode data into a null-terminated string
 *
 * Uses A-Z0-9 encoding, skipping I,O,0,1 since they're easy to get mixed up.
 *
 * @param dest			Destination buffer; set to empty string on
 *				error
 * @param destlen_chars		Length of destination buffer in characters
 * @param src			Source binary data
 * @param srclen_bits		Length of source *in bits*.  If this is not a
 *				multiple of 8, the *most significant* bits of
 *				the last byte will be used.  If this is not a
 *				multiple of 5, the least significant bits of
 *				the last symbol will be padded with 0 bits.
 * @param add_crc_every		If non-zero, add a CRC symbol after each group
 *				of this many symbols.  There must be an exact
 *				number of groups; that is, ceil(srclen_bits/5)
 *				must be a multiple of add_crc_every.
 * @return EC_SUCCESS, or non-zero error code.
 */
int base32_encode(char *dest, int destlen_chars,
		  const void *srcbits, int srclen_bits,
		  int add_crc_every);

/**
 * base32-decode data from a null-terminated string
 *
 * Ignores whitespace and '-' dashes in the source string.
 *
 * If the destination is smaller than the decoded bitstream, only that many
 * bits will be decoded.  This is useful for decoding the first part of a
 * bitstream to look for a struct version.
 *
 * If the destination is larger than the decoded bitstream, check the return
 * value to determine how many bits were decoded from the source.  Note that if
 * padding was added by base32_encode (that is, the input length was not a
 * multiple of 5 bits), the padding will be included in the count.
 *
 * @param dest			Destination; must be at least
 *				ceil(destlen_bits/8) bytes.
 * @param destlen_bits		Length of destination *in bits*.
 * @param src			Source string (null-terminated)
 * @param crc_after_every	If non-zero, expect CRC symbol after every
 *				group of this many symbols.
 * @return Number of decoded *bits*, or -1 if error.
 */
int base32_decode(uint8_t *dest, int destlen_bits, const char *src,
		  int crc_after_every);

#endif

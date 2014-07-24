/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Very simple 8-bit CRC function.
 */
#ifndef __EC_CRC8_H__
#define __EC_CRC8_H__

/**
 * crc8
 * Return CRC-8 of the data, using x^8 + x^2 + x + 1 polynomial.  A table-based
 * algorithm would be faster, but for only a few bytes it isn't worth the code
 * size.
 * @param data uint8_t *, input, a pointer to input data
 * @param len int, input, size of iput data in byte
 * @return the crc-8 of the input data.
 */
uint8_t crc8(const uint8_t *data, int len);

#endif /* __EC_CRC8_H__ */

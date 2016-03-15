/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Very simple 8-bit CRC function.
 */
#ifndef __CROS_EC_CRC8_H
#define __CROS_EC_CRC8_H

/**
 * crc8
 * Return CRC-8 of the data, using x^8 + x^2 + x + 1 polynomial.  A table-based
 * algorithm would be faster, but for only a few bytes it isn't worth the code
 * size.
 * @param data uint8_t *, input, a pointer to input data
 * @param len int, input, size of input data in bytes
 * @return the crc-8 of the input data.
 */
uint8_t crc8(const uint8_t *data, int len);

/**
 * crc8_arg
 * Return CRC-8 of the data, based upon pre-calculated partial CRC of previous
 * data.
 * @param data uint8_t *, input, a pointer to input data
 * @param len int, input, size of input data in bytes
 * @param previous_crc uint8_t, input, pre-calculated CRC of previous data.
 *        Seed with zero for a new calculation (or use the result of crc8()).
 * @return the crc-8 of the input data.
 */
uint8_t crc8_arg(const uint8_t *data, int len, uint8_t previous_crc);

#endif /* __CROS_EC_CRC8_H */

/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Coral SKU ID Table */

#ifndef __CROS_EC_SKU_H
#define __CROS_EC_SKU_H

#define SKU_CONVERTIBLE(id) (1 << ((id) & 0x7))

/*
 * There are 256 possible SKUs for Coral. This table is used to map a given SKU
 * ID to its form factor, which is then used to determine number of motion
 * sensors. A bit value of 0 is for clamshell and a bit value of 1 indicates a
 * convertible device. The assumption is all devices are defined as clamshells
 * unless SKU_CONVERTIBLE(id) is spelled out in the initialization.
 */
static const uint8_t form_factor[32] = {
	/* SKU 0 - 7 */
	SKU_CONVERTIBLE(4) | SKU_CONVERTIBLE(5),
	/* SKU 8 - 15 */
	SKU_CONVERTIBLE(8) | SKU_CONVERTIBLE(9) | SKU_CONVERTIBLE(10) |
		SKU_CONVERTIBLE(11),
	/* SKU 16 - 23 */
	0x00,
	/* SKU 24 - 31 */
	0x00,
	/* SKU 32 - 39 */
	0x00,
	/* SKU 40 - 47 */
	0x00,
	/* SKU 48 - 55 */
	0x00,
	/* SKU 56 - 63 */
	0x00,
	/* SKU 64 - 71 */
	SKU_CONVERTIBLE(71),
	/* SKU 72 - 79 */
	0x00,
	/* SKU 80 - 87 */
	0x00,
	/* SKU 88 - 95 */
	0x00,
	/* SKU 96 - 103 */
	0x00,
	/* SKU 104 - 111 */
	0x00,
	/* SKU 112 - 119 */
	0x00,
	/* SKU 120 - 127 */
	0x00,
	/* SKU 128 - 135 */
	0x00,
	/* SKU 136 - 143 */
	0x00,
	/* SKU 144 - 151 */
	0x00,
	/* SKU 152 - 159 */
	0x00,
	/* SKU 160 - 167 */
	SKU_CONVERTIBLE(163) | SKU_CONVERTIBLE(164) | SKU_CONVERTIBLE(165) |
		SKU_CONVERTIBLE(166),
	/* SKU 168 - 175 */
	0x00,
	/* SKU 176 - 183 */
	0x00,
	/* SKU 184 - 191 */
	0x00,
	/* SKU 192 - 199 */
	0x00,
	/* SKU 200 - 207 */
	0x00,
	/* SKU 208 - 215 */
	0x00,
	/* SKU 216 - 223 */
	0x00,
	/* SKU 224 - 231 */
	0x00,
	/* SKU 232 - 239 */
	0x00,
	/* SKU 240 - 247 */
	0x00,
	/* SKU 248 - 255 */
	0x00,
};

#define SKU_IS_CONVERTIBLE(id) ((form_factor[(id) >> 3] >> ((id) & 0x7)) & 1)

#endif /* __CROS_EC_SKU_H */

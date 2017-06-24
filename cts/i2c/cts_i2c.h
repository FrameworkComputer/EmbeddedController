/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

enum cts_i2c_packets {
	WRITE8_OFF,
	WRITE16_OFF,
	WRITE32_OFF,
	READ8_OFF,
	READ16_OFF,
	READ32_OFF,
};

#define WRITE8_DATA	0x42
#define WRITE16_DATA	0x1234
#define WRITE32_DATA	0xDEADBEEF
#define READ8_DATA	0x23
#define READ16_DATA	0xACED
#define READ32_DATA	0x01ABCDEF

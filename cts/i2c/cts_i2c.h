/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

enum cts_i2c_packets {
	WRITE_8_OFFSET = 0,
	WRITE_16_OFFSET = 1,
	WRITE_32_OFFSET = 2,
	READ_8_OFFSET = 3,
	READ_16_OFFSET = 4,
	READ_32_OFFSET = 5,
};

#define WRITE_8_DATA 0x42
#define WRITE_16_DATA 0x1234
#define WRITE_32_DATA 0xDEADBEEF
#define READ_8_DATA 0x23
#define READ_16_DATA 0xACED
#define READ_32_DATA 0x01ABCDEF

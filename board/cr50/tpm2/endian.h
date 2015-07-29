/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_BOARD_CR50_TPM2_ENDIAN_H
#define __EC_BOARD_CR50_TPM2_ENDIAN_H

#include <stddef.h>
#include <stdint.h>

static inline void swap_n(void *in, void *out, size_t size)
{
	int i;

	for (i = 0; i < size; i++)
		((uint8_t *)out)[size - i - 1] = ((uint8_t *)in)[i];
}

static inline uint16_t be16toh(uint16_t in)
{
	uint16_t out;

	swap_n(&in, &out, sizeof(out));
	return out;
}

static inline uint32_t be32toh(uint32_t in)
{
	uint32_t out;

	swap_n(&in, &out, sizeof(out));
	return out;
}

static inline uint64_t be64toh(uint64_t in)
{
	uint64_t out;

	swap_n(&in, &out, sizeof(out));
	return out;
}

#define htobe16 be16toh
#define htobe32 be32toh
#define htobe64 be64toh

#endif /* __EC_BOARD_CR50_TPM2_ENDIAN_H */

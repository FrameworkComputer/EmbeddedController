/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_BUILTIN_ENDIAN_H
#define __EC_BUILTIN_ENDIAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Functions to convert byte order in various sized big endian integers to
 * host byte order. Note that the code currently does not require functions
 * for converting little endian integers.
 */
#if (__BYTE_ORDER__  == __ORDER_LITTLE_ENDIAN__)

static inline uint16_t be16toh(uint16_t in)
{
	return __builtin_bswap16(in);
}
static inline uint32_t be32toh(uint32_t in)
{
	return __builtin_bswap32(in);
}
static inline uint64_t be64toh(uint64_t in)
{
	return __builtin_bswap64(in);
}

#define htobe16 be16toh
#define htobe32 be32toh
#define htobe64 be64toh

#endif  /* __BYTE_ORDER__  == __ORDER_LITTLE_ENDIAN__ */

#ifdef __cplusplus
}
#endif

#endif  /* __EC_BUILTIN_ENDIAN_H */

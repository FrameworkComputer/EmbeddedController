/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CRC_H
#define __CROS_EC_CRC_H
/* CRC-32 implementation with USB constants */
/* Note: it's a stateful CRC-32 to match the hardware block interface */

#ifdef CONFIG_HW_CRC
#include "crc_hw.h"
#else

/* Use software implementation */

/* Static context variant */

void crc32_init(void);

/**
 * Calculate CRC32 of data in arbitrary length.
 *
 * @param buf   Data for CRC32 to be calculated for.
 * @param size  Size of <buf> in bytes.
 */
void crc32_hash(const void *buf, int size);

void crc32_hash32(uint32_t val);

void crc32_hash16(uint16_t val);

uint32_t crc32_result(void);

/* Provided context variant */

void crc32_ctx_init(uint32_t *ctx);

/**
 * Calculate CRC32 of data in arbitrary length using given context.
 *
 * @param crc   CRC32 context.
 * @param buf   Data for CRC32 to be calculated for.
 * @param size  Size of <buf> in bytes.
 */
void crc32_ctx_hash(uint32_t *crc, const void *buf, int size);

void crc32_ctx_hash32(uint32_t *ctx, uint32_t val);

void crc32_ctx_hash16(uint32_t *ctx, uint16_t val);

void crc32_ctx_hash8(uint32_t *ctx, uint8_t val);

uint32_t crc32_ctx_result(uint32_t *ctx);

#endif /* CONFIG_HW_CRC */

#endif /* __CROS_EC_CRC_H */

/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CRC_H
#define __CROS_EC_CRC_H
/* CRC-32 implementation with USB constants */
/* Note: it's a stateful CRC-32 to match the hardware block interface */

#if defined(CONFIG_HW_CRC) && !defined(HOST_TOOLS_BUILD)
#include "crc_hw.h"
#else
#ifdef CONFIG_ZEPHYR
#include <stdint.h>
#endif /* CONFIG_ZEPHYR */

#ifndef HOST_TOOLS_BUILD
#ifdef __cplusplus
extern "C" {
#endif
#endif

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

/**
 * Return CRC-16 of the data using X^16 + X^15 + X^2 + 1 polynomial, based
 * upon pre-calculated partial CRC of previous data.
 * @param data uint8_t *, input, a pointer to input data
 * @param len int, input, size of input data in bytes
 * @param previous_crc uint16_t, input, pre-calculated CRC of previous data.
 *        Seed with zero for a new calculation.
 * @return the crc-16 of the input data.
 */
uint16_t cros_crc16(const uint8_t *data, int len, uint16_t previous_crc);

#ifndef HOST_TOOLS_BUILD
#ifdef __cplusplus
}
#endif
#endif

#endif /* CONFIG_HW_CRC && !HOST_TOOLS_BUILD */

#endif /* __CROS_EC_CRC_H */

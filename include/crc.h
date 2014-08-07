/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CRC_H
#define _CRC_H
/* CRC-32 implementation with USB constants */
/* Note: it's a stateful CRC-32 to match the hardware block interface */

#ifdef CONFIG_HW_CRC
#include "crc_hw.h"
#else

/* Use software implementation */

void crc32_init(void);

void crc32_hash32(uint32_t val);

void crc32_hash16(uint16_t val);

uint32_t crc32_result(void);

#endif /* CONFIG_HW_CRC */

#endif /* _CRC_H */

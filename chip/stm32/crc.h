/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CRC_H
#define _CRC_H
/* CRC-32 implementation with USB constants */

/* Note: it's a stateful CRC-32 to match the hardware block interface */

#ifdef CONFIG_HW_CRC

#include "registers.h"

static inline void crc32_init(void)
{
	/* switch on CRC controller */
	STM32_RCC_AHBENR |= 1 << 6; /* switch on CRC controller */
	/* reset CRC state */
	STM32_CRC_CR = STM32_CRC_CR_RESET | STM32_CRC_CR_REV_OUT
		     | STM32_CRC_CR_REV_IN_WORD;
	while (STM32_CRC_CR & 1)
		;
}

static inline void crc32_hash32(uint32_t val)
{
	STM32_CRC_DR = val;
}

static inline void crc32_hash16(uint16_t val)
{
	STM32_CRC_DR16 = val;
}

static inline uint32_t crc32_result(void)
{
	return STM32_CRC_DR ^ 0xFFFFFFFF;
}

#else /* !CONFIG_HW_CRC */
/* Use software implementation */
void crc32_init(void);
void crc32_hash32(uint32_t val);
void crc32_hash16(uint16_t val);
uint32_t crc32_result(void);
#endif /* CONFIG_HW_CRC */

#endif /* _CRC_H */

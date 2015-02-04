/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CRC_HW_H
#define _CRC_HW_H
/* CRC-32 hardware implementation with USB constants */

#include "clock.h"
#include "registers.h"

static inline void crc32_init(void)
{
	/* switch on CRC controller */
	STM32_RCC_AHBENR |= 1 << 6; /* switch on CRC controller */
	/* Delay 1 AHB clock cycle after the clock is enabled */
	clock_wait_bus_cycles(BUS_AHB, 1);
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

#endif /* _CRC_HW_H */

/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __STM32F4xx_LL_RCC_H
#define __STM32F4xx_LL_RCC_H

/* Add definitions of types normally provided by the STM32 HAL.
 * Tests don't use HAL, all needed functions are mocked.
 */
#define LL_RCC_MCO2SOURCE_HSE 0x44
#define LL_RCC_MCO2_DIV_1 0xf4

void LL_RCC_ConfigMCO(uint32_t MCOxSource, uint32_t MCOxPrescaler);

#endif

/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SYSTEM_CHIP_H_
#define __CROS_EC_SYSTEM_CHIP_H_

#define SET_BIT(reg, bit) ((reg) |= (0x1 << (bit)))
#define CLEAR_BIT(reg, bit) ((reg) &= (~(0x1 << (bit))))

/******************************************************************************/
/* Optional M4 Registers */
#define CPU_MPU_CTRL REG32(0xE000ED94)
#define CPU_MPU_RNR REG32(0xE000ED98)
#define CPU_MPU_RBAR REG32(0xE000ED9C)
#define CPU_MPU_RASR REG32(0xE000EDA0)

void system_download_from_flash(uint32_t srcAddr, uint32_t dstAddr,
				uint32_t size, uint32_t exeAddr);

/* Begin and end address for little FW; defined in linker script */
extern unsigned int __flash_lplfw_start;
extern unsigned int __flash_lplfw_end;

#endif /* __CROS_EC_SYSTEM_CHIP_H_ */

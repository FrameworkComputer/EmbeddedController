/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Control and status register */

/* TODO: move to core/riscv-rv32i? */

#ifndef __CROS_EC_CSR_H
#define __CROS_EC_CSR_H

#include "common.h"

static inline uint32_t read_csr(uint32_t reg)
{
	uint32_t val;

	asm volatile("csrr %0, %1" : "=r"(val) : "i"(reg));
	return val;
}

static inline void write_csr(uint32_t reg, uint32_t val)
{
	asm volatile("csrw %0, %1" ::"i"(reg), "r"(val));
}

static inline uint32_t set_csr(uint32_t reg, uint32_t bit)
{
	uint32_t val;

	asm volatile("csrrs %0, %1, %2" : "=r"(val) : "i"(reg), "r"(bit));
	return val;
}

static inline uint32_t clear_csr(uint32_t reg, uint32_t bit)
{
	uint32_t val;

	asm volatile("csrrc %0, %1, %2" : "=r"(val) : "i"(reg), "r"(bit));
	return val;
}

/* VIC */
#ifdef CHIP_FAMILY_RV55
#define CSR_VIC_MICAUSE (0x5c5)
#define CSR_VIC_MILMS_G (0x5c4)
#else
#define CSR_VIC_MICAUSE (0x5c0)
#define CSR_VIC_MIEMS (0x5c2)
#endif
#define CSR_VIC_MIPEND_G0 (0x5d0)
#define CSR_VIC_MIMASK_G0 (0x5d8)
#define CSR_VIC_MIWAKEUP_G0 (0x5e0)
#define CSR_VIC_MILSEL_G0 (0x5e8)
#define CSR_VIC_MIEMASK_G0 (0x5f0)

/* centralized control enable */
#define CSR_MCTREN (0x7c0)
/* I$, D$, ITCM, DTCM, BTB, RAS, VIC, CG, mpu */
#define CSR_MCTREN_ICACHE BIT(0)
#define CSR_MCTREN_DCACHE BIT(1)
#define CSR_MCTREN_ITCM BIT(2)
#define CSR_MCTREN_DTCM BIT(3)
#define CSR_MCTREN_BTB BIT(4)
#ifdef CHIP_FAMILY_RV55
#define CSR_MCTREN_TLP BIT(5)
#else
#define CSR_MCTREN_RAS BIT(5)
#endif
#define CSR_MCTREN_VIC BIT(6)
#define CSR_MCTREN_CG BIT(7)
#define CSR_MCTREN_MPU BIT(8)

/* MPU */
#define CSR_MPU_ENTRY_EN (0x9c0)
#define CSR_MPU_LITCM (0x9dc)
#define CSR_MPU_LDTCM (0x9dd)
#define CSR_MPU_HITCM (0x9de)
#define CSR_MPU_HDTCM (0x9df)
#define CSR_MPU_L(n) (0x9e0 + (n))
#define CSR_MPU_H(n) (0x9f0 + (n))
/* MPU attributes: set if permitted */
/* Privilege, machine mode in RISC-V.  We don't use the flag because
 * we don't separate user / machine mode in EC OS. */
#define MPU_ATTR_P BIT(5)
/* Readable */
#define MPU_ATTR_R BIT(6)
/* Writable */
#define MPU_ATTR_W BIT(7)
/* Cacheable */
#define MPU_ATTR_C BIT(8)
/* Bufferable */
#define MPU_ATTR_B BIT(9)

/* PMU */
#define CSR_PMU_MPMUCTR (0xbc0)
#define CSR_PMU_MPMUCTR_C BIT(0)
#define CSR_PMU_MPMUCTR_I BIT(1)
#define CSR_PMU_MPMUCTR_H3 BIT(2)
#define CSR_PMU_MPMUCTR_H4 BIT(3)
#define CSR_PMU_MPMUCTR_H5 BIT(4)

#define CSR_PMU_MCYCLE (0xb00)
#define CSR_PMU_MINSTRET (0xb02)
#define CSR_PMU_MHPMCOUNTER3 (0xb03)
#define CSR_PMU_MHPMCOUNTER4 (0xb04)
#define CSR_PMU_MHPMCOUNTER5 (0xb05)

#define CSR_PMU_MCYCLEH (0xb80)
#define CSR_PMU_MINSTRETH (0xb82)
#define CSR_PMU_MHPMCOUNTER3H (0xb83)
#define CSR_PMU_MHPMCOUNTER4H (0xb84)
#define CSR_PMU_MHPMCOUNTER5H (0xb85)

#define CSR_PMU_MHPMEVENT3 (0x323)
#define CSR_PMU_MHPMEVENT4 (0x324)
#define CSR_PMU_MHPMEVENT5 (0x325)

#endif /* __CROS_EC_CSR_H */

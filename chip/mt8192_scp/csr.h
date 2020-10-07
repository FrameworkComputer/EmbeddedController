/* Copyright 2020 The Chromium OS Authors. All rights reserved.
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
	asm volatile ("csrw %0, %1" :: "i"(reg), "r"(val));
}

static inline uint32_t set_csr(uint32_t reg, uint32_t bit)
{
	uint32_t val;

	asm volatile ("csrrs %0, %1, %2" : "=r"(val) : "i"(reg), "r"(bit));
	return val;
}

static inline uint32_t clear_csr(uint32_t reg, uint32_t bit)
{
	uint32_t val;

	asm volatile ("csrrc %0, %1, %2" : "=r"(val) : "i"(reg), "r"(bit));
	return val;
}

/* VIC */
#define CSR_VIC_MICAUSE		(0x5c0)
#define CSR_VIC_MIEMS		(0x5c2)
#define CSR_VIC_MIPEND_G0	(0x5d0)
#define CSR_VIC_MIMASK_G0	(0x5d8)
#define CSR_VIC_MIWAKEUP_G0	(0x5e0)
#define CSR_VIC_MILSEL_G0	(0x5e8)
#define CSR_VIC_MIEMASK_G0	(0x5f0)

/* centralized control enable */
#define CSR_MCTREN		(0x7c0)
/* I$, D$, ITCM, DTCM, BTB, RAS, VIC, CG, mpu */
#define   CSR_MCTREN_ICACHE	BIT(0)
#define   CSR_MCTREN_DCACHE	BIT(1)
#define   CSR_MCTREN_ITCM	BIT(2)
#define   CSR_MCTREN_DTCM	BIT(3)
#define   CSR_MCTREN_BTB	BIT(4)
#define   CSR_MCTREN_RAS	BIT(5)
#define   CSR_MCTREN_VIC	BIT(6)
#define   CSR_MCTREN_CG		BIT(7)
#define   CSR_MCTREN_MPU	BIT(8)

#endif /* __CROS_EC_CSR_H */

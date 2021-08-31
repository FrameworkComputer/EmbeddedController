/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * @file
 * @brief ITE it8xxx2 register structure definitions used by the Chrome OS EC.
 */

#ifndef _ITE_IT8XXX2_REG_DEF_CROS_H
#define _ITE_IT8XXX2_REG_DEF_CROS_H

/*
 * ECPM (EC Clock and Power Management) device registers
 */
struct ecpm_reg {
	/* 0x000: Reserved1 */
	volatile uint8_t reserved1;
	/* 0x001: Clock Gating Control 1 */
	volatile uint8_t ECPM_CGCTRL1;
	/* 0x002: Clock Gating Control 2 */
	volatile uint8_t ECPM_CGCTRL2;
	/* 0x003: PLL Control */
	volatile uint8_t ECPM_PLLCTRL;
	/* 0x004: Auto Clock Gating */
	volatile uint8_t ECPM_AUTOCG;
	/* 0x005: Clock Gating Control 3 */
	volatile uint8_t ECPM_CGCTRL3;
	/* 0x006: PLL Frequency */
	volatile uint8_t ECPM_PLLFREQ;
	/* 0x007: Reserved2 */
	volatile uint8_t reserved2;
	/* 0x008: PLL Clock Source Status */
	volatile uint8_t ECPM_PLLCSS;
	/* 0x009: Clock Gating Control 4 */
	volatile uint8_t ECPM_CGCTRL4;
	/* 0x00A: Reserved3 */
	volatile uint8_t reserved3;
	/* 0x00B: Reserved4 */
	volatile uint8_t reserved4;
	/* 0x00C: System Clock Divide Control 0 */
	volatile uint8_t ECPM_SCDCR0;
	/* 0x00D: System Clock Divide Control 1 */
	volatile uint8_t ECPM_SCDCR1;
	/* 0x00E: System Clock Divide Control 2 */
	volatile uint8_t ECPM_SCDCR2;
	/* 0x00F: System Clock Divide Control 3 */
	volatile uint8_t ECPM_SCDCR3;
	/* 0x010: System Clock Divide Control 4 */
	volatile uint8_t ECPM_SCDCR4;
};

#endif /* _ITE_IT8XXX2_REG_DEF_CROS_H */

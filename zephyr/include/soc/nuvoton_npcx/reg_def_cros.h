/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * @file
 * @brief Nuvoton NPCX register structure definitions used by the Chrome OS EC.
 */

#ifndef _NUVOTON_NPCX_REG_DEF_CROS_H
#define _NUVOTON_NPCX_REG_DEF_CROS_H

/*
 * Monotonic Counter (MTC) device registers
 */
struct mtc_reg {
	/* 0x000: Timing Ticks Count Register */
	volatile uint32_t TTC;
	/* 0x004: Wake-Up Ticks Count Register */
	volatile uint32_t WTC;
};

/* MTC register fields */
#define NPCX_WTC_PTO 30
#define NPCX_WTC_WIE 31

#endif /* _NUVOTON_NPCX_REG_DEF_CROS_H */

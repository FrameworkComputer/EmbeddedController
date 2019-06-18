/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific MFT module for Chrome EC */

#ifndef __CROS_EC_FAN_CHIP_H
#define __CROS_EC_FAN_CHIP_H

/* MFT mode select */
enum npcx_mft_mdsel {
	NPCX_MFT_MDSEL_1,
	NPCX_MFT_MDSEL_2,
	NPCX_MFT_MDSEL_3,
	NPCX_MFT_MDSEL_4,
	NPCX_MFT_MDSEL_5,
	/* Number of MFT modes */
	NPCX_MFT_MDSEL_COUNT
};

/* MFT module select */
enum npcx_mft_module {
	NPCX_MFT_MODULE_1,
	NPCX_MFT_MODULE_2,
	NPCX_MFT_MODULE_3,
	/* Number of MFT modules */
	NPCX_MFT_MODULE_COUNT
};

/* MFT clock source */
enum npcx_mft_clk_src {
	TCKC_NOCLK = 0,
	TCKC_PRESCALE_APB1_CLK = 1,
	TCKC_LFCLK = 5,
};

/* Data structure to define MFT channels. */
struct mft_t {
	/* MFT module ID */
	enum npcx_mft_module module;
	/* MFT clock source */
	enum npcx_mft_clk_src clk_src;
	/* PWM id */
	int pwm_id;
};

extern const struct mft_t mft_channels[];

#endif /* __CROS_EC_FAN_CHIP_H */

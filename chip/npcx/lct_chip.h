/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_LCT_CHIP_H
#define __CROS_EC_LCT_CHIP_H
#include "registers.h"

enum NPCX_LCT_PWR_SRC {
	NPCX_LCT_PWR_SRC_VCC1,
	NPCX_LCT_PWR_SRC_VSBY
};

void npcx_lct_config(int seconds, int psl_ena, int int_ena);
void npcx_lct_enable(uint8_t enable);
void npcx_lct_enable_clk(uint8_t enable);
void npcx_lct_sel_power_src(enum NPCX_LCT_PWR_SRC pwr_src);
void npcx_lct_clear_event(void);
int npcx_lct_is_event_set(void);

#endif  /* __CROS_EC_LCT_CHIP_H */

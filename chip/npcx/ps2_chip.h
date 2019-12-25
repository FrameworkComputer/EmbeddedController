/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PS2_CHIP_H
#define __CROS_EC_PS2_CHIP_H

#include "common.h"

enum npcx_ps2_channel {
	NPCX_PS2_CH0,
	NPCX_PS2_CH1,
	NPCX_PS2_CH2,
	NPCX_PS2_CH3,
	NPCX_PS2_CH_COUNT
};

void ps2_enable_channel(int channel, int enable,
			void (*callback)(uint8_t data));
int ps2_transmit_byte(int channel, uint8_t data);

#endif  /* __CROS_EC_PS2_CHIP_H */

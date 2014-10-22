/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IT8380 development board configuration */

#ifndef __BOARD_H
#define __BOARD_H

#ifndef __ASSEMBLER__

/* stubbed features */
#undef CONFIG_LID_SWITCH

#include "gpio_signal.h"

enum pwm_channel {
	PWM_CH_0,
	PWM_CH_1,
	PWM_CH_2,
	PWM_CH_3,
	PWM_CH_4,
	PWM_CH_5,
	PWM_CH_6,
	PWM_CH_7,

	/* Number of PWM channels */
	PWM_CH_COUNT
};

#endif /* !__ASSEMBLER__ */
#endif /* __BOARD_H */

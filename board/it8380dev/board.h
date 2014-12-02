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

enum adc_channel {
	ADC_CH_0,
	ADC_CH_1,
	ADC_CH_2,
	ADC_CH_3,
	ADC_CH_4,
	ADC_CH_5,
	ADC_CH_6,
	ADC_CH_7,

	/* Number of ADC channels */
	ADC_CH_COUNT
};

enum ec2i_setting {
	EC2I_SET_KB_LDN,
	EC2I_SET_KB_IRQ,
	EC2I_SET_KB_ENABLE,
	EC2I_SET_MOUSE_LDN,
	EC2I_SET_MOUSE_IRQ,
	EC2I_SET_MOUSE_ENABLE,
	EC2I_SET_PMC1_LDN,
	EC2I_SET_PMC1_IRQ,
	EC2I_SET_PMC1_ENABLE,

	/* Number of EC2I settings */
	EC2I_SETTING_COUNT
};

#endif /* !__ASSEMBLER__ */
#endif /* __BOARD_H */

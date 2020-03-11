/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IT8xxx2 development board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#include "baseboard.h"

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum pwm_channel {
	PWM_CH_FAN,
	PWM_CH_WITH_DSLEEP_FLAG,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum adc_channel {
	ADC_VBUSSA,
	ADC_VBUSSB,
	ADC_EVB_CH_13,
	ADC_EVB_CH_14,
	ADC_EVB_CH_15,
	ADC_EVB_CH_16,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */

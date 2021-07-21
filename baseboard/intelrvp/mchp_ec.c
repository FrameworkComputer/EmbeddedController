/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel BASEBOARD-RVP MCHP EC specific configuration */

#include "adc_chip.h"
#include "common.h"
#include "keyboard_scan.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "timer.h"
#include "usb_pd_tcpm.h"

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/*
	 * F3 key scan cycle completed but scan input is not
	 * charging to logic high when EC start scan next
	 * column for "T" key, so we set .output_settle_us
	 * to 80 us from 50 us.
	 */
	.output_settle_us = 80,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SNS_AMBIENT] = {
		.name = "ADC_TEMP_SNS_AMBIENT",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = ADC_TEMP_SNS_AMBIENT_CHANNEL,
	},
	[ADC_TEMP_SNS_DDR] = {
		.name = "ADC_TEMP_SNS_DDR",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = ADC_TEMP_SNS_DDR_CHANNEL,
	},
	[ADC_TEMP_SNS_SKIN] = {
		.name = "ADC_TEMP_SNS_SKIN",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = ADC_TEMP_SNS_SKIN_CHANNEL,
	},
	[ADC_TEMP_SNS_VR] = {
		.name = "ADC_TEMP_SNS_VR",
		.factor_mul = ADC_MAX_MVOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
		.channel = ADC_TEMP_SNS_VR_CHANNEL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/*
 * TODO - Fan and tach table.
 * MCHP MEC1322 and MEC170x have fan speed controller(s)
 * whereas MEC152x only has multiple TACH and PWM modules.
 * MEC152x fan control will require a firmware layer that uses
 * specified TACH and PWM modules.
 */

/* PWM channels */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = {
		.channel = 4,
		.flags = PWM_CONFIG_HAS_RPM_MODE,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

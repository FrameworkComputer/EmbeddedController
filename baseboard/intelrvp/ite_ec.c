/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel BASEBOARD-RVP ITE EC specific configuration */

#include "adc_chip.h"
#include "common.h"
#include "it83xx_pd.h"
#include "keyboard_scan.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "timer.h"
#include "usb_pd_tcpm.h"

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 35,
	.debounce_down_us = 5 * MSEC,
	.debounce_up_us = 40 * MSEC,
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
 * PWM HW channelx binding tachometer channelx for fan control.
 * Four tachometer input pins but two tachometer modules only,
 * so always binding [TACH_CH_TACH0A | TACH_CH_TACH0B] and/or
 * [TACH_CH_TACH1A | TACH_CH_TACH1B]
 */
const struct fan_tach_t fan_tach[] = {
	[PWM_HW_CH_DCR0] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR1] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR2] = {
		.ch_tach = TACH_CH_TACH1A,
		.fan_p = 2,
		.rpm_re = 1,
		.s_duty = 1,
	},
	[PWM_HW_CH_DCR3] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR4] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR5] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR6] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
	[PWM_HW_CH_DCR7] = {
		.ch_tach = TACH_CH_NULL,
		.fan_p = -1,
		.rpm_re = -1,
		.s_duty = -1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fan_tach) == PWM_HW_CH_TOTAL);

/* PWM channels */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = {
		.channel = PWM_HW_CH_DCR2,
		.flags = PWM_CONFIG_HAS_RPM_MODE,
		.freq_hz = 30000,
		.pcfsr_sel = PWM_PRESCALER_C7,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

#if defined(CONFIG_USBC_VCONN) && defined(CONFIG_USB_PD_TCPM_ITE_ON_CHIP)
void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
#ifndef CONFIG_USBC_PPC_VCONN
	/*
	 * Setting VCONN low by disabling the power switch before
	 * enabling the VCONN on respective CC line
	 */
	gpio_set_level(tcpc_gpios[port].vconn.cc1_pin,
			!tcpc_gpios[port].vconn.pin_pol);
	gpio_set_level(tcpc_gpios[port].vconn.cc2_pin,
			!tcpc_gpios[port].vconn.pin_pol);

	if (enabled)
		gpio_set_level((cc_pin != USBPD_CC_PIN_1) ?
				tcpc_gpios[port].vconn.cc2_pin :
				tcpc_gpios[port].vconn.cc1_pin,
				tcpc_gpios[port].vconn.pin_pol);
#endif
}
#endif

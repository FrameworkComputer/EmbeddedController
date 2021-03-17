/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel BASEBOARD-RVP NPCX EC specific configuration */

#ifndef __CROS_EC_NPCX_EC_H
#define __CROS_EC_NPCX_EC_H

#if !defined(__ASSEMBLER__)

enum mft_channel {
	MFT_CH_0 = 0,
	/* Number of MFT channels */
	MFT_CH_COUNT,
};

#endif /* __ASSEMBLER__ */

/* ADC channels */
#define ADC_MAX_MVOLT			ADC_MAX_VOLT
#define ADC_TEMP_SNS_AMBIENT_CHANNEL	NPCX_ADC_CH3
#define ADC_TEMP_SNS_DDR_CHANNEL	NPCX_ADC_CH4
#define ADC_TEMP_SNS_SKIN_CHANNEL	NPCX_ADC_CH2
#define ADC_TEMP_SNS_VR_CHANNEL		NPCX_ADC_CH1

/* KSO2 is inverted */
#define GPIO_KBD_KSO2			GPIO_EC_KSO_02_INV

/* Fan */
#define CONFIG_PWM
#define PWN_FAN_CHANNEL		3

/* GPIO64/65 are used as UART pins. */
#define NPCX_UART_MODULE2	1

#endif /* __CROS_EC_NPCX_EC_H */

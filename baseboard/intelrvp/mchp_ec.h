/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel BASEBOARD-RVP MCHP EC specific configuration */

#ifndef __CROS_EC_MCHP_EC_H
#define __CROS_EC_MCHP_EC_H

/* ADC channels */
#define ADC_TEMP_SNS_AMBIENT_CHANNEL	CHIP_ADC_CH7
#define ADC_TEMP_SNS_DDR_CHANNEL	CHIP_ADC_CH4
#define ADC_TEMP_SNS_SKIN_CHANNEL	CHIP_ADC_CH3
#define ADC_TEMP_SNS_VR_CHANNEL		CHIP_ADC_CH1

/*
 * ADC maximum voltage is a board level configuration.
 * MEC152x ADC can use an external 3.0 or 3.3V reference with
 * maximum values up to the reference voltage.
 * The ADC maximum voltage depends upon the external reference
 * voltage connected to MEC152x.
 */
#define ADC_MAX_MVOLT 3000

#endif /* __CROS_EC_MCHP_EC_H */

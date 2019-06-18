/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Stellaris EKB-LM4F-EAC board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "gpio.h"
#include "i2c.h"
#include "registers.h"
#include "util.h"

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	/* EC internal temperature is calculated by
	 * 273 + (295 - 450 * ADC_VALUE / ADC_READ_MAX) / 2
	 * = -225 * ADC_VALUE / ADC_READ_MAX + 420.5
	 */
	{"ECTemp", LM4_ADC_SEQ0, -225, ADC_READ_MAX, 420,
	 LM4_AIN_NONE, 0x0e /* TS0 | IE0 | END0 */, 0, 0},

	/* Charger current is mapped from 0~4000mA to 0~1.6V.
	 * And ADC maps 0~3.3V to ADC_READ_MAX.
	 *
	 * Note that on BDS, this is really just the turn pot on the Badger
	 * board, but that's good enough for debugging the ADC.
	 */
	{"BDSPot", LM4_ADC_SEQ1, 33 * 4000, ADC_READ_MAX * 16, 0,
	 LM4_AIN(0), 0x06 /* IE0 | END0 */, LM4_GPIO_E, (1<<3)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);


/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"lightbar", 5, 400},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#include "gpio_list.h"

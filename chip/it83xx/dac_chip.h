/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IT83xx DAC module for Chrome EC */

#ifndef __CROS_EC_DAC_CHIP_H
#define __CROS_EC_DAC_CHIP_H

#define DAC_AVCC 3300
/*
 * Each channel generates an output ranging
 * from 0V to AVCC with 8-bit resolution.
 */
#define DAC_RAW_DATA (BIT(8) - 1)

/* List of DAC channels. */
enum chip_dac_channel {
	CHIP_DAC_CH2 = 2,
	CHIP_DAC_CH3,
	CHIP_DAC_CH4,
	CHIP_DAC_CH5,
};

/**
 * DAC module enable.
 *
 * @param ch		Channel to enable.
 */
void dac_enable_channel(enum chip_dac_channel ch);

/**
 * DAC module disable.
 *
 * @param ch		Channel to disable.
 */
void dac_disable_channel(enum chip_dac_channel ch);

/**
 * Set DAC output voltage.
 *
 * @param ch		Channel to set.
 * @param mv		Setting ch output voltage.
 */
void dac_set_output_voltage(enum chip_dac_channel ch, int mv);

/**
 * Get DAC output voltage.
 *
 * @param ch		Channel to get.
 *
 * @return		Getting ch output voltage.
 */
int dac_get_output_voltage(enum chip_dac_channel ch);

#endif /* __CROS_EC_DAC_CHIP_H */


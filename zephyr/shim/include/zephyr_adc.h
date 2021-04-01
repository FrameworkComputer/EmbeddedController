/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_ADC_H
#define __CROS_EC_ZEPHYR_ADC_H

#ifdef CONFIG_PLATFORM_EC_ADC

#define NODE_ID_AND_COMMA(node_id) node_id,
enum adc_channel {
#if DT_NODE_EXISTS(DT_INST(0, named_adc_channels))
	DT_FOREACH_CHILD(DT_INST(0, named_adc_channels), NODE_ID_AND_COMMA)
#endif /* named_adc_channels */
	ADC_CH_COUNT
};

struct adc_t {
	const char *name;
	uint8_t input_ch;
	int factor_mul;
	int factor_div;
};

extern const struct adc_t adc_channels[];
#endif /* CONFIG_PLATFORM_EC_ADC */

#endif /* __CROS_EC_ZEPHYR_ADC_H */

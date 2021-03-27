/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_ADC_H
#define __CROS_EC_ZEPHYR_ADC_H

#ifdef CONFIG_PLATFORM_EC_ADC

#define ZSHIM_ADC_ID(node_id)         DT_ENUM_UPPER_TOKEN(node_id, enum_name)
#define ADC_ID_WITH_COMMA(node_id)    ZSHIM_ADC_ID(node_id),

enum adc_channel {
#if DT_NODE_EXISTS(DT_INST(0, named_adc_channels))
	DT_FOREACH_CHILD(DT_INST(0, named_adc_channels), ADC_ID_WITH_COMMA)
#endif /* named_adc_channels */
	ADC_CH_COUNT
};

#undef ADC_ID_WITH_COMMA

struct adc_t {
	const char *name;
	uint8_t input_ch;
	int factor_mul;
	int factor_div;
};

extern const struct adc_t adc_channels[];
#endif /* CONFIG_PLATFORM_EC_ADC */

#endif /* __CROS_EC_ZEPHYR_ADC_H */

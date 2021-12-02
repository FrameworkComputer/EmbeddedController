/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_ADC_H
#define __CROS_EC_ZEPHYR_ADC_H

#include <drivers/adc.h>

#ifdef CONFIG_PLATFORM_EC_ADC

#define ZSHIM_ADC_ID(node_id) DT_STRING_UPPER_TOKEN(node_id, enum_name)
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
	struct adc_channel_cfg channel_cfg;
};

#ifndef CONFIG_ADC_CHANNELS_RUNTIME_CONFIG
extern const struct adc_t adc_channels[];
#else
extern struct adc_t adc_channels[];
#endif /* CONFIG_ADC_CHANNELS_RUNTIME_CONFIG */
#else
/* Empty declaration to avoid warnings if adc.h is included */
enum adc_channel {
	ADC_CH_COUNT
};
#endif /* CONFIG_PLATFORM_EC_ADC */

#endif /* __CROS_EC_ZEPHYR_ADC_H */

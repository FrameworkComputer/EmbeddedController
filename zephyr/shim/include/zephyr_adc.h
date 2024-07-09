/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_ADC_H
#define __CROS_EC_ZEPHYR_ADC_H

#include <zephyr/drivers/adc.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_ADC

#define ZSHIM_ADC_ID(node_id) DT_STRING_UPPER_TOKEN(node_id, enum_name)

enum adc_channel {
#if DT_NODE_EXISTS(DT_INST(0, named_adc_channels))
	DT_FOREACH_CHILD_SEP(DT_INST(0, named_adc_channels), ZSHIM_ADC_ID,
			     (, )),
#endif /* named_adc_channels */
	ADC_CH_COUNT
};

struct adc_t {
	const char *name;
	const struct device *dev;
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
enum adc_channel { ADC_CH_COUNT };
#endif /* CONFIG_ADC */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ZEPHYR_ADC_H */

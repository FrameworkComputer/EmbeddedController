/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/adc.h>
#include <logging/log.h>
#include "adc.h"
#include "zephyr_adc.h"

LOG_MODULE_REGISTER(shim_adc, LOG_LEVEL_ERR);

#define ADC_NODE DT_NODELABEL(adc0)
const struct device *adc_dev;

#define HAS_NAMED_ADC_CHANNELS DT_NODE_EXISTS(DT_INST(0, named_adc_channels))

#if HAS_NAMED_ADC_CHANNELS
#define ADC_CHANNEL_COMMA(node_id)                                      \
	[ZSHIM_ADC_ID(node_id)] = {                                     \
		.name = DT_LABEL(node_id),                              \
		.input_ch = DT_PROP(node_id, channel),                  \
		.factor_mul = DT_PROP(node_id, mul),                    \
		.factor_div = DT_PROP(node_id, div),                    \
		.channel_cfg = {                                        \
			.channel_id = DT_PROP(node_id, channel),        \
			.gain = DT_ENUM_TOKEN(node_id, gain),           \
			.reference = DT_ENUM_TOKEN(node_id, reference), \
			.acquisition_time =                             \
				DT_PROP(node_id, acquisition_time),     \
			.differential = DT_PROP(node_id, differential), \
		},                                                      \
	},
#ifdef CONFIG_ADC_CHANNELS_RUNTIME_CONFIG
struct adc_t adc_channels[] = { DT_FOREACH_CHILD(
	DT_INST(0, named_adc_channels), ADC_CHANNEL_COMMA) };
#else
const struct adc_t adc_channels[] = { DT_FOREACH_CHILD(
	DT_INST(0, named_adc_channels), ADC_CHANNEL_COMMA) };
#endif
#endif /* named_adc_channels */

static int init_device_bindings(const struct device *device)
{
	ARG_UNUSED(device);
	adc_dev = DEVICE_DT_GET(ADC_NODE);

	if (!device_is_ready(adc_dev)) {
		LOG_ERR("Error: device %s is not ready", adc_dev->name);
		return -1;
	}

#if HAS_NAMED_ADC_CHANNELS
	for (int i = 0; i < ARRAY_SIZE(adc_channels); i++)
		adc_channel_setup(adc_dev, &adc_channels[i].channel_cfg);
#endif

	return 0;
}
SYS_INIT(init_device_bindings, POST_KERNEL, 51);

int adc_read_channel(enum adc_channel ch)
{
	int ret = 0, rv;
	struct adc_sequence seq = {
		.options = NULL,
		.channels = BIT(adc_channels[ch].input_ch),
		.buffer = &ret,
		.buffer_size = sizeof(ret),
		.resolution = CONFIG_PLATFORM_EC_ADC_RESOLUTION,
		.oversampling = CONFIG_PLATFORM_EC_ADC_OVERSAMPLING,
		.calibrate = false,
	};

	rv = adc_read(adc_dev, &seq);
	if (rv)
		return rv;

	adc_raw_to_millivolts(adc_ref_internal(adc_dev), ADC_GAIN_1,
			      CONFIG_PLATFORM_EC_ADC_RESOLUTION, &ret);
	ret = (ret * adc_channels[ch].factor_mul) / adc_channels[ch].factor_div;
	return ret;
}

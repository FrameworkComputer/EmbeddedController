/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "zephyr_adc.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(shim_adc, LOG_LEVEL_ERR);

#if defined(CONFIG_PLATFORM_EC_ADC_CMD) && defined(CONFIG_ADC_SHELL)
#error "Define only one 'adc' console command."
#endif

#define ADC_CHANNEL_INIT(node_id) \
	[ZSHIM_ADC_ID(node_id)] = {                                       \
		.name = DT_NODE_FULL_NAME(node_id),                       \
		.dev = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(node_id)),       \
		.input_ch = DT_IO_CHANNELS_INPUT(node_id),                \
		.factor_mul = DT_PROP(node_id, mul),                      \
		.factor_div = DT_PROP(node_id, div),                      \
		.channel_cfg = {                                          \
			.channel_id = DT_IO_CHANNELS_INPUT(node_id),      \
			.gain = DT_STRING_TOKEN(node_id, gain),           \
			.reference = DT_STRING_TOKEN(node_id, reference), \
			.acquisition_time =                               \
				DT_PROP(node_id, acquisition_time),       \
			.differential = DT_PROP(node_id, differential),   \
		},                                                        \
	},
#ifdef CONFIG_ADC_CHANNELS_RUNTIME_CONFIG
struct adc_t adc_channels[] = { DT_FOREACH_CHILD(DT_INST(0, named_adc_channels),
						 ADC_CHANNEL_INIT) };
#else
const struct adc_t adc_channels[] = { DT_FOREACH_CHILD(
	DT_INST(0, named_adc_channels), ADC_CHANNEL_INIT) };
#endif

static int init_device_bindings(void)
{
	for (int i = 0; i < ARRAY_SIZE(adc_channels); i++) {
		if (!device_is_ready(adc_channels[i].dev))
			k_oops();

		adc_channel_setup(adc_channels[i].dev,
				  &adc_channels[i].channel_cfg);
	}

	return 0;
}
SYS_INIT(init_device_bindings, POST_KERNEL, 51);

test_mockable int adc_read_channel(enum adc_channel ch)
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

	rv = adc_read(adc_channels[ch].dev, &seq);
	if (rv)
		return rv;

	adc_raw_to_millivolts(adc_ref_internal(adc_channels[ch].dev),
			      ADC_GAIN_1, CONFIG_PLATFORM_EC_ADC_RESOLUTION,
			      &ret);
	ret = (ret * adc_channels[ch].factor_mul) / adc_channels[ch].factor_div;
	return ret;
}

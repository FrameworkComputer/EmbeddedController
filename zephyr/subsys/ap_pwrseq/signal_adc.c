/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "signal_adc.h"

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <power_signals.h>

#define DT_DRV_COMPAT intel_ap_pwrseq_adc

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

struct adc_config {
	const struct device *dev_trig_high;
	const struct device *dev_trig_low;
	const struct device *adc_dev;
	uint8_t adc_ch;
	uint16_t threshold;
	enum power_signal signal;
};

#define ADC_HIGH_DEV(node_id) DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(node_id))

#define ADC_HIGH_CHAN(node_id) DT_IO_CHANNELS_INPUT(node_id)

#define ADC_THRESH(node_id) DT_PROP(node_id, threshold_mv)

#define INIT_ADC_CONFIG(inst)                                                 \
	{                                                                     \
		.dev_trig_high =                                              \
			DEVICE_DT_GET(DT_INST_PHANDLE(inst, trigger_high)),   \
		.dev_trig_low =                                               \
			DEVICE_DT_GET(DT_INST_PHANDLE(inst, trigger_low)),    \
		.adc_dev = ADC_HIGH_DEV(DT_INST_PHANDLE(inst, trigger_high)), \
		.adc_ch = ADC_HIGH_CHAN(DT_INST_PHANDLE(inst, trigger_high)), \
		.threshold = ADC_THRESH(DT_INST_PHANDLE(inst, trigger_high)), \
		.signal = PWR_DT_INST_SIGNAL_ENUM(inst),                      \
	},

static const struct adc_config config[] = { DT_INST_FOREACH_STATUS_OKAY(
	INIT_ADC_CONFIG) };

/*
 * Bit allocations for atomic state
 */
enum { ADC_BIT_VALUE = 0, ADC_BIT_LOW_ENABLED = 1, ADC_BIT_HIGH_ENABLED = 2 };

atomic_t adc_state[ARRAY_SIZE(config)];

static void set_trigger(const struct device *dev, atomic_t *state, int bit,
			bool enable)
{
	/*
	 * Only enable or disable if the trigger is not
	 * already enabled or disabled.
	 */
	if (enable ? !atomic_test_and_set_bit(state, bit) :
		     atomic_test_and_clear_bit(state, bit)) {
		struct sensor_value val;

		val.val1 = enable;
		sensor_attr_set(dev, SENSOR_CHAN_VOLTAGE, SENSOR_ATTR_ALERT,
				&val);
	}
}

static void set_low_trigger(enum pwr_sig_adc adc, bool enable)
{
	set_trigger(config[adc].dev_trig_low, &adc_state[adc],
		    ADC_BIT_LOW_ENABLED, enable);
}

static void set_high_trigger(enum pwr_sig_adc adc, bool enable)
{
	set_trigger(config[adc].dev_trig_high, &adc_state[adc],
		    ADC_BIT_HIGH_ENABLED, enable);
}

static void trigger_high(enum pwr_sig_adc adc)
{
	set_high_trigger(adc, false);
	atomic_set_bit(&adc_state[adc], ADC_BIT_VALUE);
	set_low_trigger(adc, true);
	LOG_DBG("power signal adc%d is HIGH", adc);
	power_signal_interrupt(config[adc].signal, 1);
}

static void trigger_low(enum pwr_sig_adc adc)
{
	set_low_trigger(adc, false);
	atomic_clear_bit(&adc_state[adc], ADC_BIT_VALUE);
	set_high_trigger(adc, true);
	LOG_DBG("power signal adc%d is LOW", adc);
	power_signal_interrupt(config[adc].signal, 0);
}

int power_signal_adc_get(enum pwr_sig_adc adc)
{
	if (adc < 0 || adc >= ARRAY_SIZE(config)) {
		return -EINVAL;
	}
	return atomic_test_bit(&adc_state[adc], ADC_BIT_VALUE);
}

int power_signal_adc_enable(enum pwr_sig_adc adc)
{
	if (adc < 0 || adc >= ARRAY_SIZE(config)) {
		return -EINVAL;
	}

	/* Only need to enable relevant trigger depending on current state */
	if (atomic_test_bit(&adc_state[adc], ADC_BIT_VALUE)) {
		set_low_trigger(adc, true);
	} else {
		set_high_trigger(adc, true);
	}
	return 0;
}

int power_signal_adc_disable(enum pwr_sig_adc adc)
{
	if (adc < 0 || adc >= ARRAY_SIZE(config)) {
		return -EINVAL;
	}
	set_low_trigger(adc, false);
	set_high_trigger(adc, false);
	return 0;
}

/*
 * Macros to create individual callbacks for
 * high and low triggers for each ADC.
 */

#define TAG_ADC(tag, name) DT_CAT(tag, name)

#define PWR_ADC_ENUM(inst) \
	TAG_ADC(PWR_SIG_TAG_ADC, PWR_DT_INST_SIGNAL_ENUM(inst))

#define ADC_CB(inst, lev) cb_##lev##_##inst

#define ADC_CB_DEFINE(inst, lev)                                            \
	static void ADC_CB(inst, lev)(const struct device *dev,             \
				      const struct sensor_trigger *trigger) \
	{                                                                   \
		trigger_##lev(PWR_ADC_ENUM(inst));                          \
	}

DT_INST_FOREACH_STATUS_OKAY_VARGS(ADC_CB_DEFINE, high)
DT_INST_FOREACH_STATUS_OKAY_VARGS(ADC_CB_DEFINE, low)

#define ADC_CB_COMMA(inst, lev) ADC_CB(inst, lev),

static const struct sensor_trigger power_signal_trig = {
	.type = SENSOR_TRIG_THRESHOLD,
	.chan = SENSOR_CHAN_VOLTAGE,
};

void power_signal_adc_init(void)
{
	sensor_trigger_handler_t low_cb[] = { DT_INST_FOREACH_STATUS_OKAY_VARGS(
		ADC_CB_COMMA, low) };
	sensor_trigger_handler_t high_cb[] = {
		DT_INST_FOREACH_STATUS_OKAY_VARGS(ADC_CB_COMMA, high)
	};
	int i, rv;
	int32_t val = 0;

	for (i = 0; i < ARRAY_SIZE(low_cb); i++) {
		/*
		 * Read initial value.
		 */
		const struct device *dev = config[i].adc_dev;
		struct adc_sequence seq = {
			.options = NULL,
			.channels = BIT(config[i].adc_ch),
			.buffer = &val,
			.buffer_size = sizeof(val),
			.resolution = CONFIG_PLATFORM_EC_ADC_RESOLUTION,
			.oversampling = CONFIG_PLATFORM_EC_ADC_OVERSAMPLING,
			.calibrate = false,
		};

		rv = adc_read(dev, &seq);
		if (rv) {
			LOG_ERR("ADC %s:%d initial read failed", dev->name,
				config[i].adc_ch);
		} else {
			adc_raw_to_millivolts(adc_ref_internal(dev), ADC_GAIN_1,
					      CONFIG_PLATFORM_EC_ADC_RESOLUTION,
					      &val);
			if (val >= config[i].threshold) {
				atomic_set_bit(&adc_state[i], ADC_BIT_VALUE);
			}
		}
		/* Set high and low trigger callbacks */
		sensor_trigger_set(config[i].dev_trig_high, &power_signal_trig,
				   high_cb[i]);
		sensor_trigger_set(config[i].dev_trig_low, &power_signal_trig,
				   low_cb[i]);
		power_signal_adc_enable(i);
	}
}

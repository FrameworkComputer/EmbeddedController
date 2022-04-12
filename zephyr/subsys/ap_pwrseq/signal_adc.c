/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/atomic.h>
#include <logging/log.h>

#include <power_signals.h>
#include <signal_adc.h>
#include "drivers/sensor.h"

#define MY_COMPAT	intel_ap_pwrseq_adc

#if HAS_ADC_SIGNALS

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

struct adc_config {
	const struct device *dev_trig_high;
	const struct device *dev_trig_low;
	enum power_signal signal;
};

#define INIT_ADC_CONFIG(id)	\
{									\
	.dev_trig_high = DEVICE_DT_GET(DT_PHANDLE(id, trigger_high)),	\
	.dev_trig_low = DEVICE_DT_GET(DT_PHANDLE(id, trigger_low)),	\
	.signal = PWR_SIGNAL_ENUM(id),					\
},

static const struct adc_config config[] = {
DT_FOREACH_STATUS_OKAY(MY_COMPAT, INIT_ADC_CONFIG)
};

/*
 * Bit allocations for atomic state
 */
enum {
	ADC_BIT_VALUE = 0,
	ADC_BIT_LOW_ENABLED = 1,
	ADC_BIT_HIGH_ENABLED = 2
};

atomic_t adc_state[ARRAY_SIZE(config)];

static void set_trigger(const struct device *dev,
			atomic_t *state,
			int bit,
			bool enable)
{
	/*
	 * Only enable or disable if the trigger is not
	 * already enabled or disabled.
	 */
	if (enable
		? !atomic_test_and_set_bit(state, bit)
		: atomic_test_and_clear_bit(state, bit)) {
		struct sensor_value val;

		val.val1 = enable;
		sensor_attr_set(dev,
				SENSOR_CHAN_VOLTAGE,
				SENSOR_ATTR_ALERT,
				&val);
	}
}

static void set_low_trigger(enum pwr_sig_adc adc, bool enable)
{
	set_trigger(config[adc].dev_trig_low,
		    &adc_state[adc],
		    ADC_BIT_LOW_ENABLED,
		    enable);

}

static void set_high_trigger(enum pwr_sig_adc adc, bool enable)
{
	set_trigger(config[adc].dev_trig_high,
		    &adc_state[adc],
		    ADC_BIT_HIGH_ENABLED,
		    enable);
}

static void trigger_high(enum pwr_sig_adc adc)
{
	atomic_set_bit(&adc_state[adc], ADC_BIT_VALUE);
	set_low_trigger(adc, true);
	set_high_trigger(adc, false);
	LOG_DBG("power signal adc%d is HIGH", adc);
	power_signal_interrupt(config[adc].signal, 1);
}

static void trigger_low(enum pwr_sig_adc adc)
{
	atomic_clear_bit(&adc_state[adc], ADC_BIT_VALUE);
	set_low_trigger(adc, false);
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

#define PWR_ADC_ENUM(id) TAG_ADC(PWR_SIG_TAG_ADC, PWR_SIGNAL_ENUM(id))

#define ADC_CB(id, lev)	cb_##lev##_##id

#define ADC_CB_DEFINE(id, lev)					\
static void ADC_CB(id, lev)(const struct device *dev,		\
		       const struct sensor_trigger *trigger)	\
{								\
	trigger_##lev(PWR_ADC_ENUM(id));			\
}

DT_FOREACH_STATUS_OKAY_VARGS(MY_COMPAT, ADC_CB_DEFINE, high)
DT_FOREACH_STATUS_OKAY_VARGS(MY_COMPAT, ADC_CB_DEFINE, low)

#define ADC_CB_COMMA(id, lev)	ADC_CB(id, lev),

void power_signal_adc_init(void)
{
	struct sensor_trigger trig = {
		.type = SENSOR_TRIG_THRESHOLD,
		.chan = SENSOR_CHAN_VOLTAGE
	};
	sensor_trigger_handler_t low_cb[] = {
		DT_FOREACH_STATUS_OKAY_VARGS(MY_COMPAT, ADC_CB_COMMA, low)
	};
	sensor_trigger_handler_t high_cb[] = {
		DT_FOREACH_STATUS_OKAY_VARGS(MY_COMPAT, ADC_CB_COMMA, high)
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(low_cb); i++) {
		/* Set high and low trigger callbacks */
		sensor_trigger_set(config[i].dev_trig_high, &trig, high_cb[i]);
		sensor_trigger_set(config[i].dev_trig_low, &trig, low_cb[i]);
		power_signal_adc_enable(i);
	}
}

#endif /*  HAS_ADC_SIGNALS */

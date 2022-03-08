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
};

#define INIT_ADC_CONFIG(id)	\
{									\
	.dev_trig_high = DEVICE_DT_GET(DT_PHANDLE(id, trigger_high)),	\
	.dev_trig_low = DEVICE_DT_GET(DT_PHANDLE(id, trigger_low)),	\
},

static const struct adc_config config[] = {
DT_FOREACH_STATUS_OKAY(MY_COMPAT, INIT_ADC_CONFIG)
};

static atomic_t value[ARRAY_SIZE(config)];

static void trigger_high(enum pwr_sig_adc adc)
{
	struct sensor_value val;

	atomic_set_bit(&value[adc], 0);
	val.val1 = false;
	sensor_attr_set(config[adc].dev_trig_high,
			SENSOR_CHAN_VOLTAGE,
			SENSOR_ATTR_ALERT,
			&val);
	val.val1 = true;
	sensor_attr_set(config[adc].dev_trig_low,
			SENSOR_CHAN_VOLTAGE,
			SENSOR_ATTR_ALERT,
			&val);
	LOG_DBG("power signal adc%d is HIGH", adc);
	power_update_signals();
}

static void trigger_low(enum pwr_sig_adc adc)
{
	struct sensor_value val;

	atomic_clear_bit(&value[adc], 0);
	val.val1 = false;
	sensor_attr_set(config[adc].dev_trig_low,
			SENSOR_CHAN_VOLTAGE,
			SENSOR_ATTR_ALERT,
			&val);
	val.val1 = true;
	sensor_attr_set(config[adc].dev_trig_high,
			SENSOR_CHAN_VOLTAGE,
			SENSOR_ATTR_ALERT,
			&val);
	LOG_DBG("power signal adc%d is LOW", adc);
	power_update_signals();
}

int power_signal_adc_get(enum pwr_sig_adc adc)
{
	if (adc < 0 || adc >= ARRAY_SIZE(config)) {
		return -EINVAL;
	}
	return !!value[adc];
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
	struct sensor_value val;
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

		/* Enable high trigger callback only */
		val.val1 = true;
		sensor_attr_set(config[i].dev_trig_high,
				SENSOR_CHAN_VOLTAGE,
				SENSOR_ATTR_ALERT,
				&val);
	}
}

#endif /*  HAS_ADC_SIGNALS */

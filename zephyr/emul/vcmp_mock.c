/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_vcmp_mock

#include "vcmp_mock.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(SENSOR_GENERIC_EMUL, CONFIG_SENSOR_LOG_LEVEL);

struct vcmp_mock_data {
	sensor_trigger_handler_t handler;
	const struct sensor_trigger *trigger;
	bool alert_enabled;
};

void vcmp_mock_trigger(const struct device *dev)
{
	struct vcmp_mock_data *data = dev->data;

	if (data->handler) {
		data->handler(dev, data->trigger);
	}
}

static int vcmp_mock_attr_set(const struct device *dev,
			      enum sensor_channel chan,
			      enum sensor_attribute attr,
			      const struct sensor_value *val)
{
	struct vcmp_mock_data *data = dev->data;

	if (chan != SENSOR_CHAN_VOLTAGE) {
		return -ENOTSUP;
	}

	switch (attr) {
	case SENSOR_ATTR_ALERT:
		data->alert_enabled = val->val1;
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int vcmp_mock_attr_get(const struct device *dev,
			      enum sensor_channel chan,
			      enum sensor_attribute attr,
			      struct sensor_value *val)
{
	struct vcmp_mock_data *data = dev->data;

	if (chan != SENSOR_CHAN_VOLTAGE) {
		return -ENOTSUP;
	}

	switch (attr) {
	case SENSOR_ATTR_ALERT:
		val->val1 = data->alert_enabled;
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int vcmp_mock_trigger_set(const struct device *dev,
				 const struct sensor_trigger *trig,
				 sensor_trigger_handler_t handler)
{
	struct vcmp_mock_data *data = dev->data;

	data->handler = handler;
	data->trigger = trig;

	return -ENOTSUP;
}

static int vcmp_mock_sample_fetch(const struct device *dev,
				  enum sensor_channel chan)
{
	return -ENOTSUP;
}

static int vcmp_mock_channel_get(const struct device *dev,
				 enum sensor_channel chan,
				 struct sensor_value *val)
{
	return -ENOTSUP;
}

static DEVICE_API(sensor, vcmp_mock_driver_api) = {
	.attr_set = vcmp_mock_attr_set,
	.channel_get = vcmp_mock_channel_get,
	.sample_fetch = vcmp_mock_sample_fetch,
	.attr_get = vcmp_mock_attr_get,
	.trigger_set = vcmp_mock_trigger_set,
};

int vcmp_mock_init(const struct device *dev)
{
	return 0;
}

#define VCMP_MOCK_INST(inst)                                                   \
	static struct vcmp_mock_data vcmp_mock_data_##inst;                    \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, vcmp_mock_init, NULL,               \
				     &vcmp_mock_data_##inst, NULL,             \
				     POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, \
				     &vcmp_mock_driver_api);

DT_INST_FOREACH_STATUS_OKAY(VCMP_MOCK_INST)

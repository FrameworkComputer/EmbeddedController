/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_prochot_vcmp

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

/* Include the right definition for the custom sensor threshold attributes, the
 * enum name happen to be the same for both it8xxx2 and npcx so we just have to
 * include the right file.
 */
#if defined(CONFIG_VCMP_IT8XXX2)
#include <zephyr/drivers/sensor/it8xxx2_vcmp.h>
#elif defined(CONFIG_ADC_CMP_NPCX)
#include <zephyr/drivers/sensor/adc_cmp_npcx.h>
#elif defined(CONFIG_TEST)
#include <test_vcmp_sensor.h>
#else
#error Unsupported platform
#endif

#include <chipset.h>

LOG_MODULE_REGISTER(prochot_vcmp, LOG_LEVEL_INF);

#define TH_HIGH_PERCENT 80
#define TH_LOW_PERCENT 50

struct prochot_vcmp_config {
	const struct device *vcmp_dev;
	uint16_t high_level_mv;
};

struct prochot_vcmp_data {
	bool last_state;
};

static void prochot_vcmp_configure(const struct device *dev, bool state)
{
	const struct prochot_vcmp_config *cfg = dev->config;
	struct sensor_value val;
	enum sensor_attribute attr;
	int ret;

	memset(&val, 0, sizeof(val));

	val.val1 = 0;
	ret = sensor_attr_set(cfg->vcmp_dev, SENSOR_CHAN_VOLTAGE,
			      SENSOR_ATTR_ALERT, &val);
	if (ret < 0) {
		LOG_ERR("vcmp attr set failed: %d", ret);
		return;
	}

	if (state) {
		val.val1 = cfg->high_level_mv * TH_HIGH_PERCENT / 100;
		attr = (enum sensor_attribute)SENSOR_ATTR_UPPER_VOLTAGE_THRESH;
	} else {
		val.val1 = cfg->high_level_mv * TH_LOW_PERCENT / 100;
		attr = (enum sensor_attribute)SENSOR_ATTR_LOWER_VOLTAGE_THRESH;
	}

	ret = sensor_attr_set(cfg->vcmp_dev, SENSOR_CHAN_VOLTAGE, attr, &val);
	if (ret < 0) {
		LOG_ERR("vcmp attr set failed: %d", ret);
		return;
	}

	val.val1 = 1;
	ret = sensor_attr_set(cfg->vcmp_dev, SENSOR_CHAN_VOLTAGE,
			      SENSOR_ATTR_ALERT, &val);
	if (ret < 0) {
		LOG_ERR("vcmp attr set failed: %d", ret);
		return;
	}
}

static void prochot_vcmp_handler(const struct device *sensor_dev,
				 const struct sensor_trigger *trigger)
{
	const struct device *dev = DEVICE_DT_GET(DT_INST(0, DT_DRV_COMPAT));
	struct prochot_vcmp_data *data = dev->data;

	data->last_state = !data->last_state;

	prochot_vcmp_configure(dev, data->last_state);

	if (!chipset_in_state(CHIPSET_STATE_ON)) {
		return;
	}

	if (data->last_state) {
		LOG_INF("PROCHOT state: asserted");
	} else {
		LOG_INF("PROCHOT state: deasserted");
	}
}

static const struct sensor_trigger prochot_trig = {
	.type = SENSOR_TRIG_THRESHOLD,
	.chan = SENSOR_CHAN_VOLTAGE,
};

static int prochot_vcmp_init(const struct device *dev)
{
	const struct prochot_vcmp_config *cfg = dev->config;
	struct prochot_vcmp_data *data = dev->data;
	int ret;

	ret = sensor_trigger_set(cfg->vcmp_dev, &prochot_trig,
				 prochot_vcmp_handler);
	if (ret < 0) {
		LOG_ERR("trigger set failed: %d", ret);
		return ret;
	}

	/* Initialize for detecting a high transition */
	data->last_state = true;
	prochot_vcmp_configure(dev, true);

	return 0;
}

static const struct prochot_vcmp_config prochot_vcmp_cfg = {
	.vcmp_dev = DEVICE_DT_GET(DT_INST_PHANDLE(0, vcmp)),
	.high_level_mv = DT_INST_PROP(0, high_level_mv),
};

static struct prochot_vcmp_data prochot_vcmp_data;

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1);
DEVICE_DT_INST_DEFINE(0, prochot_vcmp_init, NULL, &prochot_vcmp_data,
		      &prochot_vcmp_cfg, POST_KERNEL,
		      CONFIG_SENSOR_INIT_PRIORITY, NULL);

#if CONFIG_TEST
int test_reinit(void)
{
	return prochot_vcmp_init(DEVICE_DT_GET(DT_INST(0, DT_DRV_COMPAT)));
}
#endif

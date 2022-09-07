/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_rtc_emul

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emul_rtc);

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <ec_commands.h>
#include <drivers/cros_rtc.h>
#include <zephyr/sys/__assert.h>

#include "flash.h"

struct cros_rtc_emul_data {
	const struct device *rtc_dev;
	uint32_t alarm_time;
	cros_rtc_alarm_callback_t alarm_callback;
	uint32_t value;
};

struct rtc_emul_cfg {
	/** Pointer to run-time data */
	struct cros_rtc_emul_data *data;
};

#define RTC_DEV DT_CHOSEN(zephyr_rtc_controller)

#define DRV_DATA(dev) ((struct cros_rtc_emul_data *)(dev)->data)

static int cros_rtc_emul_configure(const struct device *dev,
				   cros_rtc_alarm_callback_t callback)
{
	struct cros_rtc_emul_data *data = DRV_DATA(dev);

	if (callback == NULL) {
		return -EINVAL;
	}

	data->alarm_callback = callback;

	return EC_SUCCESS;
}

static int cros_rtc_emul_get_value(const struct device *dev, uint32_t *value)
{
	struct cros_rtc_emul_data *data = DRV_DATA(dev);

	*value = data->value;

	return EC_SUCCESS;
}

static int cros_rtc_emul_set_value(const struct device *dev, uint32_t value)
{
	struct cros_rtc_emul_data *data = DRV_DATA(dev);

	data->value = value;

	return EC_SUCCESS;
}

static int cros_rtc_emul_get_alarm(const struct device *dev, uint32_t *seconds,
				   uint32_t *microseconds)
{
	struct cros_rtc_emul_data *data = DRV_DATA(dev);

	*seconds = data->alarm_time;
	*microseconds = 0;

	return EC_SUCCESS;
}

static int cros_rtc_emul_reset_alarm(const struct device *dev)
{
	struct cros_rtc_emul_data *data = DRV_DATA(dev);

	data->alarm_time = 0;

	return EC_SUCCESS;
}

static int cros_rtc_emul_set_alarm(const struct device *dev, uint32_t seconds,
				   uint32_t microseconds)
{
	struct cros_rtc_emul_data *data = DRV_DATA(dev);
	int ret;

	ARG_UNUSED(microseconds);

	ret = cros_rtc_emul_reset_alarm(dev);

	if (ret < 0) {
		return ret;
	}

	data->alarm_time = seconds;

	if (ret < 0) {
		return ret;
	}

	return EC_SUCCESS;
}

static const struct cros_rtc_driver_api emul_cros_rtc_driver_api = {
	.configure = cros_rtc_emul_configure,
	.get_value = cros_rtc_emul_get_value,
	.set_value = cros_rtc_emul_set_value,
	.get_alarm = cros_rtc_emul_get_alarm,
	.set_alarm = cros_rtc_emul_set_alarm,
	.reset_alarm = cros_rtc_emul_reset_alarm,
};

static int rtc_emul_init(const struct device *dev)
{
	struct cros_rtc_emul_data *data = DRV_DATA(dev);

	data->alarm_callback = NULL;
	data->alarm_time = 0;
	data->value = 0;

	return EC_SUCCESS;
}

#define RTC_EMUL(n)                                                            \
	static struct cros_rtc_emul_data cros_rtc_emul_data_##n = {};          \
	static const struct rtc_emul_cfg rtc_emul_cfg_##n = {                  \
		.data = &cros_rtc_emul_data_##n,                               \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(n, rtc_emul_init, NULL, &cros_rtc_emul_data_##n, \
			      &rtc_emul_cfg_##n, PRE_KERNEL_1,                 \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,             \
			      &emul_cros_rtc_driver_api)
DT_INST_FOREACH_STATUS_OKAY(RTC_EMUL);

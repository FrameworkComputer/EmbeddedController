/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT microchip_xec_cros_rtc

#include <assert.h>
#include <drivers/cros_rtc.h>
#include <drivers/gpio.h>
#include <kernel.h>
#include <soc.h>
#include <soc/microchip_xec/reg_def_cros.h>

#include "ec_tasks.h"
#include "task.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(cros_rtc, LOG_LEVEL_ERR);

/* Driver config */
struct cros_rtc_xec_config {
	uintptr_t base;
};

/* Driver data */
struct cros_rtc_xec_data {
	cros_rtc_alarm_callback_t alarm_callback;
};

/* Driver convenience defines */
#define DRV_CONFIG(dev) ((const struct cros_rtc_xec_config *)(dev)->config)

#define DRV_DATA(dev) ((struct cros_rtc_xec_data *)(dev)->data)

#define HAL_INSTANCE(dev) (struct rtc_hw *)(DRV_CONFIG(dev)->base)

/* cros ec RTC api functions */
static int cros_rtc_xec_configure(const struct device *dev,
				  cros_rtc_alarm_callback_t callback)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(callback);

	/* TODO(b/214988394): Chip has RTC hardware, need to implement code */

	return 0;
}

static int cros_rtc_xec_get_value(const struct device *dev, uint32_t *value)
{
	ARG_UNUSED(dev);

	/* TODO(b/214988394): Chip has RTC hardware, need to implement code */
	*value = 0;

	return 0;
}

static int cros_rtc_xec_set_value(const struct device *dev, uint32_t value)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(value);

	/* TODO(b/214988394): Chip has RTC hardware, need to implement code */

	return 0;
}

static int cros_rtc_xec_get_alarm(const struct device *dev, uint32_t *seconds,
				  uint32_t *microseconds)
{
	ARG_UNUSED(dev);

	/* TODO(b/214988394): Chip has RTC hardware, need to implement code */
	*seconds = 0;
	*microseconds = 0;

	return 0;
}

static int cros_rtc_xec_set_alarm(const struct device *dev, uint32_t seconds,
				  uint32_t microseconds)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(seconds);
	ARG_UNUSED(microseconds);

	/* TODO(b/214988394): Chip has RTC hardware, need to implement code */
	return -ENOTSUP;
}

static int cros_rtc_xec_reset_alarm(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* TODO(b/214988394): Chip has RTC hardware, need to implement code */
	return -ENOTSUP;
}

/* cros ec RTC driver registration */
static const struct cros_rtc_driver_api cros_rtc_xec_driver_api = {
	.configure = cros_rtc_xec_configure,
	.get_value = cros_rtc_xec_get_value,
	.set_value = cros_rtc_xec_set_value,
	.get_alarm = cros_rtc_xec_get_alarm,
	.set_alarm = cros_rtc_xec_set_alarm,
	.reset_alarm = cros_rtc_xec_reset_alarm,
};

static int cros_rtc_xec_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* TODO(b/214988394): Chip has RTC hardware, need to implement code */
	return 0;
}

static const struct cros_rtc_xec_config cros_rtc_xec_cfg_0 = {
	.base = DT_INST_REG_ADDR(0),
	/* TODO(b/214988394): Chip has RTC hardware, need to implement code */
};

static struct cros_rtc_xec_data cros_rtc_xec_data_0;

DEVICE_DT_INST_DEFINE(0, cros_rtc_xec_init, NULL,
		      &cros_rtc_xec_data_0, &cros_rtc_xec_cfg_0, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		      &cros_rtc_xec_driver_api);

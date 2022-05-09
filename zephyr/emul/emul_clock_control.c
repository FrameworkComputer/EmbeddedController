/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_clock_control_emul

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/kernel.h>

#include "common.h"
#include "emul/emul_clock_control.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(clock_control_emul, CONFIG_CLOCK_CONTROL_EMUL_LOG_LEVEL);

/** Data needed to maintain the current emulator state */
struct emul_clock_ctrl_data {
	/** The current clock rate */
	uint32_t rate;
	/** The current clock status */
	enum clock_control_status status;
	/** Async k_work structure */
	struct k_work async_on_work;
	/** Mutex used to guard the callback values */
	struct k_mutex cb_mutex;
	/** Async callback */
	clock_control_cb_t cb;
	/** Async user data to pass to the callback */
	void *cb_user_data;
	/** Async device to pass to the callback */
	const struct device *cb_dev;
	/** Async subsystem to pass to the callback */
	clock_control_subsys_t cb_subsys;
};

static int drv_clock_ctrl_on(const struct device *dev,
			     clock_control_subsys_t sys)
{
	struct emul_clock_ctrl_data *data = dev->data;
	int rc;

	k_mutex_lock(&data->cb_mutex, K_FOREVER);
	if (data->status == CLOCK_CONTROL_STATUS_OFF) {
		data->status = CLOCK_CONTROL_STATUS_ON;
		rc = 0;
	} else {
		LOG_ERR("Invalid clock status: %d", data->status);
		rc = EC_ERROR_UNIMPLEMENTED;
	}
	k_mutex_unlock(&data->cb_mutex);
	return 0;
}

static int drv_clock_ctrl_off(const struct device *dev,
			      clock_control_subsys_t sys)
{
	struct emul_clock_ctrl_data *data = dev->data;
	int rc;

	k_mutex_lock(&data->cb_mutex, K_FOREVER);
	if (data->status == CLOCK_CONTROL_STATUS_ON) {
		data->status = CLOCK_CONTROL_STATUS_OFF;
		rc = 0;
	} else {
		LOG_ERR("Invalid clock status: %d", data->status);
		rc = EC_ERROR_UNIMPLEMENTED;
	}
	k_mutex_unlock(&data->cb_mutex);
	return rc;
}

static void clock_ctrl_on_from_work(struct k_work *work)
{
	struct emul_clock_ctrl_data *data =
		CONTAINER_OF(work, struct emul_clock_ctrl_data, async_on_work);

	k_mutex_lock(&data->cb_mutex, K_FOREVER);
	if (data->status == CLOCK_CONTROL_STATUS_STARTING) {
		data->status = CLOCK_CONTROL_STATUS_ON;
		data->cb(data->cb_dev, data->cb_subsys, data->cb_user_data);
		data->cb = NULL;
		data->cb_dev = NULL;
		data->cb_subsys = NULL;
		data->cb_user_data = NULL;
	} else {
		LOG_ERR("Invalid clock status: %d", data->status);
	}
	k_mutex_unlock(&data->cb_mutex);
}

static int drv_clock_ctrl_async_on(const struct device *dev,
				   clock_control_subsys_t sys,
				   clock_control_cb_t cb, void *user_data)
{
	struct emul_clock_ctrl_data *data = dev->data;
	int rc;

	k_mutex_lock(&data->cb_mutex, K_FOREVER);
	switch (data->status) {
	case CLOCK_CONTROL_STATUS_ON:
		rc = 0;
		break;
	case CLOCK_CONTROL_STATUS_STARTING:
		rc = EC_ERROR_BUSY;
		break;
	case CLOCK_CONTROL_STATUS_OFF:
		data->status = CLOCK_CONTROL_STATUS_STARTING;
		data->cb = cb;
		data->cb_dev = dev;
		data->cb_subsys = sys;
		data->cb_user_data = user_data;

		k_work_init(&data->async_on_work, clock_ctrl_on_from_work);
		rc = k_work_submit(&data->async_on_work);
		if (rc > 0) {
			/*
			 * Work is already submitted, this should never happen
			 * because of the above cases.
			 */
			rc = EC_ERROR_UNKNOWN;
		} else if (rc < 0) {
			rc = EC_ERROR_BUSY;
		}
		break;
	default:
		LOG_ERR("Invalid clock status: %d", data->status);
		rc = EC_ERROR_UNIMPLEMENTED;
	}
	k_mutex_unlock(&data->cb_mutex);

	return rc;
}

static int drv_clock_ctrl_get_rate(const struct device *dev,
				   clock_control_subsys_t sys, uint32_t *rate)
{
	struct emul_clock_ctrl_data *data = dev->data;

	*rate = data->rate;
	return 0;
}

static enum clock_control_status
drv_clock_ctrl_get_status(const struct device *dev, clock_control_subsys_t sys)
{
	struct emul_clock_ctrl_data *data = dev->data;
	enum clock_control_status status;

	k_mutex_lock(&data->cb_mutex, K_FOREVER);
	status = data->status;
	k_mutex_unlock(&data->cb_mutex);

	return status;
}

static const struct clock_control_driver_api driver_api = {
	.on = drv_clock_ctrl_on,
	.off = drv_clock_ctrl_off,
	.async_on = drv_clock_ctrl_async_on,
	.get_rate = drv_clock_ctrl_get_rate,
	.get_status = drv_clock_ctrl_get_status,
};

static int drv_clock_ctrl_init(const struct device *dev)
{
	struct emul_clock_ctrl_data *data = dev->data;

	k_mutex_init(&data->cb_mutex);
	return 0;
}

#define INIT_CLOCK_CTRL(n)                                                   \
	static struct emul_clock_ctrl_data emul_clock_ctrl_data_##n = {      \
		.status = COND_CODE_1(DT_INST_PROP(n, clock_frequency) == 0, \
				      (CLOCK_CONTROL_STATUS_OFF),            \
				      (CLOCK_CONTROL_STATUS_ON)),            \
		.rate = DT_INST_PROP(n, clock_frequency),                    \
	};                                                                   \
	DEVICE_DT_INST_DEFINE(n, drv_clock_ctrl_init, NULL,                  \
			      &emul_clock_ctrl_data_##n, NULL, PRE_KERNEL_1, \
			      CONFIG_KERNEL_INIT_PRIORITY_OBJECTS,           \
			      &driver_api);

DT_INST_FOREACH_STATUS_OKAY(INIT_CLOCK_CTRL)

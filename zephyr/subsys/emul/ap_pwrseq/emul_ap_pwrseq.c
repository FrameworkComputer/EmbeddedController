/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "ap_power/ap_pwrseq.h"
#include "ap_power/ap_pwrseq_sm.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

struct ap_pwrseq_data {
	/* State machine data reference. */
	void *sm_data;
};

static struct ap_pwrseq_data emul_ap_pwrseq_data;

static int ap_pwrseq_driver_init(const struct device *dev);

DEVICE_DEFINE(ap_pwrseq_dev, "ap_pwrseq_drv", ap_pwrseq_driver_init, NULL,
	      &emul_ap_pwrseq_data, NULL, POST_KERNEL,
	      CONFIG_APPLICATION_INIT_PRIORITY, NULL);

/**
 * State machine prototypes
 **/
void *ap_pwrseq_sm_get_instance(void);

int ap_pwrseq_sm_init(void *const data, k_tid_t tid,
		      enum ap_pwrseq_state init_state);

int ap_pwrseq_sm_run_state(void *const data, uint32_t events);

enum ap_pwrseq_state ap_pwrseq_sm_get_cur_state(void *const data);

/**
 *  Private functions definition.
 **/
static int ap_pwrseq_driver_init(const struct device *dev)
{
	struct ap_pwrseq_data *const data = dev->data;

	data->sm_data = ap_pwrseq_sm_get_instance();

	return 0;
}

/**
 *  Global functions definition.
 **/
const struct device *ap_pwrseq_get_instance(void)
{
	return DEVICE_GET(ap_pwrseq_dev);
}

int ap_pwrseq_start(const struct device *dev, enum ap_pwrseq_state init_state)
{
	struct ap_pwrseq_data *const data = dev->data;

	return ap_pwrseq_sm_init(data->sm_data, NULL, init_state);
}

void ap_pwrseq_post_event(const struct device *dev, enum ap_pwrseq_event event)
{
	struct ap_pwrseq_data *const data = dev->data;
	enum ap_pwrseq_state cur_state, new_state;

	if (event >= AP_PWRSEQ_EVENT_COUNT) {
		return;
	}

	while (true) {
		cur_state = ap_pwrseq_sm_get_cur_state(data->sm_data);
		/**
		 * Given that thread is not created for emulator, run functions
		 * will be executed whenever an event is posted.
		 **/
		ap_pwrseq_sm_run_state(data->sm_data, (uint32_t)BIT(event));
		new_state = ap_pwrseq_sm_get_cur_state(data->sm_data);
		if (cur_state == new_state) {
			return;
		}
	}
}

enum ap_pwrseq_state ap_pwrseq_get_current_state(const struct device *dev)
{
	struct ap_pwrseq_data *const data = dev->data;
	enum ap_pwrseq_state ret_state;

	ret_state = ap_pwrseq_sm_get_cur_state(data->sm_data);

	return ret_state;
}

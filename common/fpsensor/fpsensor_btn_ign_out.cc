/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor_btn_ign_out.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>

LOG_MODULE_REGISTER(fp_btn_ign_out, LOG_LEVEL_INF);

namespace
{

const gpio_dt_spec btn_ign_gpio =
	GPIO_DT_SPEC_GET(DT_ALIAS(gpio_btn_ign_out), gpios);

int init()
{
	/* Configure and initialize in case zephyr didn't do it */
	int ret = gpio_pin_configure_dt(&btn_ign_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure power button ignore GPIO: %d",
			ret);
	}
	return ret;
}
SYS_INIT(init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

}

#ifdef CONFIG_CROS_EC_RW
namespace
{

K_SEM_DEFINE(btn_ign_lock, 1, 1);
bool is_active = false;

void deactivation_handler(struct k_work *)
{
	k_sem_take(&btn_ign_lock, K_FOREVER);
	if (!is_active) {
		gpio_pin_set_dt(&btn_ign_gpio, 0);
		LOG_INF("Power button ignore deactivated.");
	}
	k_sem_give(&btn_ign_lock);
}

K_WORK_DELAYABLE_DEFINE(deactivation_dwork, deactivation_handler);

void activate()
{
	k_sem_take(&btn_ign_lock, K_FOREVER);
	if (!is_active) {
		is_active = true;
		k_work_cancel_delayable(&deactivation_dwork);
		gpio_pin_set_dt(&btn_ign_gpio, 1);
		LOG_INF("Power button ignore activated.");
	}
	k_sem_give(&btn_ign_lock);
}

void deactivate()
{
	k_sem_take(&btn_ign_lock, K_FOREVER);
	if (is_active) {
		is_active = false;
		k_work_schedule(
			&deactivation_dwork,
			K_MSEC(CONFIG_PLATFORM_EC_FINGERPRINT_BTN_IGN_OUT_DELAY_MS));
	}
	k_sem_give(&btn_ign_lock);
}

} /* namespace */

namespace fp_btn_ign_out
{

void update(std::uint32_t sensor_mode)
{
	if (sensor_mode &
	    (FP_MODE_ENROLL_SESSION | FP_MODE_MATCH | FP_MODE_CAPTURE))
		activate();
	else
		deactivate();
}

} /* namespace fp_btn_ign_out */
#endif /* CONFIG_CROS_EC_RW */

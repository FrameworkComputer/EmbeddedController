/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_cse_early_rec

#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_pwrseq.h>

LOG_MODULE_REGISTER(cse_early_rec, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "only one 'cros-ec,cse-early-rec' compatible node may be present");

struct cse_early_rec_config {
	struct gpio_dt_spec cse_early_rec_gpio;
} cse_early_rec_cfg = {
	.cse_early_rec_gpio =
		GPIO_DT_SPEC_GET(DT_DRV_INST(0), cse_early_rec_gpios),
};

static void ap_power_g3_exit_cb(const struct device *dev,
				const enum ap_pwrseq_state entry,
				const enum ap_pwrseq_state exit)
{
	/* System is powering up from G3. Check the flags to see if a
	 * boot to recovery mode is happening and set the CSE early
	 * recovery GPIO appropriately.
	 *
	 * Note: this is NOT the authoritative mechanism for signalling
	 * recovery mode boot to the AP. That is performed by the EC
	 * sending a host event. This GPIO is read by the AP FW's CSE
	 * blob, which needs to be aware of recovery boots before the
	 * host event is sent.
	 */
	if (system_is_manual_recovery()) {
		LOG_INF("CSE early recovery signal asserted (recovery mode)");
		gpio_pin_set_dt(&cse_early_rec_cfg.cse_early_rec_gpio, 1);
	} else {
		LOG_INF("CSE early recovery signal de-asserted (normal mode)");
		gpio_pin_set_dt(&cse_early_rec_cfg.cse_early_rec_gpio, 0);
	}
}
AP_PWRSEQ_STATE_EXIT_CALLBACK_DEFINE(ap_power_g3_exit_cb, AP_POWER_STATE_G3);

/**
 * @brief Driver initialization function
 *
 *        Initialize the provided GPIO pin as an output and do not
 * assert it at this time.
 */
static int cse_early_rec_driver_init(const struct device *dev)
{
	int ret;
	const struct cse_early_rec_config *cfg = dev->config;

	if (!gpio_is_ready_dt(&cfg->cse_early_rec_gpio)) {
		/* LCOV_EXCL_START */
		LOG_ERR_DEVICE_NOT_READY(cfg->cse_early_rec_gpio.port);
		return -ENODEV;
		/* LCOV_EXCL_STOP */
	}

	ret = gpio_pin_configure_dt(&cfg->cse_early_rec_gpio,
				    GPIO_OUTPUT_INACTIVE);
	if (ret) {
		/* LCOV_EXCL_START */
		LOG_ERR("CSE early recovery pin config failed: %d", ret);
		return ret;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

/* This driver depends on GPIO drivers being ready. */
BUILD_ASSERT(CONFIG_CSE_EARLY_RECOVERY_GPIO_DRIVER_INIT_PRIORITY >
	     CONFIG_GPIO_INIT_PRIORITY);

DEVICE_DT_INST_DEFINE(0, cse_early_rec_driver_init, NULL, NULL,
		      &cse_early_rec_cfg, POST_KERNEL,
		      CONFIG_CSE_EARLY_RECOVERY_GPIO_DRIVER_INIT_PRIORITY,
		      NULL);

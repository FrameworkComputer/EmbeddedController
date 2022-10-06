/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

#include "config.h"
#include "hooks.h"
#include "watchdog.h"

LOG_MODULE_REGISTER(watchdog_shim, LOG_LEVEL_ERR);

#define wdt DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog))

#ifdef TEST_BUILD
extern bool wdt_warning_triggered;
#endif /* TEST_BUILD */

static void wdt_warning_handler(const struct device *wdt_dev, int channel_id)
{
	/* TODO(b/176523207): watchdog warning message */
	printk("Watchdog deadline is close!\n");
#ifdef TEST_BUILD
	wdt_warning_triggered = true;
#endif
#ifdef CONFIG_SOC_SERIES_MEC172X
	extern void cros_chip_wdt_handler(const struct device *wdt_dev,
					  int channel_id);
	cros_chip_wdt_handler(wdt_dev, channel_id);
#endif
}

int watchdog_init(void)
{
	int err;
	struct wdt_timeout_cfg wdt_config;

	if (!device_is_ready(wdt)) {
		LOG_ERR("Error: device %s is not ready", wdt->name);
		return -1;
	}

	/* Reset SoC when watchdog timer expires. */
	wdt_config.flags = WDT_FLAG_RESET_SOC;

	/*
	 * Set the Warning timer as CONFIG_AUX_TIMER_PERIOD_MS.
	 * Then the watchdog reset time = CONFIG_WATCHDOG_PERIOD_MS.
	 */
	wdt_config.window.min = 0U;
	wdt_config.window.max = CONFIG_AUX_TIMER_PERIOD_MS;
	wdt_config.callback = wdt_warning_handler;

	err = wdt_install_timeout(wdt, &wdt_config);

	/* If watchdog is running, reinstall it. */
	if (err == -EBUSY) {
		wdt_disable(wdt);
		err = wdt_install_timeout(wdt, &wdt_config);
	}

	if (err < 0) {
		LOG_ERR("Watchdog install error");
		return err;
	}

	err = wdt_setup(wdt, 0);
	if (err < 0) {
		LOG_ERR("Watchdog setup error");
		return err;
	}

	return EC_SUCCESS;
}

void watchdog_reload(void)
{
	if (!device_is_ready(wdt))
		LOG_ERR("Error: device %s is not ready", wdt->name);

	wdt_feed(wdt, 0);
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

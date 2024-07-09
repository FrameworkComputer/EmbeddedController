/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "config.h"
#include "hooks.h"
#include "panic.h"
#include "task.h"
#include "watchdog.h"

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(watchdog_shim, LOG_LEVEL_ERR);

struct watchdog_info {
	const struct device *wdt_dev;
	struct wdt_timeout_cfg config;
};

__maybe_unused static void wdt_warning_handler(const struct device *wdt_dev,
					       int channel_id);
__maybe_unused static void
wdt_warning_handler_with_enable(const struct device *wdt_dev, int channel_id);

const struct watchdog_info wdt_info[] = {
#if DT_NODE_HAS_STATUS(DT_CHOSEN(cros_ec_watchdog), okay)
	{
		.wdt_dev = DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog)),
		.config = {
#if DT_NODE_HAS_COMPAT(DT_CHOSEN(cros_ec_watchdog), st_stm32_watchdog)
			.flags = WDT_FLAG_RESET_SOC,
			.window.min = 0U,
			.window.max = CONFIG_WATCHDOG_PERIOD_MS,
			.callback = NULL,
#else
			.flags = WDT_FLAG_RESET_SOC,
			.window.min = 0U,
			.window.max = CONFIG_AUX_TIMER_PERIOD_MS,
			.callback = wdt_warning_handler,
#endif
		},
	},
#endif
#ifdef CONFIG_PLATFORM_EC_WATCHDOG_HELPER
	{
		.wdt_dev = DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog_helper)),
		.config = {
			.flags = 0U,
			.window.min = 0U,
			.window.max = CONFIG_AUX_TIMER_PERIOD_MS,
			.callback = wdt_warning_handler_with_enable,
		},
	},
#endif
};

/* Array to keep channel used to implement watchdog */
int wdt_chan[ARRAY_SIZE(wdt_info)];
bool watchdog_initialized;

#ifdef TEST_BUILD
bool wdt_warning_triggered;
#endif /* TEST_BUILD */

static int watchdog_config(const struct watchdog_info *info)
{
	const struct device *wdt_dev = info->wdt_dev;
	const struct wdt_timeout_cfg *config = &info->config;
	int chan;

	chan = wdt_install_timeout(wdt_dev, config);

	/* If watchdog is running, reinstall it. */
	if (chan == -EBUSY) {
		wdt_disable(wdt_dev);
		chan = wdt_install_timeout(wdt_dev, config);
	}

	if (chan < 0) {
		LOG_ERR("Watchdog install error: %d", chan);
	}

	return chan;
}

static int watchdog_enable(const struct device *wdt_dev)
{
	int err;

	err = wdt_setup(wdt_dev, 0);
	if (err < 0)
		LOG_ERR("Watchdog %s setup error: %d", wdt_dev->name, err);

	return err;
}

static int watchdog_init_device(const struct watchdog_info *info)
{
	const struct device *wdt_dev = info->wdt_dev;
	int chan, err;

	if (!device_is_ready(wdt_dev)) {
		LOG_ERR("device %s not ready", wdt_dev->name);
		return -ENODEV;
	}

	chan = watchdog_config(info);
	if (chan < 0)
		return chan;

	err = watchdog_enable(wdt_dev);
	if (err < 0)
		return err;

	return chan;
}

int watchdog_init(void)
{
	int err = EC_SUCCESS;

	if (watchdog_initialized)
		return -EBUSY;

	for (int i = 0; i < ARRAY_SIZE(wdt_info); i++) {
		wdt_chan[i] = watchdog_init_device(&wdt_info[i]);
		if (wdt_chan[i] < 0 && err == EC_SUCCESS)
			err = wdt_chan[i];
	}

	watchdog_initialized = true;
	watchdog_reload();

	return err;
}

void watchdog_reload(void)
{
	if (!watchdog_initialized)
		return;

	for (int i = 0; i < ARRAY_SIZE(wdt_info); i++) {
		if (wdt_chan[i] < 0)
			continue;

		wdt_feed(wdt_info[i].wdt_dev, wdt_chan[i]);
	}
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

__maybe_unused static void wdt_warning_handler(const struct device *wdt_dev,
					       int channel_id)
{
	const char *thread_name = k_thread_name_get(k_current_get());

#ifdef CONFIG_RISCV
	printk("WDT pre-warning MEPC:%p THREAD_NAME:%s\n",
	       (void *)csr_read(mepc), thread_name);
#else
	/* TODO(b/176523207): watchdog warning message */
	printk("Watchdog deadline is close! THREAD_NAME:%s\n", thread_name);
#endif
#ifdef TEST_BUILD
	wdt_warning_triggered = true;
#endif
#ifdef CONFIG_SOC_SERIES_MEC172X
	extern void cros_chip_wdt_handler(const struct device *wdt_dev,
					  int channel_id);
	cros_chip_wdt_handler(wdt_dev, channel_id);
#endif

	/* Save the current task id in panic info.
	 * The PANIC_SW_WATCHDOG_WARN reason will be changed to a regular
	 * PANIC_SW_WATCHDOG in system_common_pre_init if a watchdog reset
	 * occurs.
	 */
	panic_set_reason(PANIC_SW_WATCHDOG_WARN, 0, task_get_current());
}

__maybe_unused static void
wdt_warning_handler_with_enable(const struct device *wdt_dev, int channel_id)
{
	wdt_warning_handler(wdt_dev, channel_id);
	/* Watchdog is disabled after calling handler. Re-enable it now. */
	watchdog_enable(wdt_dev);
}

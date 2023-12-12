/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT st_stm32_rcc

#include "drivers/cros_system.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/reboot.h>

/* Driver data */
struct cros_system_stm32_data {
	int reset; /* reset cause */
};

#define DRV_DATA(dev) ((struct cros_system_stm32_data *)(dev)->data)

static const struct device *const watchdog =
	DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog));

static const char *cros_system_stm32_get_chip_vendor(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "st";
}

static const char *cros_system_stm32_get_chip_name(const struct device *dev)
{
	ARG_UNUSED(dev);

	return CONFIG_SOC;
}

static const char *cros_system_stm32_get_chip_revision(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "";
}

static int cros_system_stm32_get_reset_cause(const struct device *dev)
{
	struct cros_system_stm32_data *data = DRV_DATA(dev);

	return data->reset;
}

static int cros_system_stm32_soc_reset(const struct device *dev)
{
	ARG_UNUSED(dev);

	uint32_t chip_reset_flags = chip_read_reset_flags();

	/*
	 * We are going to reboot MCU here, so we need to disable caches here.
	 * SCB_DisableDCache also flushes data cache lines.
	 */
#ifdef CONFIG_DCACHE
	SCB_DisableDCache();
#endif

#ifdef CONFIG_ICACHE
	SCB_DisableICache();
#endif

	if (chip_reset_flags & EC_RESET_FLAG_HARD) {
		/*
		 * Set minimal watchdog timeout - 1 millisecond.
		 * STM32 IWDG can be set for lower value, but we are limited by
		 * Zephyr API.
		 */
		struct wdt_timeout_cfg minimal_timeout = { .window.max = 1 };

		/* Setup watchdog */
		wdt_install_timeout(watchdog, &minimal_timeout);

		/* Apply the changes (the driver will reload watchdog) */
		wdt_setup(watchdog, 0);

		/* Spin and wait for reboot */
		while (1)
			;
	} else {
		/* Reset implementation for ARM ignores the reset type */
		sys_reboot(0);
	}

	/* Should never return */
	return 0;
}

__maybe_unused static uint64_t
cros_system_stm32_deep_sleep_ticks(const struct device *dev)
{
	/* Deep sleep is not supported for now */
	return 0;
}

static int cros_system_stm32_init(const struct device *dev)
{
	struct cros_system_stm32_data *data = DRV_DATA(dev);
	uint32_t reset_cause;

	data->reset = UNKNOWN_RST;
	hwinfo_get_reset_cause(&reset_cause);

	/* Clear the hardware reset cause. */
	hwinfo_clear_reset_cause();

	if (reset_cause & RESET_WATCHDOG) {
		data->reset = WATCHDOG_RST;
	} else if (reset_cause & RESET_SOFTWARE) {
		/* Use DEBUG_RST because it maps to EC_RESET_FLAG_SOFT. */
		data->reset = DEBUG_RST;
	} else if (reset_cause & RESET_POR) {
		data->reset = POWERUP;
	} else if (reset_cause & RESET_PIN) {
		data->reset = VCC1_RST_PIN;
	}

	return 0;
}

static struct cros_system_stm32_data cros_system_stm32_dev_data;

static const struct cros_system_driver_api cros_system_driver_stm32_api = {
	.get_reset_cause = cros_system_stm32_get_reset_cause,
	.soc_reset = cros_system_stm32_soc_reset,
	.chip_vendor = cros_system_stm32_get_chip_vendor,
	.chip_name = cros_system_stm32_get_chip_name,
	.chip_revision = cros_system_stm32_get_chip_revision,
#ifdef CONFIG_PM
	.deep_sleep_ticks = cros_system_stm32_deep_sleep_ticks,
#endif
};

DEVICE_DEFINE(cros_system_stm32_0, "CROS_SYSTEM", cros_system_stm32_init, NULL,
	      &cros_system_stm32_dev_data, NULL, PRE_KERNEL_1,
	      CONFIG_CROS_SYSTEM_STM32_INIT_PRIORITY,
	      &cros_system_driver_stm32_api);

#if CONFIG_CROS_SYSTEM_STM32_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif

/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/cros_system.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/init.h>
#include <zephyr/sys/reboot.h>

static int reset_cause = UNKNOWN_RST;

static const struct device *const watchdog =
	DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog));

const char *cros_system_chip_vendor(void)
{
	return "st";
}

const char *cros_system_chip_name(void)
{
	return CONFIG_SOC;
}

const char *cros_system_chip_revision(void)
{
	return "";
}

int cros_system_get_reset_cause(void)
{
	return reset_cause;
}

int cros_system_soc_reset(void)
{
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

#ifdef CONFIG_PM
uint64_t cros_system_deep_sleep_ticks(void)
{
	/* Deep sleep is not supported for now */
	return 0;
}
#endif

static int cros_system_stm32_init(void)
{
	uint32_t hw_reset_cause;

	reset_cause = UNKNOWN_RST;
	hwinfo_get_reset_cause(&hw_reset_cause);

	/* Clear the hardware reset cause. */
	hwinfo_clear_reset_cause();

	if (hw_reset_cause & RESET_WATCHDOG) {
		reset_cause = WATCHDOG_RST;
	} else if (hw_reset_cause & RESET_SOFTWARE) {
		/* Use DEBUG_RST because it maps to EC_RESET_FLAG_SOFT. */
		reset_cause = DEBUG_RST;
	} else if (hw_reset_cause & RESET_POR) {
		reset_cause = POWERUP;
	} else if (hw_reset_cause & RESET_PIN) {
		reset_cause = VCC1_RST_PIN;
	}

	return 0;
}

SYS_INIT(cros_system_stm32_init, PRE_KERNEL_1,
	 CONFIG_CROS_SYSTEM_INIT_PRIORITY);

#if CONFIG_CROS_SYSTEM_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif

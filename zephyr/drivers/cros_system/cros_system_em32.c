/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/cros_system.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

/* Make sure the watchdog config is enabled. */
BUILD_ASSERT(IS_ENABLED(CONFIG_WATCHDOG),
	     "Watchdog should be enabled for elan em32 cros system.");

LOG_MODULE_REGISTER(cros_system);

/* Driver data */
struct cros_system_em32_data {
	int reset; /* reset cause */
};

static struct cros_system_em32_data cros_system_em32_data;
#define DRV_DATA() (&cros_system_em32_data)

/* Data structure of hwinfo_em32 driver */
/*
 * TODO(b/450710838): The em32_hwinfo structure is supported by
 * elan hwinfo_em32 driver, which will be upstreamed later.
 */
struct em32_hwinfo {
	uint32_t chip_id;
	uint32_t device_id;
	uint32_t ic_version;
};

static const struct device *const watchdog =
	DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog));

/* Soc specific system local functions */
static int system_em32_watchdog_stop(void)
{
	if (!device_is_ready(watchdog)) {
		LOG_ERR_DEVICE_NOT_READY(watchdog);
		return -ENODEV;
	}

	wdt_disable(watchdog);

	return 0;
}

static int system_em32_get_chip_id(uint32_t *chip_id)
{
	struct em32_hwinfo dev_hwinfo = { 0 };
	ssize_t ret = 0;

	/* Validate Input Pointer */
	if (chip_id == NULL) {
		LOG_ERR("Invalid chip_id buffer!");
		return -EPERM;
	}

	/* Get HW Info. */
	ret = hwinfo_get_device_id((uint8_t *)&dev_hwinfo, sizeof(dev_hwinfo));
	if (ret < 0) {
		LOG_ERR("hwinfo_get_device_id fail, err=%d.", ret);
		return (int)ret;
	}

	/* Load chip ID to input buffer */
	*chip_id = dev_hwinfo.chip_id;
	LOG_DBG("chip_id: 0x%x.", *chip_id);

	return 0;
}

static int system_em32_get_chip_version(uint8_t *chip_version)
{
	struct em32_hwinfo dev_hwinfo = { 0 };
	ssize_t ret = 0;
	uint8_t second_last_byte = 0;

	/* Validate Input Pointer */
	if (chip_version == NULL) {
		LOG_ERR("Invalid chip_version buffer!");
		return -EPERM;
	}

	/* Get HW Info. */
	ret = hwinfo_get_device_id((uint8_t *)&dev_hwinfo, sizeof(dev_hwinfo));
	if (ret < 0) {
		LOG_ERR("hwinfo_get_device_id fail, err=%d.", ret);
		return (int)ret;
	}

	/* Load chip version (take the two's complement of second_last_byte)
	 * to input buffer.
	 */
	second_last_byte = (uint8_t)((dev_hwinfo.ic_version & 0x0000ff00) >> 8);
	*chip_version = (uint8_t)(~second_last_byte + 1);
	LOG_DBG("chip_version: 0x%02x.", *chip_version);

	return 0;
}

const char *cros_system_chip_vendor(void)
{
	return "elan";
}

const char *cros_system_chip_name(void)
{
	static char buf[9] = { 'e', 'm', '3', '2', 'f' };
	int ret = 0;
	uint32_t chip_id = 0;

	/* Get Chip ID */
	ret = system_em32_get_chip_id(&chip_id);
	if (ret < 0) {
		LOG_ERR("system_em32_get_chip_id fail, err=%d.", ret);
		return "";
	}

	/* Set Chip Name String */
	snprintk(buf + 5, sizeof(buf) - 5, "%03x", chip_id);
	buf[8] = '\0';

	return buf;
}

const char *cros_system_chip_revision(void)
{
	static char buf[3] = { 0 };
	int ret = 0;
	uint8_t chip_version = 0;

	/* Get Chip Version */
	ret = system_em32_get_chip_version(&chip_version);
	if (ret < 0) {
		LOG_ERR("system_em32_get_chip_version fail, err=%d.", ret);
		return "";
	}

	/* Set Chip Version String */
	snprintk(buf, sizeof(buf), "%02x", chip_version);
	buf[2] = '\0';

	return buf;
}

int cros_system_get_reset_cause(void)
{
	struct cros_system_em32_data *data = DRV_DATA();

	LOG_DBG("cros_system_em32_get_reset_cause reset 0x%x", data->reset);
	return data->reset;
}

int cros_system_soc_reset(void)
{
	/*
	 * Set minimal watchdog timeout - 1 millisecond.
	 * Elan EM32 WDT can be set for lower value, but we are limited by
	 * Zephyr API.
	 */
	struct wdt_timeout_cfg minimal_timeout = { .window.max = 1 };

	LOG_DBG("cros_system_em32_soc_reset");

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

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable_all();

	/* Stop watchdog */
	system_em32_watchdog_stop();

	/* Setup watchdog */
	wdt_install_timeout(watchdog, &minimal_timeout);

	/* Apply the changes (the driver will reload watchdog) */
	wdt_setup(watchdog, 0);

	/* Spin and wait for reboot */
	while (1)
		;

	/* Should never return */
	return 0;
}

static int cros_system_em32_init(void)
{
	struct cros_system_em32_data *data = DRV_DATA();
	uint32_t reset_cause;

	data->reset = UNKNOWN_RST;
	hwinfo_get_reset_cause(&reset_cause);

	if (reset_cause & RESET_WATCHDOG) {
		data->reset = WATCHDOG_RST;
	} else if (reset_cause & RESET_SOFTWARE) {
		/* Use DEBUG_RST because it maps to EC_RESET_FLAG_SOFT. */
		data->reset = DEBUG_RST;
	} else if (reset_cause & RESET_BROWNOUT) {
		data->reset = POWERUP;
	} else if (reset_cause & RESET_PIN) {
		data->reset = VCC1_RST_PIN;
	} else if (reset_cause & RESET_LOW_POWER_WAKE) {
		/* Use DEBUG_RST because it maps to RESET_LOW_POWER_WAKE. */
		data->reset = DEBUG_RST;
	}

	return 0;
}

SYS_INIT(cros_system_em32_init, PRE_KERNEL_1, CONFIG_CROS_SYSTEM_INIT_PRIORITY);

#if CONFIG_CROS_SYSTEM_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif

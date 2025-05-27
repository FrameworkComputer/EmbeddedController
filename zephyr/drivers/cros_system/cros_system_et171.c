/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/cros_system.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/syscon.h>

/* TODO(b/452878239): Use defines from Egis HAL once it is upstreamed. */
/* Registers definitions */
#define AOSMU_SECURE_CON 0xc /* Secure key handling */
#define AOSMU_SECURE_CON_SYSTEM_RESET BIT(1) /* issue reset to whole SoC */

/* Driver data */
struct cros_system_et171_data {
	int reset; /* reset cause */
};

/* It is AOSMU Egis IC. */
static const struct device *const syscon_dev =
	DEVICE_DT_GET(DT_NODELABEL(syscon));

#define DRV_DATA(dev) ((struct cros_system_et171_data *)(dev)->data)

static const char *cros_system_et171_get_chip_vendor(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "egis";
}

static const char *cros_system_et171_get_chip_name(const struct device *dev)
{
	ARG_UNUSED(dev);

	return CONFIG_SOC;
}

static const char *cros_system_et171_get_chip_revision(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "";
}

static int cros_system_et171_get_reset_cause(const struct device *dev)
{
	struct cros_system_et171_data *data = DRV_DATA(dev);

	return data->reset;
}

static int cros_system_et171_soc_reset(const struct device *dev)
{
	ARG_UNUSED(dev);

	uint32_t reg = 0;
	int ret;

	ret = syscon_read_reg(syscon_dev, AOSMU_SECURE_CON, &reg);
	if (ret) {
		return ret;
	}

	reg |= AOSMU_SECURE_CON_SYSTEM_RESET;
	syscon_write_reg(syscon_dev, AOSMU_SECURE_CON, reg);

	/* Should never return */
	return 0;
}

__maybe_unused static uint64_t
cros_system_et171_deep_sleep_ticks(const struct device *dev)
{
	return 0;
}

static int cros_system_et171_init(const struct device *dev)
{
	struct cros_system_et171_data *data = DRV_DATA(dev);
	uint32_t reset_cause;

	data->reset = UNKNOWN_RST;
	hwinfo_get_reset_cause(&reset_cause);
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

static struct cros_system_et171_data cros_system_et171_dev_data;

static DEVICE_API(cros_system, cros_system_driver_et171_api) = {
	.get_reset_cause = cros_system_et171_get_reset_cause,
	.soc_reset = cros_system_et171_soc_reset,
	.chip_vendor = cros_system_et171_get_chip_vendor,
	.chip_name = cros_system_et171_get_chip_name,
	.chip_revision = cros_system_et171_get_chip_revision,
#ifdef CONFIG_PM
	.deep_sleep_ticks = cros_system_et171_deep_sleep_ticks,
#endif
};

DEVICE_DEFINE(cros_system_et171_0, "CROS_SYSTEM", cros_system_et171_init, NULL,
	      &cros_system_et171_dev_data, NULL, PRE_KERNEL_1,
	      CONFIG_CROS_SYSTEM_ET171_INIT_PRIORITY,
	      &cros_system_driver_et171_api);

#if CONFIG_CROS_SYSTEM_ET171_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif

/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/cros_system.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/reboot.h>

/* Driver data */
struct cros_system_ft_data {
	int reset; /* reset cause */
};

#define DRV_DATA(dev) ((struct cros_system_ft_data *)(dev)->data)

#if !DT_NODE_EXISTS(DT_NODELABEL(rst))
#error "rst node must be exists"
#else
#define RESET_BASE_ADDR (DT_REG_ADDR(DT_NODELABEL(rst)))
#endif

#define RCR_OFFSET 0
#define RESET_RCR_SOFTRST BIT(31)

static const char *cros_system_ft_get_chip_vendor(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "ft";
}

static const char *cros_system_ft_get_chip_name(const struct device *dev)
{
	ARG_UNUSED(dev);

	return CONFIG_SOC;
}

static const char *cros_system_ft_get_chip_revision(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "";
}

static int cros_system_ft_get_reset_cause(const struct device *dev)
{
	struct cros_system_ft_data *data = DRV_DATA(dev);

	return data->reset;
}

static int cros_system_ft_soc_reset(const struct device *dev)
{
	ARG_UNUSED(dev);
	uint32_t value;

	value = sys_read32(RESET_BASE_ADDR + RCR_OFFSET);
	sys_write32(value | RESET_RCR_SOFTRST, RESET_BASE_ADDR + RCR_OFFSET);
	/* Should never return */
	return 0;
}

static int cros_system_ft_init(const struct device *dev)
{
	struct cros_system_ft_data *data = DRV_DATA(dev);
	uint32_t reset_cause;

	data->reset = UNKNOWN_RST;
	hwinfo_get_reset_cause(&reset_cause);

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

static struct cros_system_ft_data cros_system_ft_dev_data;

static DEVICE_API(cros_system, cros_system_driver_ft_api) = {
	.get_reset_cause = cros_system_ft_get_reset_cause,
	.soc_reset = cros_system_ft_soc_reset,
	.chip_vendor = cros_system_ft_get_chip_vendor,
	.chip_name = cros_system_ft_get_chip_name,
	.chip_revision = cros_system_ft_get_chip_revision,
};

DEVICE_DEFINE(cros_system_ft_0, "CROS_SYSTEM", cros_system_ft_init, NULL,
	      &cros_system_ft_dev_data, NULL, PRE_KERNEL_1,
	      CONFIG_CROS_SYSTEM_FT90_INIT_PRIORITY,
	      &cros_system_driver_ft_api);

#if CONFIG_CROS_SYSTEM_FT90_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif

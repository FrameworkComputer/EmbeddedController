/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/cros_system.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/syscon.h>
#include <zephyr/init.h>

/* TODO(b/452878239): Use defines from Egis HAL once it is upstreamed. */
/* Registers definitions */
#define AOSMU_SECURE_CON 0xc /* Secure key handling */
#define AOSMU_SECURE_CON_SYSTEM_RESET BIT(1) /* issue reset to whole SoC */

static int reset_cause;

/* It is AOSMU Egis IC. */
static const struct device *const syscon_dev =
	DEVICE_DT_GET(DT_NODELABEL(syscon));

const char *cros_system_chip_vendor(void)
{
	return "egis";
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

#ifdef CONFIG_PM
uint64_t cros_system_deep_sleep_ticks(void)
{
	return 0;
}
#endif

static int cros_system_et171_init(void)
{
	uint32_t hw_reset_cause;

	reset_cause = UNKNOWN_RST;
	hwinfo_get_reset_cause(&hw_reset_cause);
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

SYS_INIT(cros_system_et171_init, PRE_KERNEL_1,
	 CONFIG_CROS_SYSTEM_INIT_PRIORITY);

#if CONFIG_CROS_SYSTEM_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif

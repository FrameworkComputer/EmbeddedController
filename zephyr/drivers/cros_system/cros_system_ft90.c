/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/cros_system.h"
#include "system.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/init.h>
#include <zephyr/sys/reboot.h>

/* Static driver data */
static int reset_cause_val = UNKNOWN_RST;

#if !DT_NODE_EXISTS(DT_NODELABEL(rst))
#error "rst node must be exists"
#else
#define RESET_BASE_ADDR (DT_REG_ADDR(DT_NODELABEL(rst)))
#endif

#define RCR_OFFSET 0
#define RESET_RCR_SOFTRST BIT(31)

const char *cros_system_chip_vendor(void)
{
	return "ft";
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
	return reset_cause_val;
}

int cros_system_soc_reset(void)
{
	uint32_t value;

	value = sys_read32(RESET_BASE_ADDR + RCR_OFFSET);
	sys_write32(value | RESET_RCR_SOFTRST, RESET_BASE_ADDR + RCR_OFFSET);
	/* Should never return */
	return 0;
}

uint64_t cros_system_deep_sleep_ticks(void)
{
	return 0;
}

int cros_system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	ARG_UNUSED(seconds);
	ARG_UNUSED(microseconds);
	return -ENOSYS;
}

int cros_system_get_hibernate_wake_source(enum hibernate_wake_source *source)
{
	ARG_UNUSED(source);
	return -ENOSYS;
}

static int cros_system_ft_init(void)
{
	uint32_t hwinfo_reset_cause;

	reset_cause_val = UNKNOWN_RST;
	hwinfo_get_reset_cause(&hwinfo_reset_cause);

	if (hwinfo_reset_cause & RESET_WATCHDOG) {
		reset_cause_val = WATCHDOG_RST;
	} else if (hwinfo_reset_cause & RESET_SOFTWARE) {
		/* Use DEBUG_RST because it maps to EC_RESET_FLAG_SOFT. */
		reset_cause_val = DEBUG_RST;
	} else if (hwinfo_reset_cause & RESET_POR) {
		reset_cause_val = POWERUP;
	} else if (hwinfo_reset_cause & RESET_PIN) {
		reset_cause_val = VCC1_RST_PIN;
	}

	return 0;
}

SYS_INIT(cros_system_ft_init, PRE_KERNEL_1, CONFIG_CROS_SYSTEM_INIT_PRIORITY);

#if CONFIG_CROS_SYSTEM_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif

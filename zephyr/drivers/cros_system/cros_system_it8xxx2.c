/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT ite_it8xxx2_gctrl

#include <device.h>
#include <drivers/cros_system.h>
#include <logging/log.h>
#include <soc.h>
#include <soc/ite_it8xxx2/reg_def_cros.h>

#include "system.h"

LOG_MODULE_REGISTER(cros_system, LOG_LEVEL_ERR);

#define GCTRL_IT8XXX2_REG_BASE \
	((struct gctrl_it8xxx2_regs *)DT_INST_REG_ADDR(0))

#define WDT_IT8XXX2_REG_BASE \
	((struct wdt_it8xxx2_regs *)DT_REG_ADDR(DT_NODELABEL(twd0)))

static const char *cros_system_it8xxx2_get_chip_vendor(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "ite";
}

static uint32_t system_get_chip_id(void)
{
	struct gctrl_it8xxx2_regs *const gctrl_base = GCTRL_IT8XXX2_REG_BASE;

	return (gctrl_base->GCTRL_ECHIPID1 << 16) |
		(gctrl_base->GCTRL_ECHIPID2 << 8) |
		gctrl_base->GCTRL_ECHIPID3;
}

static uint8_t system_get_chip_version(void)
{
	struct gctrl_it8xxx2_regs *const gctrl_base = GCTRL_IT8XXX2_REG_BASE;

	/* bit[3-0], chip version */
	return gctrl_base->GCTRL_ECHIPVER & 0x0F;
}

static const char *cros_system_it8xxx2_get_chip_name(const struct device *dev)
{
	ARG_UNUSED(dev);

	static char buf[8] = {'i', 't'};
	uint32_t chip_id = system_get_chip_id();
	int num = 4;

	for (int n = 2; num >= 0; n++, num--)
		snprintf(buf+n, (sizeof(buf)-n), "%x",
			 chip_id >> (num * 4) & 0xF);

	return buf;
}

static const char *cros_system_it8xxx2_get_chip_revision(const struct device
							 *dev)
{
	ARG_UNUSED(dev);

	static char buf[3];
	uint8_t rev = system_get_chip_version();

	snprintf(buf, sizeof(buf), "%1xx", rev+0xa);

	return buf;
}

static int cros_system_it8xxx2_get_reset_cause(const struct device *dev)
{
	ARG_UNUSED(dev);
	struct gctrl_it8xxx2_regs *const gctrl_base = GCTRL_IT8XXX2_REG_BASE;
	/* system reset flag */
	uint32_t system_flags = chip_read_reset_flags();
	int chip_reset_cause = 0;
	uint8_t raw_reset_cause = gctrl_base->GCTRL_RSTS & IT8XXX2_GCTRL_LRS;
	uint8_t raw_reset_cause2 = gctrl_base->GCTRL_SPCTRL4 &
		(IT8XXX2_GCTRL_LRSIWR | IT8XXX2_GCTRL_LRSIPWRSWTR |
		IT8XXX2_GCTRL_LRSIPGWR);

	/* Clear reset cause. */
	gctrl_base->GCTRL_RSTS |= IT8XXX2_GCTRL_LRS;
	gctrl_base->GCTRL_SPCTRL4 |= (IT8XXX2_GCTRL_LRSIWR |
		IT8XXX2_GCTRL_LRSIPWRSWTR | IT8XXX2_GCTRL_LRSIPGWR);

	/* Determine if watchdog reset or power on reset. */
	if (raw_reset_cause & IT8XXX2_GCTRL_IWDTR) {
		system_flags |= EC_RESET_FLAG_WATCHDOG;
		chip_reset_cause = WATCHDOG_RST;
	} else if (raw_reset_cause < 2) {
		system_flags |= EC_RESET_FLAG_POWER_ON;
		chip_reset_cause = POWERUP;
	}
	/* Determine reset-pin reset. */
	if (raw_reset_cause2 & IT8XXX2_GCTRL_LRSIWR) {
		system_flags |= EC_RESET_FLAG_RESET_PIN;
		chip_reset_cause = VCC1_RST_PIN;
	}

	/* watchdog module triggers these reset */
	if (system_flags & (EC_RESET_FLAG_HARD | EC_RESET_FLAG_SOFT))
		system_flags &= ~EC_RESET_FLAG_WATCHDOG;

	/* Set the system reset flags. */
	system_set_reset_flags(system_flags);

	return chip_reset_cause;
}

static int cros_system_it8xxx2_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static int cros_system_it8xxx2_soc_reset(const struct device *dev)
{
	struct wdt_it8xxx2_regs *const wdt_base = WDT_IT8XXX2_REG_BASE;

	/* Disable interrupts to avoid task swaps during reboot. */
	interrupt_disable_all();

	/*
	 * Writing invalid key to watchdog module triggers a soft or hardware
	 * reset. It depends on the setting of bit0 at ETWDUARTCR register.
	 */
	wdt_base->ETWCFG |= IT8XXX2_WDT_EWDKEYEN;
	wdt_base->EWDKEYR = 0x00;

	/* Spin and wait for reboot */
	while (1)
		;

	/* Should never return */
	return 0;
}

static int cros_system_it8xxx2_hibernate(const struct device *dev,
					 uint32_t seconds,
					 uint32_t microseconds)
{
	/* TODO: To implement the hibernate mode */

	return 0;
}

static const struct cros_system_driver_api cros_system_driver_it8xxx2_api = {
	.get_reset_cause = cros_system_it8xxx2_get_reset_cause,
	.soc_reset = cros_system_it8xxx2_soc_reset,
	.hibernate = cros_system_it8xxx2_hibernate,
	.chip_vendor = cros_system_it8xxx2_get_chip_vendor,
	.chip_name = cros_system_it8xxx2_get_chip_name,
	.chip_revision = cros_system_it8xxx2_get_chip_revision,
};

#if CONFIG_CROS_SYSTEM_IT8XXX2_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif
DEVICE_DEFINE(cros_system_it8xxx2_0, "CROS_SYSTEM", cros_system_it8xxx2_init,
	      NULL, NULL, NULL, PRE_KERNEL_1,
	      CONFIG_CROS_SYSTEM_IT8XXX2_INIT_PRIORITY,
	      &cros_system_driver_it8xxx2_api);

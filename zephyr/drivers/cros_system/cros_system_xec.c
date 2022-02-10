/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <arch/arm/aarch32/cortex_m/cmsis.h>
#include <drivers/cros_system.h>
#include <drivers/watchdog.h>
#include <logging/log.h>
#include <soc.h>
#include <soc/microchip_xec/reg_def_cros.h>
#include <sys/util.h>

#include "system.h"
#include "system_chip.h"

LOG_MODULE_REGISTER(cros_system, LOG_LEVEL_ERR);

/* Driver config */
struct cros_system_xec_config {
	/* hardware module base address */
	uintptr_t base_pcr;
	uintptr_t base_vbr;
	uintptr_t base_wdog;
};

/* Driver data */
struct cros_system_xec_data {
	int reset; /* reset cause */
};

/* Driver convenience defines */
#define DRV_CONFIG(dev) ((const struct cros_system_xec_config *)(dev)->config)
#define DRV_DATA(dev) ((struct cros_system_xec_data *)(dev)->data)

#define HAL_PCR_INST(dev) (struct pcr_regs *)(DRV_CONFIG(dev)->base_pcr)
#define HAL_VBATR_INST(dev) (struct vbatr_regs *)(DRV_CONFIG(dev)->base_vbr)
#define HAL_WDOG_INST(dev) (struct wdt_regs *)(DRV_CONFIG(dev)->base_wdog)

/* Get saved reset flag address in battery-backed ram */
#define BBRAM_SAVED_RESET_FLAG_ADDR                         \
	(DT_REG_ADDR(DT_INST(0, microchip_xec_bbram)) + \
	 DT_PROP(DT_PATH(named_bbram_regions, saved_reset_flags), offset))

/* Soc specific system local functions */
static int system_xec_watchdog_stop(void)
{
	if (IS_ENABLED(CONFIG_WATCHDOG)) {
		const struct device *wdt_dev = DEVICE_DT_GET(
				DT_NODELABEL(wdog));
		if (!device_is_ready(wdt_dev)) {
			LOG_ERR("Error: device %s is not ready", wdt_dev->name);
			return -ENODEV;
		}

		wdt_disable(wdt_dev);
	}

	return 0;
}

static const char *cros_system_xec_get_chip_vendor(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "MCHP";
}

/* TODO - return specific chip name such as MEC1727 or MEC1723 */
static const char *cros_system_xec_get_chip_name(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "MEC172X";
}

/* TODO return chip revision from HW as an ASCII string */
static const char *cros_system_xec_get_chip_revision(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "B0";
}

static int cros_system_xec_get_reset_cause(const struct device *dev)
{
	struct cros_system_xec_data *data = DRV_DATA(dev);

	return data->reset;
}

/* MCHP TODO check and verify this logic for all corner cases:
 * Someone doing ARM Vector Reset insead of SYSRESETREQ or HW reset.
 * Does NRESETIN# status get set also on power on from no power state?
 */
static int cros_system_xec_init(const struct device *dev)
{
	struct vbatr_regs *vbr = HAL_VBATR_INST(dev);
	struct cros_system_xec_data *data = DRV_DATA(dev);
	uint32_t pfsr = vbr->PFRS;

	if (IS_BIT_SET(pfsr, MCHP_VBATR_PFRS_WDT_POS)) {
		data->reset = WATCHDOG_RST;
		vbr->PFRS = BIT(MCHP_VBATR_PFRS_WDT_POS);
	} else if (IS_BIT_SET(pfsr, MCHP_VBATR_PFRS_SYSRESETREQ_POS)) {
		data->reset = DEBUG_RST;
		vbr->PFRS = BIT(MCHP_VBATR_PFRS_SYSRESETREQ_POS);
	} else if (IS_BIT_SET(pfsr, MCHP_VBATR_PFRS_RESETI_POS)) {
		data->reset = VCC1_RST_PIN;
	} else {
		data->reset = POWERUP;
	}

	return 0;
}

noreturn static int cros_system_xec_soc_reset(const struct device *dev)
{
	struct pcr_regs *const pcr = HAL_PCR_INST(dev);

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable_all();

	/* Trigger chip reset */
	pcr->SYS_RST |= MCHP_PCR_SYS_RESET_NOW;
	/* Wait for the soc reset */
	while (1)
		;
	/* should never return */
	/* return 0; */
}

static int cros_system_xec_hibernate(const struct device *dev,
				     uint32_t seconds, uint32_t microseconds)
{
	/* Disable interrupt first */
	interrupt_disable_all();

	/* Stop the watchdog */
	system_xec_watchdog_stop();

	/* Enter hibernate mode */

	/* MCHP TODO */

	return 0;
}

static struct cros_system_xec_data cros_system_xec_dev_data;

static const struct cros_system_xec_config cros_system_dev_cfg = {
	.base_pcr = DT_REG_ADDR_BY_NAME(DT_INST(0, microchip_xec_pcr), pcrr),
	.base_vbr = DT_REG_ADDR_BY_NAME(DT_INST(0, microchip_xec_pcr), vbatr),
	.base_wdog = DT_REG_ADDR(DT_INST(0, microchip_xec_watchdog)),
};

static const struct cros_system_driver_api cros_system_driver_xec_api = {
	.get_reset_cause = cros_system_xec_get_reset_cause,
	.soc_reset = cros_system_xec_soc_reset,
	.hibernate = cros_system_xec_hibernate,
	.chip_vendor = cros_system_xec_get_chip_vendor,
	.chip_name = cros_system_xec_get_chip_name,
	.chip_revision = cros_system_xec_get_chip_revision,
};

DEVICE_DEFINE(cros_system_xec_0, "CROS_SYSTEM", cros_system_xec_init, NULL,
	      &cros_system_xec_dev_data, &cros_system_dev_cfg, PRE_KERNEL_1,
	      CONFIG_CROS_SYSTEM_XEC_INIT_PRIORITY,
	      &cros_system_driver_xec_api);

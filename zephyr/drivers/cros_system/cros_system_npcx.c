/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/cros_system.h>
#include <drivers/watchdog.h>
#include <logging/log.h>
#include <soc.h>

LOG_MODULE_REGISTER(cros_system, LOG_LEVEL_ERR);

/* Driver config */
struct cros_system_npcx_config {
	/* hardware module base address */
	uintptr_t base_scfg;
	uintptr_t base_twd;
};

/* Driver data */
struct cros_system_npcx_data {
	int reset; /* reset cause */
};

/* Driver convenience defines */
#define DRV_CONFIG(dev) ((const struct cros_system_npcx_config *)(dev)->config)

#define HAL_SCFG_INST(dev) (struct scfg_reg *)(DRV_CONFIG(dev)->base_scfg)
#define HAL_TWD_INST(dev) (struct twd_reg *)(DRV_CONFIG(dev)->base_twd)

#define DRV_DATA(dev) ((struct cros_system_npcx_data *)(dev)->data)

/*
 * For cortex-m we cannot use irq_lock() for disabling all the interrupts
 * because it leaves some (NMI and faults) still enabled. Use "cpsid i" to
 * replace it.
 */
static inline void interrupt_disable_all(void)
{
	__asm__("cpsid i");
}

static int cros_system_npcx_get_reset_cause(const struct device *dev)
{
	struct cros_system_npcx_data *data = DRV_DATA(dev);

	return data->reset;
}

static int cros_system_npcx_init(const struct device *dev)
{
	struct scfg_reg *const inst_scfg = HAL_SCFG_INST(dev);
	struct twd_reg *const inst_twd = HAL_TWD_INST(dev);
	struct cros_system_npcx_data *data = DRV_DATA(dev);

	/* check reset cause */
	if (IS_BIT_SET(inst_twd->T0CSR, NPCX_T0CSR_WDRST_STS)) {
		data->reset = WATCHDOG_RST;
		inst_twd->T0CSR |= BIT(NPCX_T0CSR_WDRST_STS);
	} else if (IS_BIT_SET(inst_scfg->RSTCTL, NPCX_RSTCTL_DBGRST_STS)) {
		data->reset = DEBUG_RST;
		inst_scfg->RSTCTL |= BIT(NPCX_RSTCTL_DBGRST_STS);
	} else if (IS_BIT_SET(inst_scfg->RSTCTL, NPCX_RSTCTL_VCC1_RST_STS)) {
		data->reset = VCC1_RST_PIN;
	} else {
		data->reset = POWERUP;
	}

	return 0;
}

static int cros_system_npcx_soc_reset(const struct device *dev)
{
	struct twd_reg *const inst_twd = HAL_TWD_INST(dev);

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable_all();

	/*
	 * NPCX chip doesn't have the specific system reset functionality. Use
	 * watchdog reset as a system reset.
	 */

	/* Stop the watchdog */
	if (IS_ENABLED(CONFIG_WATCHDOG)) {
		const struct device *wdt_dev = device_get_binding(
			DT_LABEL(DT_INST(0, nuvoton_npcx_watchdog)));

		if (!wdt_dev) {
			LOG_ERR("wdt_dev get binding failed");
			return -ENODEV;
		}

		wdt_disable(wdt_dev);
	}

	/* Enable early touch */
	inst_twd->T0CSR &= ~BIT(NPCX_T0CSR_TESDIS);
	inst_twd->TWCFG |= BIT(NPCX_TWCFG_WDSDME);

	/*
	 * The trigger of a watchdog event by a "too early service" condition.
	 * When the watchdog is written more than once during three watchdog
	 * clock cycle.
	 */
	inst_twd->WDSDM = 0x5C;
	inst_twd->WDSDM = 0x5C;

	/* Wait for the soc reset. */
	while (1) {
		;
	}

	/* should never return */
	return 0;
}

static struct cros_system_npcx_data cros_system_npcx_dev_data;

static const struct cros_system_npcx_config cros_system_dev_cfg = {
	.base_scfg = DT_REG_ADDR(DT_INST(0, nuvoton_npcx_scfg)),
	.base_twd = DT_REG_ADDR(DT_INST(0, nuvoton_npcx_watchdog)),
};

static const struct cros_system_driver_api cros_system_driver_npcx_api = {
	.get_reset_cause = cros_system_npcx_get_reset_cause,
	.soc_reset = cros_system_npcx_soc_reset,
};

/*
 * The priority of cros_system_npcx_init() should be lower than watchdog init
 * for reset cause check.
 */
DEVICE_DEFINE(cros_system_npcx_0, "CROS_SYSTEM", cros_system_npcx_init, NULL,
	      &cros_system_npcx_dev_data, &cros_system_dev_cfg, PRE_KERNEL_1,
	      30, &cros_system_driver_npcx_api);

#define HAL_DBG_REG_BASE_ADDR \
	((struct dbg_reg *)DT_REG_ADDR(DT_INST(0, nuvoton_npcx_cros_dbg)))

#define DBG_NODE           DT_NODELABEL(dbg)
#define DBG_PINCTRL_PH     DT_PHANDLE_BY_IDX(DBG_NODE, pinctrl_0, 0)
#define DBG_ALT_FILED(f)   DT_PHA_BY_IDX(DBG_PINCTRL_PH, alts, 0, f)

static int jtag_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	struct dbg_reg *const dbg_reg_base = HAL_DBG_REG_BASE_ADDR;
	const struct npcx_alt jtag_alts[] = {
		{
			.group = DBG_ALT_FILED(group),
			.bit = DBG_ALT_FILED(bit),
			.inverted = DBG_ALT_FILED(inv)
		}
	};

	dbg_reg_base->DBGCTRL = 0x04;
	dbg_reg_base->DBGFRZEN3 &= ~BIT(NPCX_DBGFRZEN3_GLBL_FRZ_DIS);
	if (DT_NODE_HAS_STATUS(DT_NODELABEL(dbg), okay))
		npcx_pinctrl_mux_configure(jtag_alts, 1, 1);

	return 0;
}
SYS_INIT(jtag_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT + 1);
